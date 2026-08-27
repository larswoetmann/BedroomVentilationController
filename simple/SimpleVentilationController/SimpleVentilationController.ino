#include <WiFi.h>
#include <USBHostSerial.h>

// WiFi credentials
const char* SSID = "Pilotvej47";
const char* PASSWORD = "Pilotvej47!";

// Static network configuration
const IPAddress LOCAL_IP(192, 168, 1, 32);
const IPAddress GATEWAY(192, 168, 1, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const IPAddress DNS(192, 168, 1, 1);

constexpr uint16_t NILAN_VID = 0x0483;
constexpr uint16_t NILAN_PID = 0x5740;
constexpr unsigned long QUERY_INTERVAL_MS = 100;
constexpr unsigned long RESPONSE_TIMEOUT_MS = 500;
constexpr unsigned long DATA_STALE_MS = 10000;
constexpr unsigned long USB_DETECTION_TIMEOUT_MS = 15000;
constexpr unsigned long USB_START_DELAY_MS = 1500;
constexpr unsigned long WIFI_RETRY_MS = 30000;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr size_t CONSOLE_LINE_COUNT = 32;
constexpr uint32_t USB_START_MARKER = 0x4E494C41;

RTC_DATA_ATTR uint32_t resetMarker = 0;

WiFiServer server(80);
USBHostSerial nilanSerial(NILAN_VID, NILAN_PID);
bool serverStarted = false;
bool usbHostStarted = false;
bool wiFiTimeoutLogged = false;
wl_status_t lastWiFiStatus = WL_NO_SHIELD;
unsigned long lastWiFiAttemptAt = 0;
String consoleLines[CONSOLE_LINE_COUNT];
size_t consoleStart = 0;
size_t consoleCount = 0;

struct NilanValue {
  const char* command;
  int value;
  bool valid;
};

NilanValue nilanValues[] = {
  {"ALR", 0, false},
  {"GFI", 0, false},
  {"GFO", 0, false},
  {"GT3", 0, false},
  {"GT4", 0, false},
  {"GT8", 0, false},
  {"GBS", 0, false},
  {"GRH", 0, false},
};

constexpr size_t NILAN_VALUE_COUNT =
    sizeof(nilanValues) / sizeof(nilanValues[0]);

enum class NilanState {
  Idle,
  StartingHost,
  WaitingForUsb,
  WaitingForServiceReply,
  Ready,
  WaitingForValue,
  Failed
};

NilanState nilanState = NilanState::Idle;
bool connectionRequested = false;
String nilanReply;
size_t queryIndex = 0;
unsigned long requestStartedAt = 0;
unsigned long lastQueryAt = 0;
unsigned long lastDataAt = 0;
unsigned long connectionStartedAt = 0;

const char HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Nilan Comfort 252</title>
  <style>
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      padding: 2rem 1rem;
      font-family: system-ui, sans-serif;
      color: #e2e8f0;
      background: #0f172a;
    }
    main { width: min(900px, 100%); margin: auto; }
    header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 1rem;
      margin-bottom: 1.5rem;
    }
    h1 { margin: 0; color: #38bdf8; font-size: clamp(1.6rem, 5vw, 2.4rem); }
    .status {
      padding: .35rem .8rem;
      border-radius: 999px;
      background: #475569;
      font-size: .85rem;
      font-weight: 700;
    }
    .status.online { background: #15803d; }
    .status.offline { background: #b91c1c; }
    .connect {
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 1rem;
      margin-bottom: 1rem;
      padding: 1rem 1.25rem;
      border: 1px solid #334155;
      border-radius: 14px;
      background: #1e293b;
    }
    button {
      padding: .7rem 1.2rem;
      border: 0;
      border-radius: 9px;
      color: #fff;
      background: #0284c7;
      font: inherit;
      font-weight: 700;
      cursor: pointer;
    }
    button:disabled { opacity: .5; cursor: wait; }
    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(190px, 1fr));
      gap: 1rem;
    }
    .card {
      padding: 1.25rem;
      border: 1px solid #334155;
      border-radius: 14px;
      background: #1e293b;
    }
    .label { color: #94a3b8; font-size: .9rem; }
    .value { margin-top: .35rem; font-size: 1.9rem; font-weight: 700; }
    .alarm-ok { color: #4ade80; }
    .alarm-active { color: #f87171; }
    .console {
      height: 220px;
      margin-top: 1rem;
      padding: 1rem;
      overflow: auto;
      white-space: pre-wrap;
      border: 1px solid #334155;
      border-radius: 14px;
      color: #a7f3d0;
      background: #020617;
      font: .82rem/1.5 ui-monospace, monospace;
    }
    footer { margin-top: 1.25rem; color: #64748b; font-size: .8rem; }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>Nilan Comfort 252</h1>
      <span class="status %%STATUS_CLASS%%">%%INITIAL_STATE%%</span>
    </header>
    <section class="connect">
      <span>Connect the ESP32-S3 to the Nilan USB port, then start.</span>
      <form action="/connect" method="get">
        <button id="connect" type="submit">Connect</button>
      </form>
    </section>
    <section class="grid">
      <article class="card"><div class="label">Alarm</div><div class="value %%ALARM_CLASS%%">%%ALARM%%</div></article>
      <article class="card"><div class="label">Inlet fan level</div><div class="value">%%INLET%%</div></article>
      <article class="card"><div class="label">Exhaust fan level</div><div class="value">%%EXHAUST%%</div></article>
      <article class="card"><div class="label">T3 room / extract air</div><div class="value">%%T3%%</div></article>
      <article class="card"><div class="label">T4 discharge air</div><div class="value">%%T4%%</div></article>
      <article class="card"><div class="label">T8 outdoor air</div><div class="value">%%T8%%</div></article>
      <article class="card"><div class="label">Relative humidity</div><div class="value">%%HUMIDITY%%</div></article>
      <article class="card"><div class="label">Bypass damper</div><div class="value">%%BYPASS%%</div></article>
    </section>
    <div id="console" class="console" role="log">%%INITIAL_CONSOLE%%</div>
    <footer>Read-only USB connection to the ventilation controller</footer>
  </main>
</body>
</html>
)rawliteral";

void logMessage(const String& message) {
  String line = "[" + String(millis()) + " ms] " + message;
  Serial.println(line);

  if (consoleCount < CONSOLE_LINE_COUNT) {
    size_t index = (consoleStart + consoleCount) % CONSOLE_LINE_COUNT;
    consoleLines[index] = line;
    ++consoleCount;
  } else {
    consoleLines[consoleStart] = line;
    consoleStart = (consoleStart + 1) % CONSOLE_LINE_COUNT;
  }
}

void usbHostLogger(const char* message) {
  static String previousMessage;
  static unsigned long previousMessageAt = 0;
  if (previousMessage == message && millis() - previousMessageAt < 30000) {
    return;
  }
  previousMessage = message;
  previousMessageAt = millis();
  logMessage(String("USB: ") + message);
}


const char* connectionStateLabel() {
  switch (nilanState) {
    case NilanState::Idle:
      return "Not connected";
    case NilanState::StartingHost:
      return "Starting USB host";
    case NilanState::WaitingForUsb:
      return "Waiting for USB";
    case NilanState::WaitingForServiceReply:
      return "Connecting";
    case NilanState::Ready:
    case NilanState::WaitingForValue:
      return "Connected";
    case NilanState::Failed:
      return "Connection failed";
  }
  return "Connection failed";
}

void finishHttpHeaders(WiFiClient& client) {
  String line;
  unsigned long startedAt = millis();
  while (client.connected() && millis() - startedAt < 500) {
    if (!client.available()) {
      delay(1);
      continue;
    }
    char c = client.read();
    if (c == '\n') {
      if (line.isEmpty()) {
        return;
      }
      line = "";
    } else if (c != '\r' && line.length() < 256) {
      line += c;
    }
  }
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      default:
        escaped += value[i];
    }
  }
  return escaped;
}

String consoleSnapshot() {
  String snapshot;

  for (size_t i = 0; i < consoleCount; ++i) {
    if (i > 0) {
      snapshot += '\n';
    }
    size_t index = (consoleStart + i) % CONSOLE_LINE_COUNT;
    snapshot += consoleLines[index];
  }
  return snapshot.isEmpty() ? "Ready. Click Connect to start." : snapshot;
}

String displayValue(size_t index, const char* prefix = "", const char* suffix = "") {
  if (!nilanValues[index].valid) {
    return "--";
  }
  return String(prefix) + String(nilanValues[index].value) + suffix;
}

void sendDashboard(WiFiClient& client) {
  String page = FPSTR(HTML);
  page.replace("%%INITIAL_STATE%%", connectionStateLabel());
  page.replace("%%STATUS_CLASS%%",
               nilanState == NilanState::Ready ? "online" :
               nilanState == NilanState::Failed ? "offline" : "");
  page.replace("%%INITIAL_CONSOLE%%", htmlEscape(consoleSnapshot()));
  page.replace("%%ALARM%%",
               !nilanValues[0].valid ? "--" :
               nilanValues[0].value == 0 ? "No alarm" :
               "Code " + String(nilanValues[0].value));
  page.replace("%%ALARM_CLASS%%",
               !nilanValues[0].valid ? "" :
               nilanValues[0].value == 0 ? "alarm-ok" : "alarm-active");
  page.replace("%%INLET%%", displayValue(1, "Level "));
  page.replace("%%EXHAUST%%", displayValue(2, "Level "));
  page.replace("%%T3%%", displayValue(3, "", " °C"));
  page.replace("%%T4%%", displayValue(4, "", " °C"));
  page.replace("%%T8%%", displayValue(5, "", " °C"));
  page.replace("%%HUMIDITY%%", displayValue(7, "", " %"));
  page.replace("%%BYPASS%%",
               !nilanValues[6].valid ? "--" :
               nilanValues[6].value != 0 ? "Open" : "Closed");

  client.print("HTTP/1.1 200 OK\r\n");
  client.print("Content-Type: text/html; charset=utf-8\r\n");
  client.print("Cache-Control: no-store\r\n");
  client.print("Content-Length: ");
  client.print(page.length());
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(page);
  client.flush();
}

void handleWebClient() {
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  String requestLine;
  unsigned long requestStartedAt = millis();
  while (client.connected() && millis() - requestStartedAt < 1000) {
    if (!client.available()) {
      delay(1);
      continue;
    }

    char c = client.read();
    if (c == '\n') {
      break;
    }
    if (c != '\r' && requestLine.length() < 128) {
      requestLine += c;
    }
  }

  finishHttpHeaders(client);

  if (requestLine.startsWith("GET /connect")) {
    String error;
    if (!nilanSerial.begin(9600, 0, 0, 8)) {
      logMessage("ERROR: connection request failed: " + error);
    }
    sendDashboard(client);
  } else if (requestLine.startsWith("GET / ")) {
    logMessage("Web: dashboard requested from " +
               client.remoteIP().toString());
    sendDashboard(client);
  } else {
    logMessage("Web: unsupported request: " + requestLine);
    client.print("HTTP/1.1 404 Not Found\r\n");
    client.print("Connection: close\r\n\r\n");
  }

  delay(1);
  client.stop();
}

void startWiFiConnection() {
  lastWiFiAttemptAt = millis();
  wiFiTimeoutLogged = false;
  logMessage("WiFi: connecting to " + String(SSID));
  WiFi.disconnect();
  WiFi.begin(SSID, PASSWORD);
}

void maintainWiFi() {
  wl_status_t status = WiFi.status();
  if (status != lastWiFiStatus) {
    logMessage("WiFi: status changed to " + String(static_cast<int>(status)));
    lastWiFiStatus = status;
  }

  if (status == WL_CONNECTED) {
    if (!serverStarted) {
      server.begin();
      serverStarted = true;
      logMessage("Web: server started at http://" +
                 WiFi.localIP().toString() + "/");
    }
    return;
  }

  if (serverStarted) {
    server.end();
    serverStarted = false;
    logMessage("Web: server stopped because WiFi disconnected");
  }

  unsigned long elapsed = millis() - lastWiFiAttemptAt;
  if (elapsed >= WIFI_RETRY_MS) {
    startWiFiConnection();
  } else if (elapsed >= WIFI_CONNECT_TIMEOUT_MS && !wiFiTimeoutLogged) {
    wiFiTimeoutLogged = true;
    logMessage("WiFi: connection attempt timed out; status=" +
               String(static_cast<int>(status)));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  logMessage("Boot: VentilationController starting");

  if (!WiFi.config(LOCAL_IP, GATEWAY, SUBNET, DNS)) {
    logMessage("WiFi: failed to configure static IP 192.168.1.32");
  } else {
    logMessage("WiFi: static IP configured as 192.168.1.32");
  }
  WiFi.setAutoReconnect(true);
  startWiFiConnection();
}

void loop() {
  maintainWiFi();
  if (serverStarted) {
    handleWebClient();
  }
}
