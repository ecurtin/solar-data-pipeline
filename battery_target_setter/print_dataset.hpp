/**
 * @file print_dataset.hpp
 *
 * Utility to print the last 24 hours of a dataset in a nice way.
 */
#ifndef PRINT_DATASET_HPP
#define PRINT_DATASET_HPP

#include <iomanip>
#include "influx_tools.hpp"

template<typename eT>
void PrintDataset(const InfluxDataset<eT>& d)
{
  setenv("TZ", "/usr/share/zoneinfo/America/New_York", 1);
  std::time_t t = d.timestamps[d.timestamps.n_elem - 24];
  std::cout << "Start time of data snippet: "
      << std::put_time(std::localtime(&t), "%c %Z") << "." << std::endl;

  // Compute the maximum width of the column names.
  size_t maxChars = 0;
  for (size_t r = 0; r < d.names.size(); ++r)
    if (d.names[r].size() > maxChars)
      maxChars = d.names[r].size();

  // Print the header row.
  std::cout << std::setw(maxChars + 3) << std::setfill(' ') << "";
  for (size_t i = 0; i < 24; ++i)
    std::cout << std::setw(8) << i << "  ";
  std::cout << std::endl;

  for (size_t r = 0; r < d.names.size(); ++r)
  {
    std::cout << std::setw(maxChars) << std::setfill(' ') << d.names[r]
        << "   ";

    for (size_t c = d.timestamps.n_elem - 24; c < d.timestamps.n_elem; ++c)
    {
      if (std::abs(d.data(r, c)) >= 1000)
      {
        std::cout << std::setw(8) << std::scientific << std::setprecision(2)
            << std::setfill(' ') << d.data(r, c) << "  ";
      }
      else if (abs(d.data(r, c)) >= 100)
      {
        std::cout << std::setw(8) << std::fixed << std::setprecision(3)
            << std::setfill(' ') << d.data(r, c) << "  ";
      }
      else if (abs(d.data(r, c)) >= 10)
      {
        std::cout << std::setw(8) << std::fixed << std::setprecision(4)
            << std::setfill(' ') << d.data(r, c) << "  ";
      }
      else
      {
        std::cout << std::setw(8) << std::fixed << std::setprecision(5)
            << std::setfill(' ') << d.data(r, c) << "  ";
      }
    }
    std::cout << std::endl;
  }
}

#endif
