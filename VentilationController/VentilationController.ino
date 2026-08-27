#include <WiFi.h>
#include <USBHostSerial.h>

const char* SSID = "Pilotvej47";
const char* PASSWORD = "Pilotvej47!";

const IPAddress LOCAL_IP(192, 168, 1, 32);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS(192, 168, 1, 1);

constexpr uint16_t NILAN_VID = 0x0483;
constexpr uint16_t NILAN_PID = 0x5740;
constexpr unsigned long USB_TIMEOUT_MS = 10000;
constexpr unsigned long REPLY_TIMEOUT_MS = 1000;

WiFiServer server(80);
USBHostSerial nilan(NILAN_VID, NILAN_PID);
bool usbHostStarted = false;

struct NilanValue {
  const char* command;
  const char* label;
  const char* suffix;
  int value;
  bool valid;
};

NilanValue values[] = {
  {"ALR", "Alarm code", "", 0, false},
  {"GFI", "Inlet fan level", "", 0, false},
  {"GFO", "Exhaust fan level", "", 0, false},
  {"F1I", "Fan level 1 inlet", " %", 0, false},
  {"F1O", "Fan level 1 exhaust", " %", 0, false},
  {"F2I", "Fan level 2 inlet", " %", 0, false},
  {"F2O", "Fan level 2 exhaust", " %", 0, false},
  {"F3I", "Fan level 3 inlet", " %", 0, false},
  {"F3O", "Fan level 3 exhaust", " %", 0, false},
  {"F4I", "Fan level 4 inlet", " %", 0, false},
  {"F4O", "Fan level 4 exhaust", " %", 0, false},
  {"GT3", "T3 room / extract air", " &deg;C", 0, false},
  {"GT4", "T4 discharge air", " &deg;C", 0, false},
  {"GT8", "T8 outdoor air", " &deg;C", 0, false},
  {"RTS", "Wanted indoor temperature", " &deg;C", 0, false},
  {"GBS", "Bypass damper", "", 0, false},
  {"GRH", "Relative humidity", " %", 0, false},
};

constexpr size_t VALUE_COUNT = sizeof(values) / sizeof(values[0]);

void clearInput() {
  while (nilan.available()) {
    nilan.read();
  }
}

