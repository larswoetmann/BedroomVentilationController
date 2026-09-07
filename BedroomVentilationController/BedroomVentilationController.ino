#include <WiFi.h>
#include <USBHostSerial.h>
#include <sys/time.h>
#include <time.h>
#include "configs.h"

#include <EMailSender.h>

constexpr uint16_t NILAN_VID = 0x0483;
constexpr uint16_t NILAN_PID = 0x5740;
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long USB_TIMEOUT_MS = 10000;
constexpr unsigned long USB_SETTLE_MS = 2000;
constexpr unsigned long REPLY_TIMEOUT_MS = 3000;
constexpr unsigned long COMMAND_QUIET_MS = 100;
constexpr unsigned long DELAY_MS = 1UL * 60UL * 1000UL;
constexpr int START_NIGHT_MINUTE = 19 * 60;
constexpr int STOP_NIGHT_MINUTE = 5 * 60;
constexpr int NIGHT_INLET_PERCENT = 55;
constexpr int NIGHT_EXHAUST_PERCENT = 60;
constexpr int DAY_INLET_PERCENT[] = {20, 35, 55};
constexpr int DAY_EXHAUST_PERCENT[] = {25, 40, 60};

USBHostSerial nilan(NILAN_VID, NILAN_PID);
bool usbHostStarted = false;

enum class ControllerStatus {
  NotSet,
  Day,
  Night,
  ErrorNetwork,
  ErrorNilan,
  ErrorEmail
};

ControllerStatus status = ControllerStatus::NotSet;
tm currentTime;

EMailSender emailSender(EMAIL_SENDER_ADDRESS, EMAIL_SMTP_PASSWORD,
                        EMAIL_SENDER_ADDRESS, EMAIL_SENDER_NAME,
                        EMAIL_SMTP_HOST, EMAIL_SMTP_PORT);

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
    report += readOk ? value : "unavailable";
    report += "</td></tr>";
  }

  report += "</table>";
  return report;
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

bool isWinterMode(const tm& currentTime) {
  int month = currentTime.tm_mon + 1;
  return month > 9 || month < 5;
}

bool isNightTime(const tm& currentTime) {
  int minuteOfDay = currentTime.tm_hour * 60 + currentTime.tm_min;
  return minuteOfDay >= START_NIGHT_MINUTE ||
         minuteOfDay < STOP_NIGHT_MINUTE;
}

bool openBypass() {
  return setNilanParameter("RTS", 15);
}

bool closeBypassIfWinter(const tm& currentTime) {
  if(isWinterMode(currentTime)) {
      return setNilanParameter("RTS", 25);
  }
  return true;
}

void startNight() {
  const int inletPercentages[] = {
      NIGHT_INLET_PERCENT, NIGHT_INLET_PERCENT, NIGHT_INLET_PERCENT};
  const int exhaustPercentages[] = {
      NIGHT_EXHAUST_PERCENT, NIGHT_EXHAUST_PERCENT, NIGHT_EXHAUST_PERCENT};

  if (!connectToNilan() ||
      !openBypass() ||
      !setFanPercentages(inletPercentages, exhaustPercentages)) {
    status = ControllerStatus::ErrorNilan;
    return;
  }

  status = ControllerStatus::Night;
  return;
}

void stopNight(const tm& currentTime) {
  if (!connectToNilan() ||
      !closeBypassIfWinter(currentTime) ||
      !setFanPercentages(DAY_INLET_PERCENT, DAY_EXHAUST_PERCENT)) {
    status = ControllerStatus::ErrorNilan;
    return;
  }

  status = ControllerStatus::Day;
  return;
}

bool connectNetwork() {
  WiFi.mode(WIFI_STA);
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
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  // Clear the previously synchronized time so getLocalTime() must wait for a
  // fresh NTP result rather than accepting an old, drifting timestamp.
  timeval invalidTime = {};
  settimeofday(&invalidTime, nullptr);

  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  tm currentTime;
  return getLocalTime(&currentTime, WIFI_TIMEOUT_MS);
}

bool connectNetworkAndSetTime() {
  if (!connectNetwork()) {
    return false;
  }

  return setTime();
}

void closeNetwork() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
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

  closeNetwork();
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
  if (!connectNetworkAndSetTime()) {
    status = ControllerStatus::ErrorNetwork;
  }
  closeNetwork();
}

void loop() {
  const time_t nowEpoch = time(nullptr);
  localtime_r(&nowEpoch, &currentTime);

  bool statusChanged = false;
  if((status == ControllerStatus::NotSet || status == ControllerStatus::Day) && isNightTime(currentTime)) {
    startNight();
    statusChanged = true;
  } else if((status == ControllerStatus::NotSet || status == ControllerStatus::Night) && !isNightTime(currentTime)) {
    stopNight(currentTime);
    statusChanged = true;
  }

  if(statusChanged) {
    sendEmailNotification();
  }

  blinkLong();

  if (status == ControllerStatus::ErrorNetwork) {
    blinkError(2);
    if (connectNetworkAndSetTime()) {
      status = ControllerStatus::NotSet;
    }
    closeNetwork();
  } else if (status == ControllerStatus::ErrorNilan) {
    blinkError(3);
    status = ControllerStatus::NotSet;
  } else if (status == ControllerStatus::ErrorEmail) {
    blinkError(4);
    status = ControllerStatus::NotSet;
  }

  delay(DELAY_MS);
}
