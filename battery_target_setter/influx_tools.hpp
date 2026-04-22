/**
 * @file influx_tools.hpp
 *
 * Utilities to pull data from InfluxDB, and handle the fact that it's not a
 * real database that can do joins (i.e. we are implementing joins by hand
 * here!).
 */
#ifndef INFLUX_TOOLS_HPP
#define INFLUX_TOOLS_HPP

#include <armadillo>
#include <vector>
#include <map>
#include <InfluxDB/InfluxDBFactory.h>

// Auxiliary structure to hold an Influx dataset, with separate members for
// timestamps and data, and tracking of metadata (column names).
template<typename eT>
struct InfluxDataset
{
  // A vector containing the names of each feature (as reported by influx).
  std::vector<std::string> names;
  // Convenience reverse mapping from column name to index.
  std::map<std::string, size_t> colmap;
  // Timestamps for each row of the data.
  arma::uvec timestamps;
  // The actual data.
  arma::Mat<eT> data;
};

/**
 * Query Influx and convert the results to Armadillo format.
 * The timestamps are stored in `timestamps` and the actual data in `output`.
 *
 * If expectedRows is nonzero, then an exception will be thrown.
 */
template<typename eT = float>
InfluxDataset<eT> InfluxToArma(std::unique_ptr<influxdb::InfluxDB>& db,
                               const std::string& query,
                               const std::string& description,
                               const size_t expectedRows = 0)
{
  std::vector<influxdb::Point> points = db->query(query.c_str());
  if (expectedRows != 0 && points.size() != expectedRows)
  {
    std::ostringstream oss;
    oss << "Expected " << expectedRows << " in " << description << ", "
        << "but got " << points.size() << "!";
    throw std::runtime_error(oss.str());
  }
  else if (points.size() == 0)
  {
    std::ostringstream oss;
    oss << "Got empty results for " << description << "!";
    throw std::runtime_error(oss.str());
  }

  // Create the dataset.
  InfluxDataset<eT> d;
  d.timestamps.set_size(points.size());
  d.data.set_size(points[0].getFieldSet().size() +
      points[0].getTagSet().size(), points.size());

  // Set the column names.
  for (size_t j = 0; j < points[0].getFieldSet().size(); ++j)
    d.names.push_back(points[0].getFieldSet()[j].first);
  for (size_t j = 0; j < points[0].getTagSet().size(); ++j)
    d.names.push_back(points[0].getTagSet()[j].first);

  // Build the reverse map.
  for (size_t i = 0; i < d.names.size(); ++i)
    d.colmap[d.names[i]] = i;

  for (size_t i = 0; i < points.size(); ++i)
  {
    // Extract the timestamp into the relevant element of the timestamp vector.
    d.timestamps[i] = std::chrono::duration_cast<std::chrono::seconds>(
        points[i].getTimestamp().time_since_epoch()).count();

    // Assumptions: fields will always be in the same order;
    // all fields are ints, floats, or doubles.
    for (size_t j = 0; j < points[i].getFieldSet().size(); ++j)
    {
      const influxdb::Point::FieldValue& f = points[i].getFieldSet()[j].second;
      if (f.index() == 0)
        d.data(j, i) = (float) std::get<int>(f);
      else if (f.index() == 1)
        d.data(j, i) = (float) std::get<long long int>(f);
      else if (f.index() == 3)
        d.data(j, i) = (float) std::get<double>(f);
      else if (f.index() == 5)
        d.data(j, i) = (float) std::get<unsigned int>(f);
      else if (f.index() == 6)
        d.data(j, i) = (float) std::get<unsigned long long int>(f);
    }

    // Any tags are expected to be parsable dates.
    for (size_t j = 0; j < points[i].getTagSet().size(); ++j)
    {
      const std::pair<std::string, std::string>& tag = points[i].getTagSet()[j];
      // Ignore the name of the tag.
      // The value of the tag is going to be a string representation of a date.
      // Making std::chrono parse a timestamp is really quite ugly...
      std::istringstream iss(tag.second);
      tm t = {};
      iss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S");
      std::chrono::time_point tp =
          std::chrono::system_clock::from_time_t(mktime(&t));
      d.data(points[i].getFieldSet().size() + j, i) =
          std::chrono::duration_cast<std::chrono::seconds>(
          tp.time_since_epoch()).count();
    }
  }

  std::cout << "Successfully pulled " << d.data.n_rows << " x " << d.data.n_cols
      << " matrix from InfluxDB for " << description << "." << std::endl;

  return d;
}

// Perform an inner join on timestamps for multiple Influx databases.
template<typename eT>
InfluxDataset<eT> InnerJoin(const InfluxDataset<eT>& d1,
                            const InfluxDataset<eT>& d2)
{
  // Create the metadata for the output.
  InfluxDataset<eT> out;
  out.names.insert(out.names.end(), d1.names.begin(), d1.names.end());
  out.names.insert(out.names.end(), d2.names.begin(), d2.names.end());
  for (size_t i = 0; i < out.names.size(); ++i)
    out.colmap[out.names[i]] = i;

  arma::uvec leftTimes, rightTimes;
  // Populate out.timestamps with the common times between the two datasets.
  arma::intersect(out.timestamps, leftTimes, rightTimes, d1.timestamps,
      d2.timestamps);

  out.data.set_size(out.names.size(), out.timestamps.n_elem);
  out.data.rows(0, d1.names.size() - 1) = d1.data.cols(leftTimes);
  out.data.rows(d1.names.size(), out.data.n_rows - 1) = d2.data.cols(rightTimes);

  return out;
}

template<typename eT>
InfluxDataset<eT> InnerJoin(const InfluxDataset<eT>& d1,
                            const InfluxDataset<eT>& d2,
                            const InfluxDataset<eT>& d3)
{
  return InnerJoin(InnerJoin(d1, d2), d3);
}

template<typename eT>
InfluxDataset<eT> InnerJoin(const InfluxDataset<eT>& d1,
                            const InfluxDataset<eT>& d2,
                            const InfluxDataset<eT>& d3,
                            const InfluxDataset<eT>& d4)
{
  return InnerJoin(InnerJoin(d1, d2), InnerJoin(d3, d4));
}

#endif
