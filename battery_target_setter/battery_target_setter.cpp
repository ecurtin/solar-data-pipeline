#include <mlpack.hpp>
#include <iomanip>
#include <mosquitto.h>
#include <InfluxDB/InfluxDBFactory.h>

#include "influx_tools.hpp"
#include "print_dataset.hpp"

using namespace arma;
using namespace mlpack;
using namespace std;
using namespace influxdb;

void onConnect(struct mosquitto *mosq, void *obj, int reasonCode);
void onPublish(struct mosquitto *mosq, void *obj, int mid);

static bool mqttConnected;
static bool mqttPublished;

int main()
{
  // Initialize libmosquitto for MQTT commands.
  if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS)
  {
    cerr << "Error initializing mosquitto!  Sorry." << endl;
    exit(1);
  }

  unique_ptr<InfluxDB> influxdb = InfluxDBFactory::Get(
      "http://localhost:8086?db=weather");

  // First get today's forecast; should be 24 points.
  InfluxDataset<float> dailyForecast = InfluxToArma(influxdb,
      "SELECT * FROM daily_forecast", "daily forecast", 24);

  // Now get all of our historical forecast data.
  InfluxDataset<float> forecastHistory = InfluxToArma(influxdb,
      "SELECT * FROM forecast_history", "forecast history");
  // For some reason, this query picks up a column called 'date'.
  if (forecastHistory.colmap.count("date") > 0)
  {
    forecastHistory.data.shed_row(forecastHistory.colmap["date"]);
    forecastHistory.names.erase(forecastHistory.names.begin() +
        forecastHistory.colmap["date"]);
    forecastHistory.colmap.erase("date");
  }

  // When we do our feature engineering, we want to do it with the daily
  // forecast and forecast history together.
  if (dailyForecast.names.size() != forecastHistory.names.size())
  {
    ostringstream oss;
    oss << "Daily forecast contains " << dailyForecast.names.size()
        << " features, but forecast history contains "
        << forecastHistory.names.size() << " features!";
    throw runtime_error(oss.str());
  }

  if (dailyForecast.timestamps[0] - 3600 !=
      forecastHistory.timestamps[forecastHistory.timestamps.n_elem - 1])
  {
    time_t t1 = forecastHistory.timestamps[
        forecastHistory.timestamps.n_elem - 1];
    time_t t2 = dailyForecast.timestamps[0];
    ostringstream oss;
    oss << "Last timestamp of forecast history is "
        << put_time(localtime(&t1), "%c %Z") << ", but first timestamp of "
        << "daily forecast is " << put_time(localtime(&t2), "%c %Z")
        << "; they should be separated by 1 hour only!";
    throw runtime_error(oss.str());
  }

  // Historical data is the sum of what was observed at the end of the hour, but
  // since we are taking it as a "forecast" for our training data, we need to
  // shift the timestamp to refer to the start of the hour.
  forecastHistory.timestamps -= 3600;

  forecastHistory.timestamps.insert_rows(forecastHistory.timestamps.n_rows,
      dailyForecast.timestamps);
  forecastHistory.data.insert_cols(forecastHistory.data.n_cols,
      dailyForecast.data);

  // Lastly, get sun azimuth and altitude data.
  std::ostringstream altazQuery;
  altazQuery << "SELECT * FROM sun_altaz WHERE time <= " << std::time(nullptr)
      << "000000000";
  InfluxDataset<float> sunAltaz = InfluxToArma(influxdb, altazQuery.str(),
      "sun alt/az");

  // Because the azimuth of the sun will always go from 360 to 0 (since 0 is
  // directly south, and the sun is always south of us), notmalize it to
  // actually be the number of degrees we are off from due south.
  sunAltaz.data.row(sunAltaz.colmap["sun_az"]) =
      180.0 - abs(sunAltaz.data.row(sunAltaz.colmap["sun_az"]) - 180.0);

  // Now pull historical data for the total time range.

  // SELECT mean(combined) from "PV power hourly" group by time(1h) where
  //  time >= minTime and time <= maxTime
  unique_ptr<InfluxDB> influxdbSolar = InfluxDBFactory::Get(
      "http://localhost:8086?db=solar_assistant");

  InfluxDataset<float> generationHistory = InfluxToArma(influxdbSolar,
      "SELECT mean(combined) AS hourly_gen FROM \"PV power\" GROUP BY time(1h)",
      "generation history");
  // Add zeros for the time we will predict.
  generationHistory.timestamps.insert_rows(generationHistory.timestamps.n_rows,
      dailyForecast.timestamps);
  generationHistory.data.insert_cols(generationHistory.data.n_cols,
      zeros<frowvec>(dailyForecast.timestamps.n_elem));

  // Find the time indices in generationHistoryTimes that match time indices in
  // forecastHistoryTimes.
  InfluxDataset<float> predictors = InnerJoin(generationHistory,
      forecastHistory, sunAltaz);
  cout << "Times in common: " << predictors.data.n_cols << "." << endl;

  // Generate historical generation features.
  frowvec lastDayGen(predictors.timestamps.n_elem);
  lastDayGen.subvec(24, lastDayGen.n_elem - 1) = predictors.data.submat(0, 0,
      0, predictors.data.n_cols - 25);

  predictors.data.insert_rows(predictors.data.n_rows, lastDayGen);
  predictors.names.push_back("gen_yesterday_this_time");
  predictors.colmap["gen_yesterday_this_time"] = predictors.data.n_rows - 1;

  frowvec avgLastWeekGen(predictors.timestamps.n_elem);
  // Sum each of the last seven days (or however many we have).
  for (size_t d = 0; d < 7; ++d)
  {
    avgLastWeekGen.subvec(24 * (d + 1), lastDayGen.n_elem - 1) +=
        predictors.data.submat(0, 0, 0,
                               predictors.data.n_cols - 1 - 24 * (d + 1));
  }

  for (size_t d = 2; d <= 7; ++d)
  {
    avgLastWeekGen.subvec(24 * d,
        (d < 7) ? (24 * (d + 1) - 1) : avgLastWeekGen.n_elem - 1) /= d;
  }

  predictors.data.insert_rows(predictors.data.n_rows, avgLastWeekGen);
  predictors.names.push_back("avg_gen_last_week_this_time");
  predictors.colmap["avg_gen_last_week_this_time"] = predictors.data.n_rows - 1;

  // Compute recent cloud variance over the past day and three days.
  frowvec cloudCoverVarianceYesterday(predictors.data.n_cols);
  frowvec cloudCoverVariance3Days(predictors.data.n_cols);
  for (size_t i = 48; i < 96; ++i)
  {
    cloudCoverVarianceYesterday[i] = var(vectorise(
        predictors.data.submat(1, i - 48, 1, i - 24)));
  }
  for (size_t i = 96; i < predictors.data.n_cols; ++i)
  {
    cloudCoverVarianceYesterday[i] = var(vectorise(
        predictors.data.submat(1, i - 48, 1, i - 24)));
    cloudCoverVariance3Days[i] = var(vectorise(
        predictors.data.submat(1, i - 96, 1, i - 24)));
  }

  predictors.data.insert_rows(predictors.data.n_rows, cloudCoverVarianceYesterday);
  predictors.names.push_back("cloud_cover_var_yesterday");
  predictors.colmap["cloud_cover_var_yesterday"] = predictors.data.n_rows - 1;

  predictors.data.insert_rows(predictors.data.n_rows, cloudCoverVariance3Days);
  predictors.names.push_back("cloud_cover_var_last_3_days");
  predictors.colmap["cloud_cover_var_last_3_days"] = predictors.data.n_rows - 1;

  // Print the dataset before we filter down to only rows that have any
  // generation at all, just so that it makes more sense to the reader.
  PrintDataset(predictors);

  // Split off our test set.  Drop the first row too, since that's (zeroed)
  // generation data.
  fmat genTest = predictors.data.submat(1, predictors.data.n_cols - 25,
      predictors.data.n_rows - 1, predictors.data.n_cols - 1);

  // This will filter out the data in the test set (since the prediction values
  // are set to zero).
  const uvec validIndices = find(predictors.data.row(0) > 0);

  predictors.data = predictors.data.cols(validIndices);
  predictors.timestamps = predictors.timestamps.elem(validIndices);

  // Train a Bayesian linear regression model on random data and make predictions.
  // All data and responses are uniform random; this uses 10 dimensional data.
  // Replace with a Load() call or similar for a real application.

  frowvec gen = predictors.data.row(0);
  predictors.data.shed_row(0);

  // Note: if a simpler model like BayesianLinearRegression is being used, it
  // can be worthwhile to divide generation and also radiation-related variables
  // by 'terrestrial_radiation', so really we are predicting
  // generation/irradiation.
  mlpack::DecisionTreeRegressor genModel; // Step 1: create model.
  genModel.Train(predictors.data, gen); // Step 2: train model.
  frowvec predictions;
  frowvec genTestPreds;

  // Now compute the RMSE on the training set.
  genModel.Predict(predictors.data, predictions);
  genModel.Predict(genTest, genTestPreds);

  // Three different regimes: 0-500 Wh, 500-5000 Wh, and 5000+ Wh.

  // TODO: really, we should compute these measures as part of k-fold
  // cross-validation.
  const double rmse500 = sqrt(accu((gen < 500.0) %
      pow(predictions - gen, 2)) / accu((gen < 500.0)));
  const double rmse5k = sqrt(accu((gen >= 500.0 && gen < 5000.0) %
      pow(predictions - gen, 2)) / accu((gen >= 500.0 && gen < 5000.0)));
  const double rmseMax = sqrt(accu((gen >= 5000.0) %
      pow(predictions - gen, 2)) / accu((gen >= 5000.0)));
  const size_t rmse500Count = accu(gen < 500.0);
  const size_t rmse5kCount = accu(gen >= 500.0 && gen < 5000.0);
  const size_t rmseMaxCount = accu(gen >= 5000.0);
  cout << "RMSE on the training set <  500 Wh:            " << rmse500 << "." << std::endl;
  cout << "RMSE on the training set >= 500 Wh, < 5000 Wh: " << rmse5k << "." << std::endl;
  cout << "RMSE on the training set >= 5000 Wh:           " << rmseMax << "." << std::endl;
  cout << "Training set points < 500 Wh: " << rmse500Count << "." << endl;
  cout << "Training set points >= 500 Wh, < 5000 Wh: " << rmse5kCount << "." << endl;
  cout << "Training set points >= 5000 Wh: " << rmseMaxCount << "." << endl;

  cout << "RMSE on the training set: "
      << sqrt(mean(pow(predictions - gen, 2))) << "." << std::endl;

  // Next, we need to collect data to build a model on power usage.
  InfluxDataset<float> usageHistory = InfluxToArma(influxdbSolar,
      "SELECT mean(inverter_0) AS hourly_usage FROM \"Load power essential\" GROUP BY time(1h)",
      "usage history");

  // We can just use the same features as for generation forecasting; many of
  // them won't be anywhere near as useful.
  InfluxDataset<float> usageFeatures = InnerJoin(usageHistory, forecastHistory);

  // Now construct the set of valid times we want to predict for; this is
  // between 7am and 11pm every day.
  uvec usageTimes = ones<uvec>(usageFeatures.timestamps.n_elem);
  for (size_t i = 0; i < usageFeatures.timestamps.n_elem; ++i)
  {
    time_t t = usageFeatures.timestamps[i];
    tm* l = localtime(&t);
    if (l->tm_hour < 7 || l->tm_hour >= 23)
      usageTimes[i] = 0;
  }

  // Now filter down only to times we care about.
  const uvec usageIndices = find(usageTimes);
  usageFeatures.timestamps = usageFeatures.timestamps.elem(usageIndices);
  usageFeatures.data = usageFeatures.data.cols(usageIndices);

  frowvec usages = usageFeatures.data.row(0);
  usageFeatures.data.shed_row(0);

  // Build the model.
  DecisionTreeRegressor usageModel;
  usageModel.Train(usageFeatures.data, usages);

  frowvec usagePreds;
  frowvec usageTestPreds;
  usageModel.Predict(usageFeatures.data, usagePreds);
  usageModel.Predict(dailyForecast.data, usageTestPreds);

  const double rmse500u = sqrt(accu((usages < 500.0) %
      pow(usagePreds - usages, 2)) / accu((usages < 500.0)));
  const double rmse5ku = sqrt(accu((usages >= 500.0 && usages < 5000.0) %
      pow(usagePreds - usages, 2)) / accu((usages >= 500.0 && usages < 5000.0)));
  const double rmseMaxu = sqrt(accu((usages >= 5000.0) %
      pow(usagePreds - usages, 2)) / accu((usages >= 5000.0)));
  const size_t rmse500Countu = accu(gen < 500.0);
  const size_t rmse5kCountu = accu(gen >= 500.0 && gen < 5000.0);
  const size_t rmseMaxCountu = accu(gen >= 5000.0);
  cout << "RMSE on the training set <  500 Wh:            " << rmse500u << "." << std::endl;
  cout << "RMSE on the training set >= 500 Wh, < 5000 Wh: " << rmse5ku << "." << std::endl;
  cout << "RMSE on the training set >= 5000 Wh:           " << rmseMaxu << "." << std::endl;
  cout << "Training set points < 500 Wh: " << rmse500Countu << "." << endl;
  cout << "Training set points >= 500 Wh, < 5000 Wh: " << rmse5kCountu << "." << endl;
  cout << "Training set points >= 5000 Wh: " << rmseMaxCountu << "." << endl;

  cout << "RMSE on the training set: "
      << sqrt(mean(pow(usagePreds - usages, 2))) << "." << std::endl;

  // Now print predictions for the next 24 hours.
  cout << endl << "Predictions for the next 24 hours:" << endl;
  for (size_t i = 0; i < dailyForecast.timestamps.n_elem; ++i)
  {
    time_t t = dailyForecast.timestamps[i];
    cout << put_time(localtime(&t), "%c %Z") << ": "
        << genTestPreds[i] << " Wh generated, "
        << usageTestPreds[i] << " Wh used." << endl;
  }

  exit(0);

  // TODO: we'll actually have a prediction for this soon enough.

  const double batteryTarget = 20.0;

  cout << "Target battery percentage given this forecast: "
      << batteryTarget << "\%." << endl;

  // Now set the actual target battery level for the inverter.  We need to make
  // potentially three setting changes:
  //
  // - The target charge level should be set to `batteryTarget`.
  //      MQTT topic '/solar-assistant/inverter_1/capacity_point_2/set/'
  // - The "do not use battery below" level should be set to `batteryTarget` + 1
  //   to prevent insane hysteretic (hysterical??) behavior.
  //      MQTT topic
  //      '/solar-assistant/inverter_1/stop_battery_discharge_capacity/set/'
  // - "Charge from grid" may need to be enabled.
  //      '/solar-assistant/inverter_1/grid_charge_point_2/set/'
  //
  // The calendar on the inverter is already set such that the *second* time
  // slot is configured to be 5am to 7am.  (TODO: what happens when daylight
  // savings time happens on the inverter?  Does it handle that correctly even
  // without network?)
  ostringstream oss;
  oss << size_t(batteryTarget);

  mosquitto* mosq = mosquitto_new("battery_target_setter", true, NULL);
  mosquitto_connect_callback_set(mosq, onConnect);
  mosquitto_publish_callback_set(mosq, onPublish);
  mqttConnected = false;
  int err = mosquitto_connect(mosq, "pastrami", 1883, 10);
  if (err != MOSQ_ERR_SUCCESS)
  {
    cerr << "Error connecting to MQTT server on pastrami!  Code: "
        << err << "." << endl;
    exit(1);
  }

  err = mosquitto_loop_start(mosq);
  if (err != MOSQ_ERR_SUCCESS)
  {
    cerr << "Error starting mosquitto loop!  Code: " << err << "."
        << endl;
  }

  while (!mqttConnected)
  {
    usleep(50000);
  }
  cout << "Connected to MQTT server." << endl;

  mqttPublished = false;
  err = mosquitto_publish(mosq, NULL,
      "solar_assistant/inverter_1/capacity_point_2/set", oss.str().size(),
      oss.str().c_str(), 0, false);
  if (err != MOSQ_ERR_SUCCESS)
  {
    cout << "Error setting capacity_point_2!  Code: " << err << "." << endl;
  }

  while (!mqttPublished)
  {
    usleep(50000);
  }
  cout << "Sent message to 'capacity_point_2'." << endl;

  oss = ostringstream();
  oss << size_t(std::min(batteryTarget + 1, 100.0));
  mqttPublished = false;
  err = mosquitto_publish(mosq, NULL,
      "solar_assistant/inverter_1/stop_battery_discharge_capacity/set",
      oss.str().size(), oss.str().c_str(), 0, false);
  if (err != MOSQ_ERR_SUCCESS)
  {
    cout << "Error setting stop_battery_discharge_capacity!  Code: " << err
        << "." << endl;
  }

  while (!mqttPublished)
  {
    usleep(50000);
  }
  cout << "Sent message to 'stop_battery_discharge_capacity'." << endl;

  mqttPublished = false;
  err = mosquitto_publish(mosq, NULL,
      "solar_assistant/inverter_1/grid_charge_point_2/set", 4, "true", 0,
      false);
  if (err != MOSQ_ERR_SUCCESS)
  {
    cout << "Error setting grid_charge_point_2!  Code: " << err << "." << endl;
  }

  while (!mqttPublished)
  {
    usleep(50000);
  }
  cout << "Sent message to 'grid_charge_point_2'." << endl;

  mosquitto_destroy(mosq);
  mosquitto_lib_cleanup();
}

void onConnect(struct mosquitto* /* mosq */, void* /* obj */, int reasonCode)
{
  if (reasonCode != 0)
  {
    cerr << "Problem connecting to MQTT server!  Code: " << reasonCode << "."
        << endl;
    exit(1);
  }

  mqttConnected = true;
}

void onPublish(struct mosquitto* mosq, void* obj, int mid)
{
  mqttPublished = true;
}
