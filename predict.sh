#!/bin/bash
#
# Simple test script to test the pipeline each day in a cron job.

python3 pull_cloud_cover.py
./battery_target_setter/battery_target_setter | mail -s "Solar forecast" ryan@ratml.org emily@framebit.org
