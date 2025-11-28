import json
import re
from datetime import datetime, timedelta

import requests
from dateutil import parser

local_info = json.loads(open("grid-points.json").read())

OFFICE = local_info["office"]
GRID_X = local_info["grid-x"]
GRID_Y = local_info["grid-y"]


def match_time_format(unparsed_time_str):
    # returns a denormalized list of tuples for observation times and cloud cover values

    START_AND_END_TIME = r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:?\d{2}?)|NOW)\/(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:?\d{2}?)|NOW)$"
    START_TIME_AND_DURATION = r"^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:?\d{2}?)|NOW)\/P(\d+Y)?(\d+M)?(\d+D)?(T(\d+H)?(\d+M)?(\d+S)?)?$"
    DURATION_AND_END_TIME = r"^P(\d+Y)?(\d+M)?(\d+D)?(T(\d+H)?(\d+M)?(\d+S)?)?\/(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(Z|[+-]\d{2}:?\d{2}?)|NOW)$"

    x = unparsed_time_str  # convenience

    # the unparsed string should match one of these completely
    match_1 = re.match(START_AND_END_TIME, unparsed_time_str)
    match_2 = re.match(START_TIME_AND_DURATION, unparsed_time_str)
    match_3 = re.match(DURATION_AND_END_TIME, unparsed_time_str)

    if match_1:
        # parse match_1
        print("matches 1")
        raise NotImplementedError("Matches unimplemented regex")
    elif match_2:
        # this one'll work right now
        match_2.groups()
        date = match_2.groups()[0]
        # Technically the spec says they could return "NOW"
        start_time = datetime.now() if date == "NOW" else parser.parse(date)

        # 2 = year, 3 = month, 4 = day, 6 = hour, 7 = minute, 8 = second
        def _char_helper(time_str):
            if not time_str:
                return 0
            else:
                return int(time_str[:-1])

        # omg why tf are we handling years and months? great question. The answer is that
        # we are because EXTREME PEDANTRY.
        # To fit this into a timedelta, which doesn't take years and months,
        # we're doing years --> days + months --> days (30.4375 is the exact average number of days per month including leap years because unnnnnhhhh)
        # + actual days. This parameter is gonna be zero basically forever, but now we're adhering to the spec. Great.
        duration_delta = timedelta(
            days=_char_helper(match_2.groups()[2]) * 365
            + _char_helper(match_2.groups()[3]) * 30.4375
            + _char_helper(match_2.groups()[4]),
            hours=_char_helper(match_2.groups()[6]),
            minutes=_char_helper(match_2.groups()[7]),
            seconds=_char_helper(match_2.groups()[8]),
        )
        end_time = start_time + duration_delta
        return (start_time, end_time)

    elif match_3:
        print("matches 3")
        # parse match_3
        raise NotImplementedError("Matches unimplemented regex")
    else:
        raise ValueError("Unparseable date string received")


response = requests.get(
    f"https://api.weather.gov/gridpoints/{OFFICE}/{GRID_X},{GRID_Y}"
)

data = response.json()
cloud_cover_list = data["properties"]["skyCover"]["values"]
import pdb; pdb.set_trace()

forecast_list = []

for x in cloud_cover_list:

    valid_time_str = x["validTime"]
    cloud_cover_value = x["value"]

    (start_time, end_time) = match_time_format(valid_time_str)
    # print(f"Start time: {start_time}, end_time: {end_time}")

    # Have: Start time: 2025-12-05 12:00:00+00:00, end_time: 2025-12-05 15:00:00+00:00
    # Want: list of datetimes at one hour increments between those

    _forecast_list = []
    i_time = start_time
    while (i_time < end_time) :
        i_time += timedelta(hours=1)
        _forecast_list.append((i_time, cloud_cover_value))
    
    for f in _forecast_list:
        forecast_list.append(f)

for f in forecast_list:
    print(f)


# hours_from_now, predicted_cloud_cover

# be able to run as a script and get the next however many hours


# example of what I'm trying to get to
# (
#     ("forecast", as_of_time, valid_time ex: 12:00, value)
#     ("forecast", as_of_time, valid_time ex: 1:00, value)
# )
