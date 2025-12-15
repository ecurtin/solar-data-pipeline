from suntimes import SunTimes, SunFiles
from datetime import datetime, timezone, timedelta
from io import StringIO
import pandas as pd
import influxdb_client
from influxdb_client.client.write_api import SYNCHRONOUS


# 866 ft + another 20 for the roof = 270m
place = SunTimes(-84.40153, 33.78317, altitude=270)

day = datetime.today()

data_str = SunFiles(place, 2025, "ATL").get_csv()

data_io = StringIO(data_str)

# df = pd.read_csv(data_io, sep=r"\s+,\s+")
df = pd.read_csv(data_io)

# only keep month, day, 
droppable_columns = df.columns[6:]
df = df.drop(droppable_columns, axis=1)

column_name_remap = {
    " day": "day",
    " hrise_utc": "rise_hour_utc",
    " mrise_utc": "rise_minute_utc",
    " hset_utc": "set_hour_utc",
    " mset_utc": "set_minute_utc"
}

df = df.rename(columns=column_name_remap)
df.insert(0, "year", day.year)

def to_rise_datetime(row):
    return datetime(year=row["year"],
                    month=row["month"],
                    day=row["day"],
                    hour=row["rise_hour_utc"],
                    minute=row["rise_minute_utc"]
                    )
    
def to_set_datetime(row):
    return datetime(year=row["year"],
                    month=row["month"],
                    day=row["day"],
                    hour=row["set_hour_utc"],
                    minute=row["set_minute_utc"]
                    )

def to_midnight_utc_datetime(row):
    return datetime(year=row["year"],
                    month=row["month"],
                    day=row["day"])

df["rise-time"] = df.apply(to_rise_datetime, axis=1)
df["set-time"] = df.apply(to_set_datetime, axis=1)
df["midnight-utc"] = df.apply(to_midnight_utc_datetime, axis=1)

df = df[["rise-time", "set-time", "midnight-utc"]]
df = df.set_index("midnight-utc")





# df.to_csv("suntimes.csv", index=False)

BUCKET = "weather"
MEASUREMENT = "suntimes"
URL="http://localhost:8086"

client = influxdb_client.InfluxDBClient(url=URL)
write_client = client.write_api(write_options=SYNCHRONOUS)



write_client.write(
                    BUCKET, 
                   "my-org", 
                   record=df, 
                   data_frame_measurement_name=MEASUREMENT,
                   data_frame_tag_columns=['location'])
