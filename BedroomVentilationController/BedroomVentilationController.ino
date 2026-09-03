#include <WiFi.h>
#include <USBHostSerial.h>
#include <esp_sleep.h>
#include <time.h>

#if __has_include("email_config.h")
#include "email_config.h"
#endif

#include <EMailSender.h>

const char* SSID = "Pilotvej47";
const char* PASSWORD = "Pilotvej47!";

constexpr uint16_t NILAN_VID = 0x0483;
constexpr uint16_t NILAN_PID = 0x5740;
constexpr unsigned long WIFI_TIMEOUT_MS = 15000;
constexpr unsigned long USB_TIMEOUT_MS = 10000;
constexpr unsigned long USB_SETTLE_MS = 2000;
constexpr unsigned long REPLY_TIMEOUT_MS = 3000;
constexpr unsigned long COMMAND_QUIET_MS = 100;
constexpr uint64_t SLEEP_DURATION_US = 15ULL * 60ULL * 1000000ULL;

constexpr int START_NIGHT_MINUTE = 19 * 60;
constexpr int STOP_NIGHT_MINUTE = 5 * 60;
constexpr int NIGHT_INLET_PERCENT = 55;
constexpr int NIGHT_EXHAUST_PERCENT = 60;
constexpr int DAY_INLET_PERCENT[] = {20, 35, 55};
constexpr int DAY_EXHAUST_PERCENT[] = {25, 40, 60};

USBHostSerial nilan(NILAN_VID, NILAN_PID);
bool usbHostStarted = false;

enum class ControllerStatus {
  Day,
  Night,
  ErrorNetwork,
  ErrorNilan
};

ControllerStatus status = ControllerStatus::Day;

EMailSender emailSender(EMAIL_SENDER_ADDRESS, EMAIL_SMTP_PASSWORD,
                        EMAIL_SENDER_ADDRESS, EMAIL_SENDER_NAME,
                        EMAIL_SMTP_HOST, EMAIL_SMTP_PORT);

void sendEmailNotification() {
  EMailSender::EMailMessage message;

  if (status == ControllerStatus::ErrorNilan) {
    message.subject = "Bedroom ventilation controller error";
    message.message = "The controller could not communicate with or configure the Nilan unit.";
  } else {
    message.subject = "Bedroom ventilation controller success";
    message.message = "The controller configured the Nilan unit.";
  }

  emailSender.send(EMAIL_RECIPIENT_ADDRESS, message);
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
  int day = currentTime.tm_mday;
  return month > 9 || month < 5 ||
         (month == 9 && day >= 1);
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

bool connectNetworkAndSetTime(tm currentTime) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startedAt >= WIFI_TIMEOUT_MS) {
      return false;
    }
    delay(250);
  }

  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");

  return getLocalTime(&currentTime, WIFI_TIMEOUT_MS);
}

void closeNetwork() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void blinkError(int count) {
  for (int i = 0; i < count; ++i) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(400);
    digitalWrite(LED_BUILTIN, LOW);
    delay(400);
  }
}

void sleepUntilNextCheck(const tm& currentTime) {
  digitalWrite(LED_BUILTIN, LOW);
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION_US); //TODO: update to calculate wakeup to be when time is next 05:05 or 19:05
  esp_deep_sleep_start();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  tm currentTime;
  if (!connectNetworkAndSetTime(currentTime)) {
    status = ControllerStatus::ErrorNetwork;
  } else {
    if(isNightTime(currentTime)) {
       startNight();
    } else {
      stopNight(currentTime);
    }
    delay(4000);
    sendEmailNotification();
    closeNetwork();
    sleepUntilNextCheck(currentTime);
  }
}

void loop() {
  if (status == ControllerStatus::ErrorNetwork) {
    blinkError(2);
  } else if (status == ControllerStatus::ErrorNilan) {
    blinkError(3);
  } else {
    blinkError(4);
  }
  delay(4000);
}