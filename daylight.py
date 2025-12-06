from suntimes import SunTimes, SunFiles
from datetime import datetime
from io import StringIO
import pandas as pd

# 866 ft + another 20 for the roof = 270m
place = SunTimes(-84.40153, 33.78317, altitude=270)

day = datetime.today()

data_str = SunFiles(place, 2025, "ATL_HOE").get_csv()

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

df.to_csv("suntimes.csv", index=False)
