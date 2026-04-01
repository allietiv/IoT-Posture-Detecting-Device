#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "PostureESP";
const char* password = "12345678";

WebServer server(80);

const int ledPin = 26;
const int buzzerPin = 27;

unsigned long lastBuzzToggle = 0;
bool buzzerState = false;

// ----------------------------
// Schema state
// ----------------------------
struct ControlState {
  bool active = false;
  String sessionId = "";
  unsigned long startedAt = 0;
};

struct LiveState {
  String sessionId = "";
  String status = "UNKNOWN";
  int score = 0;
  unsigned long updatedAt = 0;
  unsigned long durationSeconds = 0;
};

struct SessionLiveState {
  unsigned long startedAt = 0;
  unsigned long endedAt = 0;
  String latestStatus = "UNKNOWN";
  int goodCount = 0;
  int okayCount = 0;
  int badCount = 0;
  int totalReadings = 0;
  int goodPercentage = 0;
};

const int MAX_TIMELINE_POINTS = 120;

struct TimelinePoint {
  unsigned long timestamp = 0;
  String status = "UNKNOWN";
  int score = 0;
};

TimelinePoint timeline[MAX_TIMELINE_POINTS];
int timelineCount = 0;

ControlState controlState;
LiveState liveState;
SessionLiveState sessionState;

// ----------------------------
// Helpers
// ----------------------------
String generateSessionId() {
  return "session_" + String(millis());
}

int statusToScore(const String& status) {
  if (status == "GOOD") return 90;
  if (status == "OKAY") return 60;
  if (status == "BAD") return 30;
  return 0;
}

void resetSessionStats(const String& sessionId) {
  controlState.active = true;
  controlState.sessionId = sessionId;
  controlState.startedAt = millis();

  liveState.sessionId = sessionId;
  liveState.status = "UNKNOWN";
  liveState.score = 0;
  liveState.updatedAt = millis();
  liveState.durationSeconds = 0;

  sessionState.startedAt = millis();
  sessionState.endedAt = 0;
  sessionState.latestStatus = "UNKNOWN";
  sessionState.goodCount = 0;
  sessionState.okayCount = 0;
  sessionState.badCount = 0;
  sessionState.totalReadings = 0;
  sessionState.goodPercentage = 0;

  timelineCount = 0;
}

void endCurrentSession() {
  if (!controlState.active) return;
  sessionState.endedAt = millis();
  controlState.active = false;
}

void addTimelinePoint(const String& status, int score) {
  if (timelineCount < MAX_TIMELINE_POINTS) {
    timeline[timelineCount].timestamp = millis();
    timeline[timelineCount].status = status;
    timeline[timelineCount].score = score;
    timelineCount++;
  } else {
    for (int i = 1; i < MAX_TIMELINE_POINTS; i++) {
      timeline[i - 1] = timeline[i];
    }
    timeline[MAX_TIMELINE_POINTS - 1].timestamp = millis();
    timeline[MAX_TIMELINE_POINTS - 1].status = status;
    timeline[MAX_TIMELINE_POINTS - 1].score = score;
  }
}

void updateGoodPercentage() {
  if (sessionState.totalReadings == 0) {
    sessionState.goodPercentage = 0;
  } else {
    sessionState.goodPercentage =
      (sessionState.goodCount * 100) / sessionState.totalReadings;
  }
}

void applyOutputFeedback(const String& status) {
  if (status == "BAD") {
    digitalWrite(ledPin, HIGH);

    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzToggle >= 600) {
      lastBuzzToggle = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState ? HIGH : LOW);
    }
  } else if (status == "OKAY") {
    // okay = LED on, no buzzer
    digitalWrite(ledPin, HIGH);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  } else {
    // good = LED off, no buzzer
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  }
}

void recordPostureStatus(const String& status) {
  if (!controlState.active) return;

  int score = statusToScore(status);
  unsigned long now = millis();

  liveState.sessionId = controlState.sessionId;
  liveState.status = status;
  liveState.score = score;
  liveState.updatedAt = now;
  liveState.durationSeconds = (now - controlState.startedAt) / 1000;

  sessionState.latestStatus = status;
  sessionState.totalReadings++;

  if (status == "GOOD") {
    sessionState.goodCount++;
  } else if (status == "OKAY") {
    sessionState.okayCount++;
  } else if (status == "BAD") {
    sessionState.badCount++;
  }

  updateGoodPercentage();
  addTimelinePoint(status, score);
  applyOutputFeedback(status);
}