bool readLine(String& line) {
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

bool sendCommand(const String& command, String& reply) {
  clearInput();
  if (nilan.write(reinterpret_cast<const uint8_t*>(command.c_str()),
                  command.length()) != command.length()) {
    return false;
  }
  return readLine(reply);
}

bool parseValue(const String& reply, int& value) {
  if (reply.length() != 6 || (reply[0] != '+' && reply[0] != '-')) {
    return false;
  }

  int parsed = 0;
  for (size_t i = 1; i < reply.length(); ++i) {
    if (!isDigit(reply[i])) {
      return false;
    }
    parsed = parsed * 10 + reply[i] - '0';
  }

  value = reply[0] == '-' ? -parsed : parsed;
  return true;
}

bool openNilan(String& message) {
  if (!usbHostStarted) {
    message = "USB host failed to start.";
    return false;
  }

  unsigned long startedAt = millis();
  while (!static_cast<bool>(nilan)) {
    if (millis() - startedAt >= USB_TIMEOUT_MS) {
      message = "Nilan USB device 0483:5740 was not detected.";
      return false;
    }
    delay(10);
  }

  String reply;
  if (!sendCommand("S SVC +00000\r", reply)) {
    message = "No reply to the Nilan service command.";
    return false;
  }
  if (reply != "OK") {
    message = "Nilan rejected the service command: " + reply;
    return false;
  }
  return true;
}

bool readValuesOnce(String& message) {
  String reply;
  size_t successfulReads = 0;
  for (size_t i = 0; i < VALUE_COUNT; ++i) {
    values[i].valid = false;

    String command = "G ";
    command += values[i].command;
    command += '\r';

    if (sendCommand(command, reply) &&
        parseValue(reply, values[i].value)) {
      values[i].valid = true;
      ++successfulReads;
    }
  }

  message = "Read " + String(successfulReads) + " of " +
            String(VALUE_COUNT) + " values.";
  return successfulReads == VALUE_COUNT;
}

bool readNilanOnce(String& message) {
  return openNilan(message) && readValuesOnce(message);
}

bool writeNilanParameter(const char* parameter, int value, String& message) {
  char formattedValue[7];
  snprintf(formattedValue, sizeof(formattedValue), "%+06d", value);

  String reply;
  String command = "S ";
  command += parameter;
  command += ' ';
  command += formattedValue;
  command += '\r';
  if (!sendCommand(command, reply)) {
    message = "No reply while setting ";
    message += parameter;
    message += '.';
    return false;
  }
  if (reply != "OK") {
    message = "Nilan rejected ";
    message += parameter;
    message += ": ";
    message += reply;
    return false;
  }
  return true;
}

bool setRoomTemperature(int temperature, String& message) {
  if (!openNilan(message) ||
      !writeNilanParameter("RTS", temperature, message)) {
    return false;
  }

  bool allValuesRead = readValuesOnce(message);
  message = "Wanted indoor temperature set to " + String(temperature) +
            " &deg;C. " + message;
  return allValuesRead;
}

bool setFanLevel3(int inletPercent, int exhaustPercent, String& message) {
  if (!openNilan(message) ||
      !writeNilanParameter("F3I", inletPercent, message) ||
      !writeNilanParameter("F3O", exhaustPercent, message)) {
    return false;
  }

  bool allValuesRead = readValuesOnce(message);
  message = "Fan level 3 set to " + String(inletPercent) +
            "% inlet and " + String(exhaustPercent) + "% exhaust. " +
            message;
  return allValuesRead;
}

String page(bool attempted, bool success, const String& message) {
  String html =
      "<!doctype html><html><head>"
      "<meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Nilan Comfort 252</title>"
      "<style>"
      "body{font-family:system-ui;max-width:700px;margin:40px auto;padding:0 16px}"
      "form{margin:12px 0}input,button{padding:10px;font-size:16px}"
      ".ok{color:green}.error{color:#b91c1c}"
      "table{width:100%;border-collapse:collapse;margin-top:20px}"
      "td{padding:9px;border-bottom:1px solid #ddd}"
      "td:last-child{text-align:right;font-weight:bold}"
      "</style></head><body>"
      "<h1>Nilan Comfort 252</h1>"
      "<form action='/connect' method='get'>"
      "<button name='request' value='connect'>Connect and read</button></form>"
      "<form action='/set-temperature' method='post'>"
      "<label>Wanted indoor temperature: "
      "<input name='temperature' type='number' min='5' max='30' step='1' "
      "required> &deg;C</label> "
      "<button type='submit'>Set temperature</button></form>"
      "<form action='/set-fan-level-3' method='post'>"
      "<label>Fan level 3 inlet: "
      "<input name='inlet' type='number' min='0' max='100' step='1' "
      "required> %</label> "
      "<label>Exhaust: "
      "<input name='exhaust' type='number' min='0' max='100' step='1' "
      "required> %</label> "
      "<button type='submit'>Set fan level 3</button></form>";

  if (attempted) {
    html += success ? "<p class='ok'>" : "<p class='error'>";
    html += message;
    html += "</p><table>";

    for (size_t i = 0; i < VALUE_COUNT; ++i) {
      html += "<tr><td>";
      html += values[i].label;
      html += "</td><td>";
      if (values[i].valid) {
        html += String(values[i].value);
        html += values[i].suffix;
      } else {
        html += "--";
      }
      html += "</td></tr>";
    }
    html += "</table>";
  }

  html += "</body></html>";
  return html;
}

void sendPage(WiFiClient& client, bool attempted, bool success,
              const String& message) {
  String html = page(attempted, success, message);
  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: text/html; charset=utf-8\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.print("Content-Length: ");
  client.print(html.length());
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(html);
  client.flush();
}

bool readRequestLine(WiFiClient& client, String& requestLine) {
  requestLine = "";
  unsigned long startedAt = millis();

  while (client.connected() && millis() - startedAt < 2000) {
    while (client.available()) {
      char c = client.read();
      if (c == '\n') {
        requestLine.trim();
        return !requestLine.isEmpty();
      }
      if (c != '\r' && requestLine.length() < 256) {
        requestLine += c;
      }
    }
    delay(1);
  }

  return false;
}

String requestPath(const String& requestLine) {
  int firstSpace = requestLine.indexOf(' ');
  int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) {
    return "";
  }
  return requestLine.substring(firstSpace + 1, secondSpace);
}

