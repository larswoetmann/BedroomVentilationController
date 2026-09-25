#include <WiFi.h>
#include <USBHostSerial.h>
#include <FS.h>
#include <SD.h>
#include <WebServer.h>
#include <math.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "configs.h"

#include <EMailSender.h>

const char* DENMARK_TIME_ZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";

constexpr uint16_t NILAN_VID = 0x0483;
constexpr uint16_t NILAN_PID = 0x5740;
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long USB_TIMEOUT_MS = 10000;
constexpr unsigned long USB_SETTLE_MS = 2000;
constexpr unsigned long REPLY_TIMEOUT_MS = 3000;
constexpr unsigned long COMMAND_QUIET_MS = 100;
constexpr unsigned long DELAY_MS = 1UL * 60UL * 1000UL;
constexpr unsigned long NIGHT_TEMPERATURE_CHECK_MS = 15UL * 60UL * 1000UL;
constexpr float NILAN_TEMPERATURE_DIVISOR = 10.0f;
constexpr char CONFIG_FILE_PATH[] = "/ventilation.cfg";
constexpr char CONFIG_TEMP_FILE_PATH[] = "/ventilation.tmp";
constexpr char CONFIG_BACKUP_FILE_PATH[] = "/ventilation.bak";

USBHostSerial nilan(NILAN_VID, NILAN_PID);
bool usbHostStarted = false;
WebServer webServer(80);
bool webServerStarted = false;
unsigned long lastControlCycleAt = 0;
unsigned long lastNightTemperatureCheckAt = 0;
int activeNightVentilationReduction = -1;

enum class ControllerStatus {
  NotSet,
  Day,
  Night,
  ErrorConfiguration,
  ErrorNetwork,
  ErrorNilan,
  ErrorEmail
};

ControllerStatus status = ControllerStatus::NotSet;
tm currentTime;

struct ControllerConfiguration {
  int nightStartMinute;
  int nightEndMinute;
  int winterStartDate;
  int winterEndDate;
  int dayInletPercentages[3];
  int dayExhaustPercentages[3];
  int nightInletPercentages[3];
  int nightExhaustPercentages[3];
  bool useStaticIp;
  IPAddress staticIp;
  IPAddress staticGateway;
  IPAddress staticSubnet;
  IPAddress staticDns;
  bool useNightTemperatureReduction;
  float nightTemperatureThresholdC;
  float nightVentilationReductionPerDegree;
};

ControllerConfiguration configuration;

EMailSender emailSender(EMAIL_SENDER_ADDRESS, EMAIL_SMTP_PASSWORD,
                        EMAIL_SENDER_ADDRESS, EMAIL_SENDER_NAME,
                        EMAIL_SMTP_HOST, EMAIL_SMTP_PORT);

void blinkError(int count);
void handleHomepage();
void handleSettingsUpdate();

String formatTime(int minuteOfDay) {
  char formatted[6];
  snprintf(formatted, sizeof(formatted), "%02d:%02d", minuteOfDay / 60,
           minuteOfDay % 60);
  return String(formatted);
}

String formatDate(int date) {
  char formatted[6];
  snprintf(formatted, sizeof(formatted), "%02d-%02d", date / 32, date % 32);
  return String(formatted);
}

String formatFanLevels(const int levels[]) {
  char formatted[17];
  snprintf(formatted, sizeof(formatted), "%d%%, %d%%, %d%%", levels[0],
           levels[1], levels[2]);
  return String(formatted);
}

const char* statusName(ControllerStatus controllerStatus) {
  switch (controllerStatus) {
    case ControllerStatus::NotSet: return "Waiting to configure";
    case ControllerStatus::Day: return "Day";
    case ControllerStatus::Night: return "Night";
    case ControllerStatus::ErrorConfiguration: return "Configuration error";
    case ControllerStatus::ErrorNetwork: return "Network error";
    case ControllerStatus::ErrorNilan: return "Nilan communication error";
    case ControllerStatus::ErrorEmail: return "Email error";
  }
  return "Unknown";
}

String escapeHtml(const String& text) {
  String escaped;
  escaped.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    switch (text[i]) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      default: escaped += text[i]; break;
    }
  }
  return escaped;
}

bool parseInteger(const String& text, int minimum, int maximum, int& value) {
  if (text.isEmpty()) {
    return false;
  }

  char* end;
  const long parsed = strtol(text.c_str(), &end, 10);
  if (*end != '\0' || parsed < minimum || parsed > maximum) {
    return false;
  }

  value = static_cast<int>(parsed);
  return true;
}

bool parseTime(const String& text, int& minuteOfDay) {
  if (text.length() != 5 || text[2] != ':' ||
      !isDigit(text[0]) || !isDigit(text[1]) ||
      !isDigit(text[3]) || !isDigit(text[4])) {
    return false;
  }

  const int hour = (text[0] - '0') * 10 + (text[1] - '0');
  const int minute = (text[3] - '0') * 10 + (text[4] - '0');
  if (hour > 23 || minute > 59) {
    return false;
  }

  minuteOfDay = hour * 60 + minute;
  return true;
}

