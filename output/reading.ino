#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

const char* apSSID = "PostureESP";
const char* apPassword = "12345678";

const char* internetSSID = "ufdevice";
const char* internetPassword = "gogators";

WiFiServer server(80);

const int ledPin = 26;
const int buzzerPin = 27;

const String firebaseBase = "https://iot-posture-monitoring-default-rtdb.firebaseio.com";

bool sessionActive = false;
String currentSessionId = "";
String lastUploadedStatus = "";
unsigned long lastControlCheck = 0;
const unsigned long controlCheckInterval = 2000;

unsigned long lastBuzzTime = 0;
bool buzzerState = false;

String httpsGET(const String& path) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  https.begin(client, url);
  int httpCode = https.GET();

  String payload = "";
  if (httpCode > 0) {
    payload = https.getString();
  }

  https.end();
  return payload;
}

void httpsPUT(const String& path, const String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.PUT(json);
  https.end();
}

void httpsPATCH(const String& path, const String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.PATCH(json);
  https.end();
}

void httpsPOST(const String& path, const String& json) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");
  https.POST(json);
  https.end();
}

String extractJsonString(String json, String key) {
  String target = "\"" + key + "\":";
  int start = json.indexOf(target);
  if (start == -1) return "";

  start = json.indexOf("\"", start + target.length());
  if (start == -1) return "";

  int end = json.indexOf("\"", start + 1);
  if (end == -1) return "";

  return json.substring(start + 1, end);
}

bool extractJsonBool(String json, String key) {
  String target = "\"" + key + "\":";
  int start = json.indexOf(target);
  if (start == -1) return false;

  String tail = json.substring(start + target.length());
  tail.trim();

  return tail.startsWith("true");
}

void updateControlFromFirebase() {
  String controlJson = httpsGET("/control");
  if (controlJson.length() == 0 || controlJson == "null") return;

  sessionActive = extractJsonBool(controlJson, "active");
  currentSessionId = extractJsonString(controlJson, "sessionId");

  Serial.print("Session active: ");
  Serial.println(sessionActive ? "true" : "false");
  Serial.print("Session ID: ");
  Serial.println(currentSessionId);
}

void handlePostureStatus(String status) {
  if (status == "BAD") {
    digitalWrite(ledPin, HIGH);

    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzTime > 600) {
      lastBuzzTime = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState);
    }
  } else {
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  }

  if (!sessionActive || currentSessionId == "") {
    Serial.println("No active session, not uploading");
    return;
  }

  unsigned long nowMs = millis();

  String currentJson =
    "{"
    "\"status\":\"" + status + "\","
    "\"sessionId\":\"" + currentSessionId + "\","
    "\"updatedAt\":" + String(nowMs) +
    "}";

  httpsPUT("/current", currentJson);

  String sessionJson =
    "{"
    "\"latestStatus\":\"" + status + "\""
    "}";

  httpsPATCH("/sessions/" + currentSessionId, sessionJson);

  if (status != lastUploadedStatus) {
    String eventJson =
      "{"
      "\"status\":\"" + status + "\","
      "\"timestamp\":" + String(nowMs) +
      "}";

    httpsPOST("/sessions/" + currentSessionId + "/events", eventJson);
    lastUploadedStatus = status;

    Serial.print("Uploaded new event: ");
    Serial.println(status);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  WiFi.begin(internetSSID, internetPassword);
  Serial.print("Connecting to internet");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Connected to internet");
  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());

  server.begin();
  Serial.println("Server started");
}

void loop() {
  if (millis() - lastControlCheck > controlCheckInterval) {
    lastControlCheck = millis();
    updateControlFromFirebase();
  }

  WiFiClient client = server.available();

  if (client) {
    String message = "";

    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        message += c;
      }

      if (message.length() > 0) {
        message.trim();
        Serial.print("Received from input ESP: ");
        Serial.println(message);

        if (message == "GOOD" || message == "BAD") {
          handlePostureStatus(message);
        }

        break;
      }
    }

    client.stop();
  }
}