// ----------------------------
// JSON builders
// ----------------------------
String buildTimelineJson() {
  String json = "[";

  for (int i = 0; i < timelineCount; i++) {
    json += "{";
    json += "\"timestamp\":" + String(timeline[i].timestamp) + ",";
    json += "\"status\":\"" + timeline[i].status + "\",";
    json += "\"score\":" + String(timeline[i].score);
    json += "}";

    if (i < timelineCount - 1) {
      json += ",";
    }
  }

  json += "]";
  return json;
}

String buildSchemaJson() {
  String json = "{";

  json += "\"control\":{";
  json += "\"active\":" + String(controlState.active ? "true" : "false") + ",";
  json += "\"sessionId\":\"" + controlState.sessionId + "\",";
  json += "\"startedAt\":" + String(controlState.startedAt);
  json += "},";

  json += "\"live\":{";
  json += "\"sessionId\":\"" + liveState.sessionId + "\",";
  json += "\"status\":\"" + liveState.status + "\",";
  json += "\"score\":" + String(liveState.score) + ",";
  json += "\"updatedAt\":" + String(liveState.updatedAt) + ",";
  json += "\"durationSeconds\":" + String(liveState.durationSeconds);
  json += "},";

  json += "\"sessions_live\":{";
  json += "\"" + controlState.sessionId + "\":{";
  json += "\"startedAt\":" + String(sessionState.startedAt) + ",";
  json += "\"endedAt\":" + String(sessionState.endedAt) + ",";
  json += "\"latestStatus\":\"" + sessionState.latestStatus + "\",";
  json += "\"goodCount\":" + String(sessionState.goodCount) + ",";
  json += "\"okayCount\":" + String(sessionState.okayCount) + ",";
  json += "\"badCount\":" + String(sessionState.badCount) + ",";
  json += "\"totalReadings\":" + String(sessionState.totalReadings) + ",";
  json += "\"goodPercentage\":" + String(sessionState.goodPercentage) + ",";
  json += "\"timeline\":" + buildTimelineJson();
  json += "}";
  json += "}";

  json += "}";

  return json;
}

// ----------------------------
// HTTP handlers
// ----------------------------
void handleRoot() {
  String msg = "";
  msg += "Posture Output ESP is running\n";
  msg += "GET /schema -> full schema JSON\n";
  msg += "GET /start -> start session\n";
  msg += "GET /end -> end session\n";
  msg += "GET /status?value=GOOD or OKAY or BAD -> send posture update\n";
  server.send(200, "text/plain", msg);
}

void handleSchema() {
  server.send(200, "application/json", buildSchemaJson());
}

void handleStart() {
  String newSessionId = generateSessionId();
  resetSessionStats(newSessionId);
  server.send(200, "application/json", buildSchemaJson());
}

void handleEnd() {
  endCurrentSession();
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);
  buzzerState = false;
  server.send(200, "application/json", buildSchemaJson());
}

void handleStatusUpdate() {
  if (!server.hasArg("value")) {
    server.send(400, "application/json", "{\"error\":\"Missing value parameter\"}");
    return;
  }

  String status = server.arg("value");
  status.trim();
  status.toUpperCase();

  if (status != "GOOD" && status != "OKAY" && status != "BAD") {
    server.send(400, "application/json", "{\"error\":\"value must be GOOD, OKAY, or BAD\"}");
    return;
  }

  if (!controlState.active) {
    String newSessionId = generateSessionId();
    resetSessionStats(newSessionId);
  }

  recordPostureStatus(status);
  server.send(200, "application/json", buildSchemaJson());
}

void handleLegacyPost() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing request body");
    return;
  }

  String body = server.arg("plain");
  body.trim();
  body.toUpperCase();

  if (body != "GOOD" && body != "OKAY" && body != "BAD") {
    server.send(400, "text/plain", "Body must be GOOD, OKAY, or BAD");
    return;
  }

  if (!controlState.active) {
    String newSessionId = generateSessionId();
    resetSessionStats(newSessionId);
  }

  recordPostureStatus(body);
  server.send(200, "application/json", buildSchemaJson());
}

// ----------------------------
// Setup / loop
// ----------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  Serial.println("Access Point started");
  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/schema", HTTP_GET, handleSchema);
  server.on("/start", HTTP_GET, handleStart);
  server.on("/end", HTTP_GET, handleEnd);
  server.on("/status", HTTP_GET, handleStatusUpdate);
  server.on("/posture", HTTP_POST, handleLegacyPost);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}