bool parseDate(const String& text, int& date) {
  if (text.length() != 5 || text[2] != '-' ||
      !isDigit(text[0]) || !isDigit(text[1]) ||
      !isDigit(text[3]) || !isDigit(text[4])) {
    return false;
  }

  const int month = (text[0] - '0') * 10 + (text[1] - '0');
  const int day = (text[3] - '0') * 10 + (text[4] - '0');
  const int daysPerMonth[] = {0, 31, 29, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12 || day < 1 || day > daysPerMonth[month]) {
    return false;
  }

  date = month * 32 + day;
  return true;
}

bool parseFanLevels(const String& text, int levels[]) {
  int separator1 = text.indexOf(',');
  int separator2 = text.indexOf(',', separator1 + 1);
  if (separator1 <= 0 || separator2 <= separator1 + 1 ||
      text.indexOf(',', separator2 + 1) != -1) {
    return false;
  }

  String levelText[] = {
      text.substring(0, separator1),
      text.substring(separator1 + 1, separator2),
      text.substring(separator2 + 1)};
  for (size_t i = 0; i < 3; ++i) {
    levelText[i].trim();
    if (!parseInteger(levelText[i], 0, 100, levels[i])) {
      return false;
    }
  }
  return true;
}

bool parseIpAddress(const String& text, IPAddress& address) {
  return address.fromString(text.c_str());
}

bool parseDecimal(const String& text, float minimum, float maximum,
                  float& value) {
  if (text.isEmpty()) {
    return false;
  }
  char* end;
  const float parsed = strtof(text.c_str(), &end);
  if (*end != '\0' || !isfinite(parsed) || parsed < minimum ||
      parsed > maximum) {
    return false;
  }
  value = parsed;
  return true;
}

bool loadConfiguration() {
  if (!SD.begin()) {
    return false;
  }

  File file = SD.open(CONFIG_FILE_PATH, FILE_READ);
  if (!file) {
    return false;
  }

  ControllerConfiguration loaded = {};
  bool foundNightStart = false;
  bool foundNightEnd = false;
  bool foundWinterStart = false;
  bool foundWinterEnd = false;
  bool foundDayInlet = false;
  bool foundDayExhaust = false;
  bool foundNightInlet = false;
  bool foundNightExhaust = false;
  bool foundStaticIp = false;
  bool foundStaticGateway = false;
  bool foundStaticSubnet = false;
  bool foundStaticDns = false;
  bool foundNightTemperatureThreshold = false;
  bool foundNightReductionPerDegree = false;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    const int commentStart = line.indexOf('#');
    if (commentStart >= 0) {
      line.remove(commentStart);
    }
    line.trim();
    if (line.isEmpty()) {
      continue;
    }

    const int separator = line.indexOf('=');
    if (separator <= 0 || line.indexOf('=', separator + 1) != -1) {
      file.close();
      return false;
    }

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();

    bool parsed = false;
    if (key == "night_start_time" && !foundNightStart) {
      parsed = parseTime(value, loaded.nightStartMinute);
      foundNightStart = parsed;
    } else if (key == "night_end_time" && !foundNightEnd) {
      parsed = parseTime(value, loaded.nightEndMinute);
      foundNightEnd = parsed;
    } else if (key == "winter_start_date" && !foundWinterStart) {
      parsed = parseDate(value, loaded.winterStartDate);
      foundWinterStart = parsed;
    } else if (key == "winter_end_date" && !foundWinterEnd) {
      parsed = parseDate(value, loaded.winterEndDate);
      foundWinterEnd = parsed;
    } else if (key == "day_inlet_levels" && !foundDayInlet) {
      parsed = parseFanLevels(value, loaded.dayInletPercentages);
      foundDayInlet = parsed;
    } else if (key == "day_exhaust_levels" && !foundDayExhaust) {
      parsed = parseFanLevels(value, loaded.dayExhaustPercentages);
      foundDayExhaust = parsed;
    } else if (key == "night_inlet_levels" && !foundNightInlet) {
      parsed = parseFanLevels(value, loaded.nightInletPercentages);
      foundNightInlet = parsed;
    } else if (key == "night_exhaust_levels" && !foundNightExhaust) {
      parsed = parseFanLevels(value, loaded.nightExhaustPercentages);
      foundNightExhaust = parsed;
    } else if (key == "static_ip" && !foundStaticIp) {
      parsed = parseIpAddress(value, loaded.staticIp);
      foundStaticIp = parsed;
    } else if (key == "static_gateway" && !foundStaticGateway) {
      parsed = parseIpAddress(value, loaded.staticGateway);
      foundStaticGateway = parsed;
    } else if (key == "static_subnet" && !foundStaticSubnet) {
      parsed = parseIpAddress(value, loaded.staticSubnet);
      foundStaticSubnet = parsed;
    } else if (key == "static_dns" && !foundStaticDns) {
      parsed = parseIpAddress(value, loaded.staticDns);
      foundStaticDns = parsed;
    } else if (key == "night_temperature_threshold_c" &&
               !foundNightTemperatureThreshold) {
      parsed = parseDecimal(value, -40.0f, 50.0f,
                            loaded.nightTemperatureThresholdC);
      foundNightTemperatureThreshold = parsed;
    } else if (key == "night_ventilation_reduction_per_degree" &&
               !foundNightReductionPerDegree) {
      parsed = parseDecimal(value, 0.0f, 100.0f,
                            loaded.nightVentilationReductionPerDegree);
      foundNightReductionPerDegree = parsed;
    }

    if (!parsed) {
      file.close();
      return false;
    }
  }
  file.close();

  if (!foundNightStart || !foundNightEnd || !foundWinterStart ||
      !foundWinterEnd || !foundDayInlet || !foundDayExhaust ||
      !foundNightInlet || !foundNightExhaust ||
      loaded.nightStartMinute == loaded.nightEndMinute ||
      loaded.winterStartDate == loaded.winterEndDate ||
      (foundStaticIp || foundStaticGateway || foundStaticSubnet || foundStaticDns) &&
          !(foundStaticIp && foundStaticGateway && foundStaticSubnet && foundStaticDns) ||
      (foundNightTemperatureThreshold || foundNightReductionPerDegree) &&
          !(foundNightTemperatureThreshold && foundNightReductionPerDegree)) {
    return false;
  }

  loaded.useStaticIp = foundStaticIp;
  loaded.useNightTemperatureReduction = foundNightTemperatureThreshold;
  configuration = loaded;
  return true;
}

