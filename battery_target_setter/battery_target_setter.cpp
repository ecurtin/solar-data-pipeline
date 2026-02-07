#include <mlpack.hpp>
#include <iomanip>
#include <mosquitto.h>
#include <InfluxDB/InfluxDBFactory.h>

using namespace arma;
using namespace mlpack;
using namespace std;
using namespace influxdb;

void onConnect(struct mosquitto *mosq, void *obj, int reasonCode);
void onPublish(struct mosquitto *mosq, void *obj, int mid);

static bool mqttConnected;
static bool mqttPublished;

fmat influxToArma(const std::vector<Point>& points)
{
  fmat m(points[0].getFieldSet().size(), points.size());

  for (size_t i = 0; i < points.size(); ++i)
  {
    // Assumptions: fields will always be in the same order;
    // all fields are ints, floats, or doubles.
    for (size_t j = 0; j < points[i].getFieldSet().size(); ++j)
    {
      const Point::FieldValue& f = points[i].getFieldSet()[j].second;
      if (f.index() == 0)
        m(j, i) = (float) get<int>(f);
      else if (f.index() == 1)
        m(j, i) = (float) get<long long int>(f);
      else if (f.index() == 3)
        m(j, i) = (float) get<double>(f);
      else if (f.index() == 5)
        m(j, i) = (float) get<unsigned int>(f);
      else if (f.index() == 6)
        m(j, i) = (float) get<unsigned long long int>(f);
    }
  }

  return m;
}

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
  vector<Point> points = influxdb->query("SELECT * FROM daily_forecast");
  if (points.size() != 24)
  {
    ostringstream oss;
    oss << "Expected 24 rows in daily forecast; got " << points.size() << "!";
    throw runtime_error(oss.str());
  }
  fmat dailyForecast = influxToArma(points);

  // Now get all of our historical data.
  points = influxdb->query("SELECT * FROM forecast_history");
  if (points.size() == 0)
  {
    throw runtime_error("Received empty results from forecast_history "
        "measurement!");
  }
  fmat forecastHistory = influxToArma(points);

  cout << "Daily forecast size:   " << dailyForecast.n_rows << " x "
      << dailyForecast.n_cols << "." << endl;
  cout << "Forecast history size: " << forecastHistory.n_rows << " x "
      << forecastHistory.n_cols << "." << endl;

  exit(0);

  // TODO: retrain model?
  //  - pull historical data
  //  - pull generation data (kWh / hr)
  //  - fit it with a regressor
  //  - print R^2 score
  //  - serialize the model ????? for debugging
  //  - predict with the model
  //  - do math to figure out where to set the battery given the predicted power
  //    generation
  //  - then reuse existing MQTT code

  mat forecast;
  data::TextOptions opts = data::CSV + data::HasHeaders;
  data::Load("forecast.csv", forecast, opts);
  cout << "Read 'forecast.csv'; size: " << forecast.n_rows << "x"
    << forecast.n_cols << "; CSV headers:" << endl;
  for (size_t i = 0; i < opts.Headers().size(); ++i)
    cout << "  Column " << i << ": '" << opts.Headers()[i] << "'." << endl;

  umat suntimes;
  data::TextOptions opts2 = data::CSV + data::HasHeaders;
  data::Load("suntimes.csv", suntimes, opts2);
  cout << "Read 'suntimes.csv'; size: " << suntimes.n_rows << "x"
      << suntimes.n_cols << "; CSV headers:" << endl;
  for (size_t i = 0; i < opts2.Headers().size(); ++i)
    cout << "  Column " << i << ": '" << opts2.Headers()[i] << "'." << endl;

  cout << endl;

  // Next we need to determine what the UTC day is, so that we can look up the
  // correct sunrise and sunset times.
  const time_t nowTime = chrono::system_clock::to_time_t(
      chrono::system_clock::now());
  const tm* nowObj = gmtime(&nowTime);
  const size_t year = (1900 + nowObj->tm_year);
  const size_t month = (1 + nowObj->tm_mon);
  const size_t day = nowObj->tm_mday;
  cout << "Today: " << year << "-" << setw(2) << setfill('0') << month
      << "-" << setw(2) << setfill('0') << day << "." << endl;

  // Find the row that matches the current day for sunrise/sunset.
  uvec colIndices = find(suntimes.row(0) == year &&
                         suntimes.row(1) == month &&
                         suntimes.row(2) == day);
  if (colIndices.n_elem == 0)
  {
    cerr << "No row found in suntimes.csv for date " << year << "-" << setw(2)
        << setfill('0') << month << "-" << setw(2) << setfill('0') << day << "!"
        << endl;
    exit(1);
  }
  else if (colIndices.n_elem > 1)
  {
    cerr << "Multiple rows found in suntimes.csv for date " << year << "-"
        << setw(2) << setfill('0') << month << "-" << setw(2) << setfill('0')
        << day << "!" << endl;
    exit(1);
  }

  const size_t sunriseHour = suntimes.at(3, colIndices[0]);
  const size_t sunriseMin = suntimes.at(4, colIndices[0]);
  const size_t sunsetHour = suntimes.at(5, colIndices[0]);
  const size_t sunsetMin = suntimes.at(6, colIndices[0]);
  cout << "Sunrise time (UTC): " << setw(2) << setfill('0') << sunriseHour
      << ":" << setw(2) << setfill('0') << sunriseMin << "." << endl;
  cout << "Sunset time (UTC):  " << setw(2) << setfill('0') << sunsetHour
      << ":" << setw(2) << setfill('0') << sunsetMin << "." << endl;

  // Compute the average cloud cover over the set of hours for which the sun is
  // up.  Assumptions/notes:
  //
  //  * When the sun is up for a partial hour, the average only considers the
  //    part of the hour where the sun is up.
  //  * We assume that all hours are exactly the same, which is definitely not
  //    true.  A more sophisticated model would weight the early and late hours
  //    less.
  uvec startIndices = find(forecast.row(0) == year &&
                           forecast.row(1) == month &&
                           forecast.row(2) == day &&
                           forecast.row(3) == sunriseHour);
  if (startIndices.n_elem == 0)
  {
    cerr << "No row found in forecast.csv for date " << year << "-" << setw(2)
        << setfill('0') << month << "-" << setw(2) << setfill('0') << day
        << ", hour " << sunriseHour << "!" << endl;
    exit(1);
  }
  else if (startIndices.n_elem > 1)
  {
    cerr << "Multiple rows found in forecast.csv for date " << year << "-"
        << setw(2) << setfill('0') << month << "-" << setw(2) << setfill('0')
        << day << ", hour " << sunriseHour << "!" << endl;
    exit(1);
  }

  uvec endIndices = find(forecast.row(0) == year &&
                         forecast.row(1) == month &&
                         forecast.row(2) == day &&
                         forecast.row(3) == sunsetHour);
  if (endIndices.n_elem == 0)
  {
    cerr << "No row found in forecast.csv for date " << year << "-" << setw(2)
        << setfill('0') << month << "-" << setw(2) << setfill('0') << day
        << ", hour " << sunsetHour << "!" << endl;
    exit(1);
  }
  else if (endIndices.n_elem > 1)
  {
    cerr << "Multiple rows found in forecast.csv for date " << year << "-"
        << setw(2) << setfill('0') << month << "-" << setw(2) << setfill('0')
        << day << ", hour " << sunsetHour << "!" << endl;
    exit(1);
  }

  const size_t startIndex = startIndices[0];
  const size_t endIndex = endIndices[0];
  const double firstHourPct = (60.0 - sunriseMin) / 60.0;
  const double lastHourPct = sunsetMin / 60.0;
  const double avgCover = ((firstHourPct * forecast(5, startIndex)) +
    // Atlanta will never see a day where startIndex + 1 == endIndex.
    sum(forecast.row(5).subvec(startIndex + 1, endIndex - 1)) +
    (lastHourPct * forecast(5, endIndex))) /
    (firstHourPct + (endIndex - startIndex - 1) + lastHourPct);

  cout << "Predicted cloud cover over the next forecast period: " << avgCover
      << "\%." << endl;

  // The battery level doesn't go below 20%; here is an ad-hoc heuristic.  On a
  // day where we forecast 20% cloud cover or less, we will keep the battery at
  // 20%.  As the cloud cover increases, we will increase our battery level
  // linearly from cloud cover 20-100% to battery level 20-80%.
  double batteryTarget = avgCover;
  if (batteryTarget < 20.0)
    batteryTarget = 20.0;
  else
    batteryTarget = (avgCover * 0.8) + 20.0;

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
