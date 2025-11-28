#include <mlpack.hpp>

using namespace arma;
using namespace mlpack;

int main()
{
  mat forecast;
  data::TextOptions opts = data::CSV + data::HasHeaders;
  data::Load("forecast.csv", forecast, opts);
  std::cout << "Read 'forecast.csv'; size: " << forecast.n_rows << "x"
    << forecast.n_cols << "; CSV headers:" << std::endl;
  for (size_t i = 0; i < opts.Headers().size(); ++i)
  {
    std::cout << "  Column " << i << ": '" << opts.Headers()[i] << "'."
        << std::endl;
  }

  std::cout << std::endl;

  // Compute the average cloud cover over the next forecast period.
  const double avgCover = mean(forecast.row(1));
  std::cout << "Predicted cloud cover over the next forecast period: "
      << avgCover << "\%." << std::endl;

  // The battery level doesn't go below 20%; here is an ad-hoc heuristic.  On a
  // day where we forecast 20% cloud cover or less, we will keep the battery at
  // 20%.  As the cloud cover increases, we will increase our battery level
  // linearly from cloud cover 20-100% to battery level 20-80%.
  double batteryTarget = avgCover;
  if (batteryTarget < 20.0)
    batteryTarget = 20.0;
  else
    batteryTarget = ((avgCover - 20.0) * 0.75) + 20.0;

  std::cout << "Target battery percentage given this forecast: "
      << batteryTarget << "\%." << std::endl;
}