bool parseFormConfiguration(ControllerConfiguration& updated) {
  const char* requiredFields[] = {
      "night_start_time", "night_end_time", "winter_start_date",
      "winter_end_date", "day_inlet_1", "day_inlet_2", "day_inlet_3",
      "day_exhaust_1", "day_exhaust_2", "day_exhaust_3", "night_inlet_1",
      "night_inlet_2", "night_inlet_3", "night_exhaust_1",
      "night_exhaust_2", "night_exhaust_3"};
  for (const char* field : requiredFields) {
    if (!webServer.hasArg(field)) {
      return false;
    }
  }

  if (!parseTime(webServer.arg("night_start_time"), updated.nightStartMinute) ||
      !parseTime(webServer.arg("night_end_time"), updated.nightEndMinute) ||
      !parseDate(webServer.arg("winter_start_date"), updated.winterStartDate) ||
      !parseDate(webServer.arg("winter_end_date"), updated.winterEndDate) ||
      updated.nightStartMinute == updated.nightEndMinute ||
      updated.winterStartDate == updated.winterEndDate) {
    return false;
  }

  int* levelGroups[] = {updated.dayInletPercentages,
                        updated.dayExhaustPercentages,
                        updated.nightInletPercentages,
                        updated.nightExhaustPercentages};
  const char* fieldPrefixes[] = {"day_inlet_", "day_exhaust_",
                                 "night_inlet_", "night_exhaust_"};
  for (size_t group = 0; group < 4; ++group) {
    for (size_t level = 0; level < 3; ++level) {
      const String fieldName = String(fieldPrefixes[group]) + String(level + 1);
      if (!parseInteger(webServer.arg(fieldName), 0, 100,
                        levelGroups[group][level])) {
        return false;
      }
    }
  }

  updated.useStaticIp = webServer.hasArg("use_static_ip");
  if (updated.useStaticIp &&
      (!webServer.hasArg("static_ip") || !webServer.hasArg("static_gateway") ||
       !webServer.hasArg("static_subnet") || !webServer.hasArg("static_dns") ||
       !parseIpAddress(webServer.arg("static_ip"), updated.staticIp) ||
       !parseIpAddress(webServer.arg("static_gateway"), updated.staticGateway) ||
       !parseIpAddress(webServer.arg("static_subnet"), updated.staticSubnet) ||
       !parseIpAddress(webServer.arg("static_dns"), updated.staticDns))) {
    return false;
  }

  updated.useNightTemperatureReduction =
      webServer.hasArg("use_night_temperature_reduction");
  if (updated.useNightTemperatureReduction &&
      (!webServer.hasArg("night_temperature_threshold_c") ||
       !webServer.hasArg("night_ventilation_reduction_per_degree") ||
       !parseDecimal(webServer.arg("night_temperature_threshold_c"), -40.0f,
                     50.0f, updated.nightTemperatureThresholdC) ||
       !parseDecimal(webServer.arg("night_ventilation_reduction_per_degree"),
                     0.0f, 100.0f,
                     updated.nightVentilationReductionPerDegree))) {
    return false;
  }
  return true;
}

