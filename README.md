# Logic for BedroomVentilationController


fix time zone fejl
send email ved fejl, start med også at sende emails ved success
sæt høj fugtighed tilbage til 3 men set 3 fan level ned 
sluk imellem tidspunkterne hvor der skal gøres noget


## Tested board configuration

- Board: Adafruit Metro ESP32-S3
- ESP32 Arduino core: 3.3.3
- USB Mode: USB-OTG (TinyUSB)
- USB CDC On Boot: Disabled
- Upload Mode: UART0 / Hardware CDC
- USBHostSerial: 0.2.0

The Nilan USB host connection was verified with ESP32 core 3.3.3. Newer core
versions may change USB host behavior and should be hardware-tested before use.

status values: [day, night, error_network, error_nilan]
status = day
startNightTime = 19:00
stopNightTime = 05:00
nightInletProcent = 55
nightExhaustProcent = 60
winterModeStartDate = 01/09 
winterModeStopDate = 01/05
fanLevel1DayInletProcent = 20
fanLevel1DayExhaustProcent = 25
fanLevel2DayInletProcent = 35
fanLevel2DayExhaustProcent = 40
fanLevel3DayInletProcent = 50
fanLevel3DayExhaustProcent = 55


setup:
  connect to wifi
    if error set status to error_network
  set time
    if error set status to error_network
  close wifi
  if no error then run StopNight

loop:
  if status is day then check if it is time to start night and if it is run StartNight
  if status is night then check if it is time to stop night and if it is run StopNight
  if status is error_network then blink 2 times
  if status is error_nilan then blink 4 times
  enter light sleep for 15 minutes

StartNight:
  connect to nilan
  set "Wanted indoor temperature" to 15
  set inlet procent for Fan level 1, Fan level 2 and Fan level 3 to nightInletProcent 
  set exhaust procent for Fan level 1, Fan level 2 and Fan level 3 to nightExhaustProcent 
  if possible set all at the same time 
  if any errors then set status to error_nilan else set status to night

StopNight:
  connect to nilan
  if current date is in wintermode periode set "Wanted indoor temperature" to 25
  set inlet procent and exhaust procent for Fan level 1, Fan level 2 and Fan level 3 to the day levels
  if possible set all at the same time 
  if any errors then set status to error_nilan else set status to day
