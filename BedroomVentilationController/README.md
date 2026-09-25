# Bedroom Ventilation Controller

An ESP32-S3 controller for automating a Nilan CTS400 ventilation unit.

## Features

- Switches automatically between day and night ventilation schedules.
- Loads the schedule, winter period, and fan levels from `/ventilation.cfg` on an SD card.
- Supports separate supply and extract fan levels 1–3 for both day and night modes.
- Opens the Nilan bypass at night and closes it during the configured winter period.
- Serves a local web page that shows controller status, configured settings, and supported Nilan CTS400 readings.
- Lets users update configuration settings from the web page; changes are validated and saved back to the SD card.
- Can use DHCP by default or an optional static IP address, gateway, subnet mask, and DNS server.
- Optionally reduces night fan levels as outdoor temperature falls below a configurable threshold.
- Refreshes the temperature-based night fan adjustment every 15 minutes.
- Synchronizes time via NTP and sends email notifications after configuration changes or failures.
- Includes [`ventilation.cfg.example`](ventilation.cfg.example) as an SD-card configuration template.