bool startUsbHost(String& message) {
#if ARDUINO_USB_CDC_ON_BOOT
  message =
      "USB CDC On Boot is enabled. In Arduino IDE select Tools > "
      "USB CDC On Boot > Disabled, upload the sketch again, and retry.";
  return false;
#else
  if (!usbHostStarted) {
    usbHostStarted = nilan.begin(9600, 0, 0, 8);
  }
  if (!usbHostStarted) {
    message = "USB host failed to start.";
    return false;
  }
  return true;
#endif
}

String readBody(WiFiClient& client, int contentLength) {
  String body;
  body.reserve(contentLength);
  unsigned long startedAt = millis();
  while (body.length() < static_cast<size_t>(contentLength) &&
         client.connected() && millis() - startedAt < 2000) {
    while (client.available() &&
           body.length() < static_cast<size_t>(contentLength)) {
      body += static_cast<char>(client.read());
    }
    delay(1);
  }
  return body;
}

bool parseFormInteger(const String& body, const char* name, int minimum,
                      int maximum, int& parsedValue) {
  String prefix = String(name) + '=';
  int start = body.indexOf(prefix);
  if (start < 0 || (start > 0 && body[start - 1] != '&')) {
    return false;
  }
  start += prefix.length();
  int end = body.indexOf('&', start);
  if (end < 0) {
    end = body.length();
  }
  String value = body.substring(start, end);
  if (value.isEmpty()) {
    return false;
  }
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) {
      return false;
    }
  }
  parsedValue = value.toInt();
  return parsedValue >= minimum && parsedValue <= maximum;
}

bool parseTemperature(const String& body, int& temperature) {
  return parseFormInteger(body, "temperature", 5, 30, temperature);
}

bool parseFanLevel3(const String& body, int& inletPercent,
                    int& exhaustPercent) {
  return parseFormInteger(body, "inlet", 0, 100, inletPercent) &&
         parseFormInteger(body, "exhaust", 0, 100, exhaustPercent);
}

void handleClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  String requestLine;
  if (!readRequestLine(client, requestLine)) {
    client.stop();
    return;
  }

  String path = requestPath(requestLine);
  bool connectRequested =
      path == "/connect" || path == "/connect/" ||
      path.startsWith("/connect?");
  bool setRequested =
      requestLine.startsWith("POST ") && path == "/set-temperature";
  bool setFanLevel3Requested =
      requestLine.startsWith("POST ") && path == "/set-fan-level-3";

  client.setTimeout(500);
  int contentLength = 0;
  while (client.connected()) {
    String header = client.readStringUntil('\n');
    if (header == "\r" || header.isEmpty()) {
      break;
    }
    header.trim();
    if (header.startsWith("Content-Length:")) {
      contentLength = header.substring(15).toInt();
    }
  }

  if (connectRequested) {
    String message;
    bool success =
        startUsbHost(message) && readNilanOnce(message);
    sendPage(client, true, success, message);
  } else if (setRequested) {
    String message;
    int temperature = 0;
    String body = readBody(client, contentLength);
    bool success = parseTemperature(body, temperature);
    if (!success) {
      message = "Temperature must be a whole number from 5 to 30 &deg;C.";
    } else {
      success = startUsbHost(message) &&
                setRoomTemperature(temperature, message);
    }
    sendPage(client, true, success, message);
  } else if (setFanLevel3Requested) {
    String message;
    int inletPercent = 0;
    int exhaustPercent = 0;
    String body = readBody(client, contentLength);
    bool success = parseFanLevel3(body, inletPercent, exhaustPercent);
    if (!success) {
      message =
          "Fan percentages must be whole numbers from 0 to 100.";
    } else {
      success = startUsbHost(message) &&
                setFanLevel3(inletPercent, exhaustPercent, message);
    }
    sendPage(client, true, success, message);
  } else {
    sendPage(client, false, false, "");
  }

  delay(10);
  client.stop();
}

void setup() {
  WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  server.begin();
}

void loop() {
  handleClient();
}
