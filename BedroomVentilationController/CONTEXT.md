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
The scheduled operating period outside Night Mode, using the configured Day Mode CTS400 Settings.
_Avoid_: Daytime setting

**Night Mode**:
The scheduled operating period that applies the configured Night Mode CTS400 Settings.
_Avoid_: Night-time setting

**Winter Period**:
The configured recurring date range during which the Ventilation Controller applies the Day Mode RTS setting.
_Avoid_: Winter mode

**Bypass**:
The CTS400 airflow path controlled through the configured Day Mode and Night Mode RTS settings.
_Avoid_: Bypass damper

## Airflow

**Fan Level**:
One of the four configured percentage setpoints for a Supply Fan or Extract Fan in a given operating mode.
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
The user-defined schedule, Winter Period, Operating-Mode CTS400 Settings, Immediate CTS400 Settings, network mode, and optional Night-Temperature Reduction used by the Ventilation Controller.
_Avoid_: Settings file, controller settings

**Local Control Page**:
The trusted-local-network web page used to view controller status and CTS400 readings, and to update the Ventilation Configuration.
_Avoid_: Public dashboard

**CTS400 Report**:
The collection of live CTS400 values confirmed readable through controller evidence, displayed by the Local Control Page and included in controller notifications.
_Avoid_: Diagnostic log

**Validated CTS400 Setting**:
A CTS400 value with a confirmed write operation and a known acceptable value format and range.
_Avoid_: Writable value, editable parameter

**Operating-Mode CTS400 Setting**:
A configured value for a Validated CTS400 Setting in either Day Mode or Night Mode. The Ventilation Controller writes the stored value to the Nilan CTS400 Unit when that mode begins, including after startup; the Day Mode RTS value is applied during the Winter Period.
_Avoid_: Manual override, immediate CTS400 edit

**Immediate CTS400 Setting**:
A configured Validated CTS400 Setting that is not mode-specific. It is saved and written to the Nilan CTS400 Unit through its individual Apply control on the Local Control Page.
_Avoid_: Global CTS400 setting, manual override

**Read-Only CTS400 Field**:
A live CTS400 value displayed in the CTS400 Report that has no confirmed safe write operation or acceptable value range for this controller.
_Avoid_: Disabled setting, editable parameter