bool saveConfiguration(const ControllerConfiguration& updated) {
  SD.remove(CONFIG_TEMP_FILE_PATH);
  File file = SD.open(CONFIG_TEMP_FILE_PATH, FILE_WRITE);
  if (!file) {
    return false;
  }

  const String nightStart = formatTime(updated.nightStartMinute);
  const String nightEnd = formatTime(updated.nightEndMinute);
  const String winterStart = formatDate(updated.winterStartDate);
  const String winterEnd = formatDate(updated.winterEndDate);
  const bool wroteAll =
      file.printf("night_start_time = %s\n", nightStart.c_str()) > 0 &&
      file.printf("night_end_time = %s\n", nightEnd.c_str()) > 0 &&
      file.printf("winter_start_date = %s\n", winterStart.c_str()) > 0 &&
      file.printf("winter_end_date = %s\n", winterEnd.c_str()) > 0 &&
      file.printf("day_inlet_levels = %d, %d, %d\n",
                  updated.dayInletPercentages[0],
                  updated.dayInletPercentages[1],
                  updated.dayInletPercentages[2]) > 0 &&
      file.printf("day_exhaust_levels = %d, %d, %d\n",
                  updated.dayExhaustPercentages[0],
                  updated.dayExhaustPercentages[1],
                  updated.dayExhaustPercentages[2]) > 0 &&
      file.printf("night_inlet_levels = %d, %d, %d\n",
                  updated.nightInletPercentages[0],
                  updated.nightInletPercentages[1],
                  updated.nightInletPercentages[2]) > 0 &&
      file.printf("night_exhaust_levels = %d, %d, %d\n",
                  updated.nightExhaustPercentages[0],
                  updated.nightExhaustPercentages[1],
                  updated.nightExhaustPercentages[2]) > 0 &&
      (!updated.useStaticIp ||
       (file.printf("static_ip = %s\n", updated.staticIp.toString().c_str()) > 0 &&
        file.printf("static_gateway = %s\n", updated.staticGateway.toString().c_str()) > 0 &&
        file.printf("static_subnet = %s\n", updated.staticSubnet.toString().c_str()) > 0 &&
        file.printf("static_dns = %s\n", updated.staticDns.toString().c_str()) > 0)) &&
      (!updated.useNightTemperatureReduction ||
       (file.printf("night_temperature_threshold_c = %.2f\n",
                    updated.nightTemperatureThresholdC) > 0 &&
        file.printf("night_ventilation_reduction_per_degree = %.2f\n",
                    updated.nightVentilationReductionPerDegree) > 0));
  file.close();
  if (!wroteAll) {
    SD.remove(CONFIG_TEMP_FILE_PATH);
    return false;
  }

  SD.remove(CONFIG_BACKUP_FILE_PATH);
  if (SD.exists(CONFIG_FILE_PATH) &&
      !SD.rename(CONFIG_FILE_PATH, CONFIG_BACKUP_FILE_PATH)) {
    SD.remove(CONFIG_TEMP_FILE_PATH);
    return false;
  }
  if (!SD.rename(CONFIG_TEMP_FILE_PATH, CONFIG_FILE_PATH)) {
    if (SD.exists(CONFIG_BACKUP_FILE_PATH)) {
      SD.rename(CONFIG_BACKUP_FILE_PATH, CONFIG_FILE_PATH);
    }
    return false;
  }
  SD.remove(CONFIG_BACKUP_FILE_PATH);
  return true;
}

void clearNilanInput() {
  while (nilan.available()) {
    nilan.read();
  }
}

bool readNilanLine(String& line) {
  line = "";
  unsigned long startedAt = millis();

  while (millis() - startedAt < REPLY_TIMEOUT_MS) {
    while (nilan.available()) {
      char c = static_cast<char>(nilan.read());
      if (c == '\r' || c == '\n') {
        if (!line.isEmpty()) {
          return true;
        }
      } else if (line.length() < 32) {
        line += c;
      }
    }
    delay(1);
  }

  return false;
}

bool sendNilanCommand(const String& command, String& reply) {
  clearNilanInput();
  if (nilan.write(reinterpret_cast<const uint8_t*>(command.c_str()),
                  command.length()) != command.length()) {
    blinkError(4);
    return false;
  }
  if (!readNilanLine(reply)) {
    blinkError(5);
    return false;
  }
  delay(COMMAND_QUIET_MS);
  return true;
}

bool startUsbHost() {
#if ARDUINO_USB_CDC_ON_BOOT
  blinkError(1);
  return false;
#else
  if (!usbHostStarted) {
    usbHostStarted = nilan.begin(9600, 0, 0, 8);
    if (!usbHostStarted) {
    blinkError(2);
    return false;
  }
  }
  return usbHostStarted;
#endif
}

bool connectToNilan() {
  if (!startUsbHost()) {
    return false;
  }

  bool newlyConnected = !static_cast<bool>(nilan);
  unsigned long startedAt = millis();
  while (!static_cast<bool>(nilan)) {
    if (millis() - startedAt >= USB_TIMEOUT_MS) {
      blinkError(3);
      return false;
    }
    delay(10);
  }
  if (newlyConnected) {
    delay(USB_SETTLE_MS);
  }

  String reply;
  return sendNilanCommand("S SVC +00000\r", reply) && reply == "OK";
}

bool setNilanParameter(const char* parameter, int value) {
  char formattedValue[7];
  snprintf(formattedValue, sizeof(formattedValue), "%+06d", value);

  String command = "S ";
  command += parameter;
  command += ' ';
  command += formattedValue;
  command += '\r';

  String reply;
  return sendNilanCommand(command, reply) && reply == "OK";
}

bool getNilanParameter(const char* parameter, String& value) {
  String command = "G ";
  command += parameter;
  command += '\r';
  return sendNilanCommand(command, value);
}

