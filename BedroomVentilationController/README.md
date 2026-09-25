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

## Deploying

### 1. Prepare the Arduino environment

Install Arduino IDE 2 and install the **esp32 by Espressif Systems** board package. Select the ESP32-S3 board that matches the controller hardware (the generic starting point is **ESP32S3 Dev Module**).

Install these libraries:

- [`USBHostSerial`](https://github.com/bertmelis/USBHostSerial): download the repository ZIP, then select **Sketch → Include Library → Add .ZIP Library…** in Arduino IDE.
- `EMailSender`: install it through Arduino IDE's Library Manager.

The controller uses the ESP32-S3 USB-OTG interface to communicate with the Nilan USB serial adapter. Set **Tools → USB CDC On Boot** to **Disabled**; the USB host cannot start when it is enabled.

### 2. Add local credentials

Create `configs.h` alongside `BedroomVentilationController.ino`. This file is deliberately excluded from Git. Define the Wi-Fi and email values used by the sketch:

- `SSID` and `PASSWORD`
- `EMAIL_SENDER_ADDRESS`, `EMAIL_SMTP_PASSWORD`, and `EMAIL_SENDER_NAME`
- `EMAIL_SMTP_HOST` and `EMAIL_SMTP_PORT`
- `EMAIL_RECIPIENT_ADDRESS`

Keep this file private because it contains network and email credentials.

### 3. Prepare the SD card

Copy [`ventilation.cfg.example`](ventilation.cfg.example) to the root of a FAT-formatted SD card and rename it to `ventilation.cfg`. Edit the schedule and fan levels before inserting the card.

The controller uses DHCP by default. To use a fixed address, uncomment and set all four `static_*` settings. Choose an unused IP address on the local network, and restart the controller after changing network settings.

### 4. Connect and upload

Connect the SD card using the SPI wiring expected by the ESP32 board, and connect the Nilan USB serial device to the ESP32-S3 USB-OTG host port. Select the board and upload port in Arduino IDE, then upload `BedroomVentilationController.ino`.

On first boot, the controller reads the SD configuration, joins Wi-Fi, synchronizes its clock, and starts the web server. A missing or invalid `ventilation.cfg` causes a one-blink error pattern and the controller retries loading it once per minute.

### 5. Open the web page

Find the controller's IP address in the router's DHCP-client list, then open `http://<controller-ip>/`. The page displays current CTS400 readings and settings, and can update the SD-card configuration directly.

The web page has no authentication. Keep the controller on a trusted local network and do not expose it directly to the internet.
