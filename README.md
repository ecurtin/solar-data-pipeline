# solar-system

A data pipeline and lightweight prediction system for managing the battery level of a home solar installation.

## Status: In Active Development

## Idea

We have:
- A home solar installation.
- A home battery installation.
- A plan with our electric utility that lets us pay less for power in off-peak hours. 

The solar and battery installation do NOT generate enough to keep us fully off-grid except maybe the most optimal 5 days per year. Getting off grid was gonna be way too expensive.

**The goal** of this system is to charge our home battery in off-peak hours to an optimal level for the next day.

**Basic Examples:**
- Is tomorrow gonna be sunny with 14 hours of strong daylight? Maybe you don't need to charge from the grid at all because you'll do enough generation during peak cost hours.
- Is tomorrow gonna be cloudy all day? You should charge the battery up a bunch off-peak so that you use the battery during peak cost hours instead of buying expensive power from the grid.

## Structure

This system runs on a [Solar Assistant Raspberry Pi](https://solar-assistant.io/). Solar Assistant uses [MQTT](https://mqtt.org/) as the message broker between system components.

### `pull_cloud_cover.py`: runs 1x daily ~4:00am
Pulls (and denormalizes) cloud cover percentage predictions from the National Weather Service API. Generates a CSV for the next 24 hours.

### `daylight.py`: runs 1x year
Generates a CSV with each day's sunrise and sunset info using [suntimes](https://pypi.org/project/suntimes/)

### `battery_target_setter.cpp`: runs 1x daily after feature pulls
- Connects with Solar Assistant MQTT using the `mosquitto` C library.
- Predicts the target battery percentage.
    - Currently this is done by a simple heuristic as a proof-of-concept. 
    - Soon this will use a low-resource ML system using [mlpack](https://mlpack.org/).
- Sets that battery charge target via MQTT.
- Also sets some other parameters in Solar Assistant to reduce battery charge thrashing.
