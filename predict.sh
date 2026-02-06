#!/bin/bash
#
# Simple test script to test the pipeline each day in a cron job.

cd /home/ryan/src/solar-data-pipeline/
source venv/bin/activate
# Get cloud cover data.  (TODO: remove?)
python3 pull_cloud_cover.py
# Get OpenMeteo forecast for yesterday and put it into Influx.
python3 openmeteo_historical_forecast_pull.py
# Get OpenMeteo forecast for today and also put it into Influx;
# the "weather.current_forecast" measurement will be dropped entirely
# and replaced with 24 observations for today.
python3 openmeteo_forecast_pull.py
./battery_target_setter/battery_target_setter 2>&1 | mail -s "Solar forecast" ryan@ratml.org emily@framebit.org