struct NilanReportField {
  const char* code;
  const char* name;
};

const NilanReportField NILAN_REPORT_FIELDS[] = {
    {"ALR", "Alarm status"},
    {"MBV", "Mainboard software version"},
    {"FL0", "Selected fan level"},
    {"F1I", "Supply fan level 1 (%)"},
    {"F2I", "Supply fan level 2 (%)"},
    {"F3I", "Supply fan level 3 (%)"},
    {"F4I", "Supply fan level 4 (%)"},
    {"F1O", "Extract fan level 1 (%)"},
    {"F2O", "Extract fan level 2 (%)"},
    {"F3O", "Extract fan level 3 (%)"},
    {"F4O", "Extract fan level 4 (%)"},
    {"SWT", "Summer/winter threshold"},
    {"SWD", "Summer/winter setting"},
    {"DST", "De-icing start setting"},
    {"DTI", "De-icing interval"},
    {"DSP", "De-icing stop setting"},
    {"DMT", "Maximum de-icing time"},
    {"DDT", "De-icing duration setting"},
    {"RTS", "Wanted room temperature"},
    {"FCP", "Filter-change period"},
    {"DBS", "Regulation dead band"},
    {"LOT", "Low outdoor-temperature setting"},
    {"LHL", "Low-humidity fan level"},
    {"HHL", "High-humidity fan level"},
    {"GFI", "Current supply fan level"},
    {"GFO", "Current extract fan level"},
    {"GT3", "T3 extract-air temperature"},
    {"GT4", "T4 outlet-air temperature"},
    {"GT8", "T8 outdoor-air temperature"},
    {"GBS", "Bypass damper status"},
    {"GRH", "Current relative humidity"},
};

String buildNilanReport() {
  String report;
  report.reserve(3500);
  report += "<h2>CTS400 report</h2>";
  report += "<table border=\"1\" cellpadding=\"4\" cellspacing=\"0\">";
  report += "<tr><th>Value</th><th>Code</th><th>Raw reading</th></tr>";

  for (const NilanReportField& field : NILAN_REPORT_FIELDS) {
    String value;
    const bool readOk = getNilanParameter(field.code, value);
    report += "<tr><td>";
    report += field.name;
    report += "</td><td>";
    report += field.code;
    report += "</td><td>";
    report += readOk ? escapeHtml(value) : "unavailable";
    report += "</td></tr>";
  }

  report += "</table>";
  return report;
}

void appendSettingsRow(String& page, const char* name, const String& value) {
  page += "<tr><th>";
  page += name;
  page += "</th><td>";
  page += value;
  page += "</td></tr>";
}

void appendLevelInput(String& page, const char* fieldName, const char* label,
                      int value) {
  page += "<label>";
  page += label;
  page += " <input type=\"number\" name=\"";
  page += fieldName;
  page += "\" min=\"0\" max=\"100\" required value=\"";
  page += String(value);
  page += "\"> %</label>";
}

void appendTextInput(String& page, const char* fieldName, const char* label,
                     const String& value) {
  page += "<label>";
  page += label;
  page += " <input name=\"";
  page += fieldName;
  page += "\" value=\"";
  page += value;
  page += "\"></label>";
}

