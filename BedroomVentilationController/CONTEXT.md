# Bedroom Ventilation Control

This context defines the operational language of the controller that automates bedroom ventilation through a Nilan CTS400 unit. Use these terms consistently when describing its behavior, settings, and future changes.

## Ventilation operation

**Ventilation Controller**:
The autonomous device that applies the bedroom's ventilation policy to a connected Nilan CTS400 unit.
_Avoid_: App, web page

**Nilan CTS400 Unit**:
The ventilation unit controlled by the Ventilation Controller.
_Avoid_: Controller, device

**Ventilation Schedule**:
The configured division of each day into Day Mode and Night Mode.
_Avoid_: Timer, timetable

**Day Mode**:
The scheduled operating period outside Night Mode, using the configured day Supply Fan and Extract Fan levels.
_Avoid_: Daytime setting

**Night Mode**:
The scheduled operating period that applies the configured night fan levels and opens the Bypass.
_Avoid_: Night-time setting

**Winter Period**:
The configured recurring date range during which the Bypass is closed when the controller returns to Day Mode.
_Avoid_: Winter mode

**Bypass**:
The CTS400 airflow path that the controller opens for Night Mode and closes during the Winter Period.
_Avoid_: Bypass damper

## Airflow

**Fan Level**:
One of the three configured percentage setpoints for a Supply Fan or Extract Fan in a given operating mode.
_Avoid_: Fan speed, fan percentage

**Supply Fan**:
The fan that supplies air to the bedroom ventilation system.
_Avoid_: Inlet fan

**Extract Fan**:
The fan that removes air from the bedroom ventilation system.
_Avoid_: Exhaust fan

**Night-Temperature Reduction**:
An optional Night Mode adjustment that reduces every night Fan Level as outdoor temperature falls below a configured threshold.
_Avoid_: Cold-weather mode

## Configuration and observation

**Ventilation Configuration**:
The user-defined schedule, Winter Period, fan levels, network mode, and optional Night-Temperature Reduction used by the Ventilation Controller.
_Avoid_: Settings file, controller settings

**Local Control Page**:
The trusted-local-network web page used to view controller status and CTS400 readings, and to update the Ventilation Configuration.
_Avoid_: Public dashboard

**CTS400 Report**:
The set of supported live CTS400 readings displayed by the Local Control Page and included in controller notifications.
_Avoid_: Diagnostic log
