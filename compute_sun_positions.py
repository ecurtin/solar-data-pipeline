from astropy.coordinates import SkyCoord, AltAz, EarthLocation, get_sun
from astropy.time import Time, TimeDelta
from astropy import units as u
import pandas as pd
import numpy as np
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS

LATITUDE = 33.78317
LONGITUDE = -84.40153

def compute_sun_altaz(t, lat, lon):
  """
  Compute the altitude and azimuth of the sun at the given Earth coordinates and
  30 minutes from the given time.  For our modeling purposes, we want the sun's
  coordinates halfway through the hour.
  """
  s = get_sun(Time(t) + TimeDelta(1800, format='sec'))
  e = EarthLocation(lat=(lat * u.deg), lon=(lon * u.deg))
  altaz = s.transform_to(AltAz(obstime=t, location=e))
  return (altaz.alt.degree, altaz.az.degree)


df = pd.DataFrame({ 'time': pd.date_range(start='2025-08-01',
                                          end='2050-05-01',
                                          freq='h',
                                          tz='America/New_York') })

print(df)

alt_azs = [compute_sun_altaz(t, LATITUDE, LONGITUDE) for t in df['time']]
alts = [x[0] for x in alt_azs]
azs = [x[1] for x in alt_azs]

df['sun_alt'] = alts
df['sun_az'] = azs

df = df.set_index('time')

# Now export the data to Influx.
client = influxdb_client.InfluxDBClient(url="http://localhost:8086")
write_client = client.write_api(write_options=SYNCHRONOUS)
write_client.write("weather",
                   "my-org",
                   record=df,
                   data_frame_measurement_name="sun_altaz")
