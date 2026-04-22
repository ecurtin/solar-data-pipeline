# openmeteo_all_historical_forecast_pull.py
#
# Pull ALL historical forecast data from August 2025 until today and push it
# into InfluxDB.
#
# This should (ideally) only be run once.
import openmeteo_requests
import pandas as pd
import requests_cache
from retry_requests import retry
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

# Setup the Open-Meteo API client with cache and retry on error
cache_session = requests_cache.CachedSession('.cache', expire_after = 3600)
retry_session = retry(cache_session, retries = 5, backoff_factor = 0.2)
openmeteo = openmeteo_requests.Client(session = retry_session)

# Make sure all required weather variables are listed here
# The order of variables in hourly or daily is important to assign them
# correctly below
url = "https://historical-forecast-api.open-meteo.com/v1/forecast"
params = {
  "latitude": 33.78317,
  "longitude": -84.40153,
  "start_date": "2025-08-01",
  "end_date": "2026-04-21",
  "hourly": ["temperature_2m",
             "shortwave_radiation",
             "direct_radiation",
             "diffuse_radiation",
             "direct_normal_irradiance",
             "terrestrial_radiation",
             "global_tilted_irradiance",
             "soil_temperature_0_to_10cm",
             "soil_moisture_0_to_10cm",
             "surface_temperature",
             "wind_speed_10m",
             "wind_gusts_10m",
             "relative_humidity_2m",
             "dew_point_2m",
             "apparent_temperature",
             "precipitation_probability",
             "precipitation",
             "uv_index",
             "uv_index_clear_sky",
             "thunderstorm_probability",
             "rain_probability",
             "snowfall_probability",
             "freezing_rain_probability",
             "ice_pellets_probability",
             "cape",
             "lifted_index",
             "convective_inhibition",
             "wet_bulb_temperature_2m",
             "rain",
             "showers",
             "snowfall",
             "snow_depth",
             "pressure_msl",
             "surface_pressure",
             "cloud_cover",
             "cloud_cover_low",
             "cloud_cover_mid",
             "cloud_cover_high",
             "evapotranspiration",
             "visibility",
             "et0_fao_evapotranspiration",
             "vapour_pressure_deficit"],
  "models": "gfs_seamless",
  "tilt": 24
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
hourly_data["terrestrial_radiation"] = hourly.Variables(5).ValuesAsNumpy()
hourly_data["global_tilted_irradiance"] = hourly.Variables(6).ValuesAsNumpy()
hourly_data["soil_temperature_0_to_10cm"] = hourly.Variables(7).ValuesAsNumpy()
hourly_data["soil_moisture_0_to_10cm"] = hourly.Variables(8).ValuesAsNumpy()
hourly_data["surface_temperature"] = hourly.Variables(9).ValuesAsNumpy()
hourly_data["wind_speed_10m"] = hourly.Variables(10).ValuesAsNumpy()
hourly_data["wind_gusts_10m"] = hourly.Variables(11).ValuesAsNumpy()
hourly_data["relative_humidity_2m"] = hourly.Variables(12).ValuesAsNumpy()
hourly_data["dew_point_2m"] = hourly.Variables(13).ValuesAsNumpy()
hourly_data["apparent_temperature"] = hourly.Variables(14).ValuesAsNumpy()
hourly_data["precipitation_probability"] = hourly.Variables(15).ValuesAsNumpy()
hourly_data["precipitation"] = hourly.Variables(16).ValuesAsNumpy()
hourly_data["uv_index"] = hourly.Variables(17).ValuesAsNumpy()
hourly_data["uv_index_clear_sky"] = hourly.Variables(18).ValuesAsNumpy()
hourly_data["thunderstorm_probability"] = hourly.Variables(19).ValuesAsNumpy()
hourly_data["rain_probability"] = hourly.Variables(20).ValuesAsNumpy()
hourly_data["snowfall_probability"] = hourly.Variables(21).ValuesAsNumpy()
hourly_data["freezing_rain_probability"] = hourly.Variables(22).ValuesAsNumpy()
hourly_data["ice_pellets_probability"] = hourly.Variables(23).ValuesAsNumpy()
hourly_data["cape"] = hourly.Variables(24).ValuesAsNumpy()
hourly_data["lifted_index"] = hourly.Variables(25).ValuesAsNumpy()
hourly_data["convective_inhibition"] = hourly.Variables(26).ValuesAsNumpy()
hourly_data["wet_bulb_temperature_2m"] = hourly.Variables(27).ValuesAsNumpy()
hourly_data["rain"] = hourly.Variables(28).ValuesAsNumpy()
hourly_data["showers"] = hourly.Variables(29).ValuesAsNumpy()
hourly_data["snowfall"] = hourly.Variables(30).ValuesAsNumpy()
hourly_data["snow_depth"] = hourly.Variables(31).ValuesAsNumpy()
hourly_data["pressure_msl"] = hourly.Variables(32).ValuesAsNumpy()
hourly_data["surface_pressure"] = hourly.Variables(33).ValuesAsNumpy()
hourly_data["cloud_cover"] = hourly.Variables(34).ValuesAsNumpy()
hourly_data["cloud_cover_low"] = hourly.Variables(35).ValuesAsNumpy()
hourly_data["cloud_cover_mid"] = hourly.Variables(36).ValuesAsNumpy()
hourly_data["cloud_cover_high"] = hourly.Variables(37).ValuesAsNumpy()
hourly_data["evapotranspiration"] = hourly.Variables(38).ValuesAsNumpy()
hourly_data["visibility"] = hourly.Variables(39).ValuesAsNumpy()
hourly_data["et0_fao_evapotranspiration"] = hourly.Variables(40).ValuesAsNumpy()
hourly_data["vapour_pressure_deficit"] = hourly.Variables(41).ValuesAsNumpy()

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