void appendSettings(String& page) {
  page += "<h2>Controller settings</h2>";
  page += "<p>Loaded from <code>/ventilation.cfg</code> on the SD card.</p>";
  page += "<table><tr><th>Setting</th><th>Value</th></tr>";
  appendSettingsRow(page, "Night start", formatTime(configuration.nightStartMinute));
  appendSettingsRow(page, "Night end", formatTime(configuration.nightEndMinute));
  appendSettingsRow(page, "Winter start", formatDate(configuration.winterStartDate));
  appendSettingsRow(page, "Winter end (exclusive)",
                    formatDate(configuration.winterEndDate));
  appendSettingsRow(page, "Day supply levels (1, 2, 3)",
                    formatFanLevels(configuration.dayInletPercentages));
  appendSettingsRow(page, "Day extract levels (1, 2, 3)",
                    formatFanLevels(configuration.dayExhaustPercentages));
  appendSettingsRow(page, "Night supply levels (1, 2, 3)",
                    formatFanLevels(configuration.nightInletPercentages));
  appendSettingsRow(page, "Night extract levels (1, 2, 3)",
                    formatFanLevels(configuration.nightExhaustPercentages));
  appendSettingsRow(page, "Network address mode",
                    configuration.useStaticIp ? "Static" : "DHCP");
  if (configuration.useStaticIp) {
    appendSettingsRow(page, "Static IP", configuration.staticIp.toString());
    appendSettingsRow(page, "Gateway", configuration.staticGateway.toString());
    appendSettingsRow(page, "Subnet mask", configuration.staticSubnet.toString());
    appendSettingsRow(page, "DNS server", configuration.staticDns.toString());
  }
  appendSettingsRow(page, "Night temperature reduction",
                    configuration.useNightTemperatureReduction ? "Enabled" : "Disabled");
  if (configuration.useNightTemperatureReduction) {
    appendSettingsRow(page, "Reduction threshold",
                      String(configuration.nightTemperatureThresholdC, 2) + " °C");
    appendSettingsRow(page, "Reduction per degree below threshold",
                      String(configuration.nightVentilationReductionPerDegree, 2) + " percentage points");
  }
  page += "</table>";

  page += "<h2>Update settings</h2><form method=\"post\" action=\"/settings\">";
  page += "<fieldset><legend>Schedule</legend>";
  page += "<label>Night start <input type=\"time\" name=\"night_start_time\" required value=\"";
  page += formatTime(configuration.nightStartMinute);
  page += "\"></label><label>Night end <input type=\"time\" name=\"night_end_time\" required value=\"";
  page += formatTime(configuration.nightEndMinute);
  page += "\"></label><label>Winter start (MM-DD) <input name=\"winter_start_date\" pattern=\"[0-9]{2}-[0-9]{2}\" required value=\"";
  page += formatDate(configuration.winterStartDate);
  page += "\"></label><label>Winter end (MM-DD) <input name=\"winter_end_date\" pattern=\"[0-9]{2}-[0-9]{2}\" required value=\"";
  page += formatDate(configuration.winterEndDate);
  page += "\"></label></fieldset>";

  page += "<fieldset><legend>Day fan levels</legend>";
  appendLevelInput(page, "day_inlet_1", "Supply 1", configuration.dayInletPercentages[0]);
  appendLevelInput(page, "day_inlet_2", "Supply 2", configuration.dayInletPercentages[1]);
  appendLevelInput(page, "day_inlet_3", "Supply 3", configuration.dayInletPercentages[2]);
  appendLevelInput(page, "day_exhaust_1", "Extract 1", configuration.dayExhaustPercentages[0]);
  appendLevelInput(page, "day_exhaust_2", "Extract 2", configuration.dayExhaustPercentages[1]);
  appendLevelInput(page, "day_exhaust_3", "Extract 3", configuration.dayExhaustPercentages[2]);
  page += "</fieldset><fieldset><legend>Night fan levels</legend>";
  appendLevelInput(page, "night_inlet_1", "Supply 1", configuration.nightInletPercentages[0]);
  appendLevelInput(page, "night_inlet_2", "Supply 2", configuration.nightInletPercentages[1]);
  appendLevelInput(page, "night_inlet_3", "Supply 3", configuration.nightInletPercentages[2]);
  appendLevelInput(page, "night_exhaust_1", "Extract 1", configuration.nightExhaustPercentages[0]);
  appendLevelInput(page, "night_exhaust_2", "Extract 2", configuration.nightExhaustPercentages[1]);
  appendLevelInput(page, "night_exhaust_3", "Extract 3", configuration.nightExhaustPercentages[2]);
  page += "</fieldset><fieldset><legend>Night outdoor-temperature reduction</legend>";
  page += "<label><input type=\"checkbox\" name=\"use_night_temperature_reduction\"";
  if (configuration.useNightTemperatureReduction) {
    page += " checked";
  }
  page += "> Reduce night fan levels when outdoor air is colder</label>";
  page += "<label>Maximum outdoor temperature (°C) <input type=\"number\" name=\"night_temperature_threshold_c\" min=\"-40\" max=\"50\" step=\"0.1\" value=\"";
  page += configuration.useNightTemperatureReduction ?
              String(configuration.nightTemperatureThresholdC, 2) : "";
  page += "\"></label><label>Reduction per degree (percentage points) <input type=\"number\" name=\"night_ventilation_reduction_per_degree\" min=\"0\" max=\"100\" step=\"0.1\" value=\"";
  page += configuration.useNightTemperatureReduction ?
              String(configuration.nightVentilationReductionPerDegree, 2) : "";
  page += "\"></label><p>For example, 10 °C and 2.5 reduces each night fan level by 5 percentage points at 8 °C.</p></fieldset>";
  page += "<fieldset><legend>Network</legend>";
  page += "<label><input type=\"checkbox\" name=\"use_static_ip\"";
  if (configuration.useStaticIp) {
    page += " checked";
  }
  page += "> Use a static IP address</label>";
  appendTextInput(page, "static_ip", "IP address",
                  configuration.useStaticIp ? configuration.staticIp.toString() : "");
  appendTextInput(page, "static_gateway", "Gateway",
                  configuration.useStaticIp ? configuration.staticGateway.toString() : "");
  appendTextInput(page, "static_subnet", "Subnet mask",
                  configuration.useStaticIp ? configuration.staticSubnet.toString() : "");
  appendTextInput(page, "static_dns", "DNS server",
                  configuration.useStaticIp ? configuration.staticDns.toString() : "");
  page += "<p>Restart the controller after changing network settings.</p></fieldset>";
  page += "<button type=\"submit\">Save settings</button></form>";
}

