import openmeteo_requests

import pandas as pd
import requests_cache
from retry_requests import retry
from datetime import datetime, timedelta

# Setup the Open-Meteo API client with cache and retry on error
cache_session = requests_cache.CachedSession('.cache', expire_after = 3600)
retry_session = retry(cache_session, retries = 5, backoff_factor = 0.2)
openmeteo = openmeteo_requests.Client(session = retry_session)

# Get yesterday's data (this is expected to run via cron every day at ~4am).
start_date = datetime.today() - timedelta(days=1)
start_date_str = start_date.strftime("%Y-%m-%d")

# Make sure all required weather variables are listed here
# The order of variables in hourly or daily is important to assign them
# correctly below
url = "https://historical-forecast-api.open-meteo.com/v1/forecast"
params = {
  "latitude": 33.78317,
  "longitude": -84.40153,
  "start_date": start_date_str,
  "end_date": start_date_str,
  "hourly": ["temperature_2m",
             "shortwave_radiation",
             "direct_radiation",
             "diffuse_radiation",
             "direct_normal_irradiance",
             "global_tilted_irradiance",
             "terrestrial_radiation",
             "cloud_cover",
             "cloud_cover_low",
             "cloud_cover_mid",
             "cloud_cover_high",
             "precipitation_probability",
             "precipitation",
             "rain",
             "showers",
             "snowfall",
             "snow_depth",
             "visibility"],
  "models": "best_match",
}
# This throws an exception on failure.
responses = openmeteo.weather_api(url, params=params)

# Process first location. Add a for-loop for multiple locations or weather
# models
response = responses[0]
print(f"Coordinates: {response.Latitude()}°N {response.Longitude()}°E")
print(f"Elevation: {response.Elevation()} m asl")
print(f"Timezone difference to GMT+0: {response.UtcOffsetSeconds()}s")

# Process hourly data. The order of variables needs to be the same as requested.
hourly = response.Hourly()

hourly_data = {"date": pd.date_range(
  start = pd.to_datetime(hourly.Time(), unit = "s", utc = True),
  end =  pd.to_datetime(hourly.TimeEnd(), unit = "s", utc = True),
  freq = pd.Timedelta(seconds = hourly.Interval()),
  inclusive = "left"
)}
hourly_data["temperature_2m"] = hourly.Variables(0).ValuesAsNumpy()
hourly_data["shortwave_radiation"] = hourly.Variables(1).ValuesAsNumpy()
hourly_data["direct_radiation"] = hourly.Variables(2).ValuesAsNumpy()
hourly_data["diffuse_radiation"] = hourly.Variables(3).ValuesAsNumpy()
hourly_data["direct_normal_irradiance"] = hourly.Variables(4).ValuesAsNumpy()
hourly_data["global_tilted_irradiance"] = hourly.Variables(5).ValuesAsNumpy()
hourly_data["terrestrial_radiation"] = hourly.Variables(6).ValuesAsNumpy()
hourly_data["cloud_cover"] = hourly.Variables(7).ValuesAsNumpy()
hourly_data["cloud_cover_low"] = hourly.Variables(8).ValuesAsNumpy()
hourly_data["cloud_cover_mid"] = hourly.Variables(9).ValuesAsNumpy()
hourly_data["cloud_cover_high"] = hourly.Variables(10).ValuesAsNumpy()
hourly_data["precipitation_probability"] = hourly.Variables(11).ValuesAsNumpy()
hourly_data["precipitation"] = hourly.Variables(12).ValuesAsNumpy()
hourly_data["rain"] = hourly.Variables(13).ValuesAsNumpy()
hourly_data["showers"] = hourly.Variables(14).ValuesAsNumpy()
hourly_data["snowfall"] = hourly.Variables(15).ValuesAsNumpy()
hourly_data["snow_depth"] = hourly.Variables(16).ValuesAsNumpy()
hourly_data["visibility"] = hourly.Variables(17).ValuesAsNumpy()

hourly_dataframe = pd.DataFrame(data = hourly_data)
hourly_dataframe = hourly_dataframe.set_index("date")
print("\nHourly data\n", hourly_dataframe)

# Now export the data to Influx.
client = influxdb_client.InfluxDBClient(url="http://localhost:8086")
write_client = client.write_api(write_options=SYNCHRONOUS)
write_client.write("weather",
                   "my-org",
                   record=hourly_dataframe,
                   data_frame_measurement_name="forecast_history")