void handleHomepage() {
  String page;
  page.reserve(8000);
  page += "<!doctype html><html lang=\"en\"><head>";
  page += "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  page += "<meta http-equiv=\"refresh\" content=\"60\"><title>Bedroom ventilation</title>";
  page += "<style>body{font-family:system-ui,sans-serif;max-width:900px;margin:2rem auto;padding:0 1rem;color:#17211b}"
          "table{border-collapse:collapse;width:100%;margin:1rem 0 2rem}th,td{border:1px solid #ccd5cf;padding:.55rem;text-align:left}"
          "th{background:#edf4ef}h1{margin-bottom:.2rem}.status{font-weight:700;color:#0a6b32}code{background:#eef1ef;padding:.1rem .25rem}</style>";
  page += "</head><body><h1>Bedroom ventilation</h1><p class=\"status\">Controller status: ";
  page += statusName(status);
  page += "</p>";

  appendSettings(page);
  page += "<h2>Nilan CTS400 output</h2>";
  page += "<p>This page reads the supported CTS400 fields when opened. It can take a few seconds.</p>";
  if (connectToNilan()) {
    page += buildNilanReport();
  } else {
    page += "<p>Unable to connect to the Nilan unit.</p>";
  }
  page += "</body></html>";
  webServer.send(200, "text/html; charset=utf-8", page);
}

void handleSettingsUpdate() {
  ControllerConfiguration updated = {};
  if (!parseFormConfiguration(updated)) {
    webServer.send(400, "text/plain",
                   "Invalid settings. Check the time, date, and fan-level values.");
    return;
  }
  if (!saveConfiguration(updated)) {
    webServer.send(500, "text/plain",
                   "Could not save ventilation.cfg to the SD card.");
    return;
  }

  configuration = updated;
  status = ControllerStatus::NotSet;
  lastControlCycleAt = 0;
  webServer.sendHeader("Location", "/");
  webServer.send(303);
}

bool setFanPercentages(const int inletPercentages[],
                       const int exhaustPercentages[]) {
  const char* inletParameters[] = {"F1I", "F2I", "F3I"};
  const char* exhaustParameters[] = {"F1O", "F2O", "F3O"};

  for (size_t i = 0; i < 3; ++i) {
    if (!setNilanParameter(inletParameters[i], inletPercentages[i]) ||
        !setNilanParameter(exhaustParameters[i], exhaustPercentages[i])) {
      blinkError(7+i);
      return false;
    }
  }
  return true;
}

bool getOutdoorTemperature(float& temperatureC) {
  String rawTemperature;
  if (!getNilanParameter("GT8", rawTemperature)) {
    return false;
  }

  char* end;
  const float rawValue = strtof(rawTemperature.c_str(), &end);
  if (*end != '\0' || !isfinite(rawValue)) {
    return false;
  }
  temperatureC = rawValue / NILAN_TEMPERATURE_DIVISOR;
  return true;
}

bool applyNightVentilationForTemperature() {
  int inletPercentages[3];
  int exhaustPercentages[3];
  int reduction = 0;

  if (configuration.useNightTemperatureReduction) {
    float outdoorTemperatureC;
    if (!getOutdoorTemperature(outdoorTemperatureC)) {
      return false;
    }
    const float degreesBelowThreshold =
        configuration.nightTemperatureThresholdC - outdoorTemperatureC;
    if (degreesBelowThreshold > 0.0f) {
      reduction = static_cast<int>(roundf(
          degreesBelowThreshold * configuration.nightVentilationReductionPerDegree));
    }
  }

  if (reduction == activeNightVentilationReduction) {
    return true;
  }
  for (size_t i = 0; i < 3; ++i) {
    inletPercentages[i] = max(0, configuration.nightInletPercentages[i] - reduction);
    exhaustPercentages[i] = max(0, configuration.nightExhaustPercentages[i] - reduction);
  }
  if (!setFanPercentages(inletPercentages, exhaustPercentages)) {
    return false;
  }

  activeNightVentilationReduction = reduction;
  return true;
}

bool isWinterMode(const tm& currentTime) {
  const int currentDate = (currentTime.tm_mon + 1) * 32 + currentTime.tm_mday;
  if (configuration.winterStartDate < configuration.winterEndDate) {
    return currentDate >= configuration.winterStartDate &&
           currentDate < configuration.winterEndDate;
  }
  return currentDate >= configuration.winterStartDate ||
         currentDate < configuration.winterEndDate;
}

bool isNightTime(const tm& currentTime) {
  int minuteOfDay = currentTime.tm_hour * 60 + currentTime.tm_min;
  if (configuration.nightStartMinute < configuration.nightEndMinute) {
    return minuteOfDay >= configuration.nightStartMinute &&
           minuteOfDay < configuration.nightEndMinute;
  }
  return minuteOfDay >= configuration.nightStartMinute ||
         minuteOfDay < configuration.nightEndMinute;
}

bool openBypass() {
  return setNilanParameter("RTS", 15);
}

bool closeBypassIfWinter(const tm& currentTime) {
  if(isWinterMode(currentTime)) {
      return setNilanParameter("RTS", 28);
  }
  return true;
}

void startNight() {
  activeNightVentilationReduction = -1;
  if (!connectToNilan() ||
      !openBypass() ||
      !applyNightVentilationForTemperature()) {
    status = ControllerStatus::ErrorNilan;
    return;
  }

  status = ControllerStatus::Night;
  lastNightTemperatureCheckAt = millis();
  return;
}

void stopNight(const tm& currentTime) {
  if (!connectToNilan() ||
      !closeBypassIfWinter(currentTime) ||
      !setFanPercentages(configuration.dayInletPercentages,
                          configuration.dayExhaustPercentages)) {
    status = ControllerStatus::ErrorNilan;
    return;
  }

  status = ControllerStatus::Day;
  activeNightVentilationReduction = -1;
  lastNightTemperatureCheckAt = 0;
  return;
}

bool connectNetwork() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if (configuration.useStaticIp &&
      !WiFi.config(configuration.staticIp, configuration.staticGateway,
                   configuration.staticSubnet, configuration.staticDns)) {
    return false;
  }
  WiFi.begin(SSID, PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startedAt >= WIFI_TIMEOUT_MS) {
      return false;
    }
    delay(250);
  }
  return true;
}

bool setTime() {
  // Clear the previously synchronized time so getLocalTime() must wait for a
  // fresh NTP result rather than accepting an old, drifting timestamp.
  timeval invalidTime = {};
  settimeofday(&invalidTime, nullptr);

  configTzTime(DENMARK_TIME_ZONE, "pool.ntp.org", "time.cloudflare.com");
  tm synchronizedTime;
  return getLocalTime(&synchronizedTime, WIFI_TIMEOUT_MS);
}

bool connectNetworkAndSetTime() {
  if (!connectNetwork()) {
    return false;
  }

  return setTime();
}

void startWebServer() {
  if (webServerStarted) {
    return;
  }

  webServer.on("/", HTTP_GET, handleHomepage);
  webServer.on("/settings", HTTP_POST, handleSettingsUpdate);
  webServer.onNotFound([]() {
    webServer.send(404, "text/plain", "Not found");
  });
  webServer.begin();
  webServerStarted = true;
}

void sendEmailNotification() {
  EMailSender::EMailMessage message;
  const bool configurationSucceeded =
      status == ControllerStatus::Day || status == ControllerStatus::Night;

  if (configurationSucceeded) {
    message.subject = "Bedroom ventilation controller success";
    message.message = "<p>The controller configured the Nilan unit.</p>";
    message.message += buildNilanReport();
  } else {
    message.subject = "Bedroom ventilation controller error";
    message.message =
        "The controller could not communicate with or configure the Nilan unit.";
  }

  if (connectNetwork()) {
    const EMailSender::Response response = emailSender.send(EMAIL_RECIPIENT_ADDRESS, message);
    if (!response.status) {
      status = ControllerStatus::ErrorEmail;
    }

    if(!setTime()) { //to make sure it does not drift
      status = ControllerStatus::ErrorNetwork;
    }

  } else {
    status = ControllerStatus::ErrorNetwork;
  }
}

void blinkError(int count) {
  for (int i = 0; i < count; ++i) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(400);
    digitalWrite(LED_BUILTIN, LOW);
    delay(400);
  }
}

void blinkLong() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(4000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(400);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  if (!loadConfiguration()) {
    status = ControllerStatus::ErrorConfiguration;
    return;
  }
  if (!connectNetworkAndSetTime()) {
    status = ControllerStatus::ErrorNetwork;
  } else {
    startWebServer();
  }
}

void loop() {
  if (webServerStarted) {
    webServer.handleClient();
  }

  if (lastControlCycleAt != 0 &&
      millis() - lastControlCycleAt < DELAY_MS) {
    delay(10);
    return;
  }
  lastControlCycleAt = millis();

  if (status == ControllerStatus::ErrorConfiguration) {
    blinkError(1);
    if (loadConfiguration()) {
      status = ControllerStatus::NotSet;
    }
    return;
  }

  const time_t nowEpoch = time(nullptr);
  localtime_r(&nowEpoch, &currentTime);
  const bool nightTime = isNightTime(currentTime);

  bool statusChanged = false;
  if ((status == ControllerStatus::NotSet ||
       status == ControllerStatus::Day) &&
      nightTime) {
    startNight();
    statusChanged = true;
  } else if ((status == ControllerStatus::NotSet ||
              status == ControllerStatus::Night) &&
             !nightTime) {
    stopNight(currentTime);
    statusChanged = true;
  }

  if (!statusChanged && status == ControllerStatus::Night &&
      configuration.useNightTemperatureReduction &&
      millis() - lastNightTemperatureCheckAt >= NIGHT_TEMPERATURE_CHECK_MS) {
    lastNightTemperatureCheckAt = millis();
    if (!connectToNilan() || !applyNightVentilationForTemperature()) {
      status = ControllerStatus::ErrorNilan;
    }
  }

  if(statusChanged) {
    sendEmailNotification();
  }

  blinkLong();

  if (status == ControllerStatus::ErrorNetwork) {
    blinkError(2);
    if (connectNetworkAndSetTime()) {
      startWebServer();
      status = ControllerStatus::NotSet;
    }
  } else if (status == ControllerStatus::ErrorNilan) {
    blinkError(3);
    status = ControllerStatus::NotSet;
  } else if (status == ControllerStatus::ErrorEmail) {
    blinkError(4);
    status = ControllerStatus::NotSet;
  }

}
