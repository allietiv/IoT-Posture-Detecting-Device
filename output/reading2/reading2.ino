// output/reading2/reading2.ino - LED and buzzer

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>

// Device and Wifi credentials
const char* apSSID = "PostureESP";
const char* apPassword = "12345678";

const char* internetSSID = "ufdevice";
const char* internetPassword = "gogators";

// Firebase Realtime Database URL
const String firebaseBase = "https://iot-posture-monitoring-default-rtdb.firebaseio.com";

// local HTTP server on port 80
WebServer server(80);

const int REDled = 26;    // poor posture
const int GREENled = 33;
const int YELLOWled = 25;
const int buzzerPin = 27; // audible buzzer pin (bad status)
const int buttonPin = 14;
bool lastButtonState = HIGH;

unsigned long lastBuzzToggle = 0;
bool buzzerState = false;

// new debug/upload flags
bool pendingLiveUpload = false;
bool pendingSessionUpload = false;
String lastReceivedStatus = "";
unsigned long lastStatusRequestMs = 0;

// DATA STRUCTUTRES

// tracks whether a session is active
struct ControlState {
  bool active = false;          // true if a session is active
  String sessionId = "";        // current session ID
  unsigned long startedAt = 0;  // timestamp when current session started
};

// most recent posture reading (mirros /live in Firebase)
struct LiveState {
  String sessionId = "";
  String status = "UNKNOWN";
  int score = 0;
  unsigned long updatedAt = 0;
  unsigned long durationSeconds = 0;
};

// stores stats for whole session
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

// timeline tracking
const int MAX_TIMELINE_POINTS = 120;

// each point stores time + posture info
struct TimelinePoint {
  unsigned long timestamp = 0;
  String status = "UNKNOWN";
  int score = 0;
};
// array to store timeline history
TimelinePoint timeline[MAX_TIMELINE_POINTS];
int timelineCount = 0;

ControlState controlState;
LiveState liveState;
SessionLiveState sessionState;

// function prototypes
void uploadControlToFirebase();
void uploadLiveToFirebase();
void uploadSessionSummaryToFirebase();
void setLiveInactive();
void endCurrentSession();
void startSession();
void stopSession();
void recordPostureStatus(const String& status);
void applyOutputFeedback(const String& status);
void updateGoodPercentage();
void addTimelinePoint(const String& status, int score);
void addCORSHeaders();
void handleRoot();
void handleStart();
void handleEnd();
void handleStatusUpdate();

// firebase helpers

// get request from firebase, creates secure client & sends request
String httpsGET(const String& path) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  Serial.print("[HTTPS GET] ");
  Serial.println(url);

  https.begin(client, url);
  int httpCode = https.GET();

  String payload = "";
  if (httpCode > 0) {
    payload = https.getString();
    Serial.print("[HTTPS GET] code: ");
    Serial.println(httpCode);
  } else {
    Serial.print("[HTTPS GET] failed, code: ");
    Serial.println(httpCode);
  }

  https.end();
  return payload;
}

// put request - sends json to firebase
void httpsPUT(const String& path, const String& json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTPS PUT] Skipped, internet not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  Serial.print("[HTTPS PUT] ");
  Serial.println(url);
  Serial.print("[HTTPS PUT] body: ");
  Serial.println(json);

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  int httpCode = https.PUT(json);
  Serial.print("[HTTPS PUT] code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = https.getString();
    Serial.print("[HTTPS PUT] response: ");
    Serial.println(response);
  }

  https.end();
}

// post request- adds new data
void httpsPOST(const String& path, const String& json) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTPS POST] Skipped, internet not connected");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = firebaseBase + path + ".json";

  Serial.print("[HTTPS POST] ");
  Serial.println(url);
  Serial.print("[HTTPS POST] body: ");
  Serial.println(json);

  https.begin(client, url);
  https.addHeader("Content-Type", "application/json");

  int httpCode = https.POST(json);
  Serial.print("[HTTPS POST] code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String response = https.getString();
    Serial.print("[HTTPS POST] response: ");
    Serial.println(response);
  }

  https.end();
}

// utility functions

// makes a unique session ID
String generateSessionId() {
  return "session_" + String(millis());
}

// converts posture status into score
int statusToScore(const String& status) {
  if (status == "GOOD") return 90;
  if (status == "OKAY") return 60;
  if (status == "BAD") return 30;
  return 0;
}

// percentage of good posture
void updateGoodPercentage() {
  if (sessionState.totalReadings == 0) {
    sessionState.goodPercentage = 0;
  } else {
    sessionState.goodPercentage =
      (sessionState.goodCount * 100) / sessionState.totalReadings;
  }
}

// add new posture reading to timeline
void addTimelinePoint(const String& status, int score) {
  if (timelineCount < MAX_TIMELINE_POINTS) {
    timeline[timelineCount].timestamp = millis();
    timeline[timelineCount].status = status;
    timeline[timelineCount].score = score;
    timelineCount++;
  }
}

// control LED and buzzer based on posture
// bad = buzz + LED, okay = LED only, good = green LED only
void applyOutputFeedback(const String& status) {
  digitalWrite(REDled, LOW);
  digitalWrite(YELLOWled, LOW);
  digitalWrite(GREENled, LOW);

  if (status == "BAD") {
    digitalWrite(REDled, HIGH);

    unsigned long currentMillis = millis();
    if (currentMillis - lastBuzzToggle >= 600) {
      lastBuzzToggle = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState ? HIGH : LOW);
    }
  } else if (status == "OKAY") {
    digitalWrite(YELLOWled, HIGH);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  } else if (status == "GOOD") {
    digitalWrite(GREENled, HIGH);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  } else {
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  }
}

// resets for a new session
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

// ends current session
void setLiveInactive() {
  liveState.sessionId = "";
  liveState.status = "INACTIVE";
  liveState.score = 0;
  liveState.updatedAt = millis();
  liveState.durationSeconds = 0;
}

void endCurrentSession() {
  if (!controlState.active) return;

  sessionState.endedAt = millis();
  controlState.active = false;
  setLiveInactive();

  digitalWrite(REDled, LOW);
  digitalWrite(YELLOWled, LOW);
  digitalWrite(GREENled, LOW);
  digitalWrite(buzzerPin, LOW);
  buzzerState = false;
}

// shared session logic
void startSession() {
  String newSessionId = generateSessionId();
  Serial.print("[SESSION] Starting new session: ");
  Serial.println(newSessionId);

  resetSessionStats(newSessionId);

  uploadControlToFirebase();
  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();
}

void stopSession() {
  Serial.println("[SESSION] Stopping current session");
  endCurrentSession();

  uploadControlToFirebase();
  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();
}

// firebase upload functions
void uploadControlToFirebase() {
  String json =
    "{"
    "\"active\":" + String(controlState.active ? "true" : "false") + ","
    "\"sessionId\":\"" + controlState.sessionId + "\","
    "\"startedAt\":" + String(controlState.startedAt) +
    "}";

  Serial.println("[FIREBASE] Uploading /control");
  httpsPUT("/control", json);
}

void uploadLiveToFirebase() {
  String json =
    "{"
    "\"sessionId\":\"" + liveState.sessionId + "\","
    "\"status\":\"" + liveState.status + "\","
    "\"score\":" + String(liveState.score) + ","
    "\"updatedAt\":" + String(liveState.updatedAt) + ","
    "\"durationSeconds\":" + String(liveState.durationSeconds) +
    "}";

  Serial.println("[FIREBASE] Uploading /live");
  httpsPUT("/live", json);
}

void uploadSessionSummaryToFirebase() {
  if (controlState.sessionId == "") return;

  String json =
    "{"
    "\"startedAt\":" + String(sessionState.startedAt) + ","
    "\"endedAt\":" + String(sessionState.endedAt) + ","
    "\"latestStatus\":\"" + sessionState.latestStatus + "\","
    "\"goodCount\":" + String(sessionState.goodCount) + ","
    "\"okayCount\":" + String(sessionState.okayCount) + ","
    "\"badCount\":" + String(sessionState.badCount) + ","
    "\"totalReadings\":" + String(sessionState.totalReadings) + ","
    "\"goodPercentage\":" + String(sessionState.goodPercentage) +
    "}";

  Serial.print("[FIREBASE] Uploading /sessions_live/");
  Serial.println(controlState.sessionId);
  httpsPUT("/sessions_live/" + controlState.sessionId, json);
}

void recordPostureStatus(const String& status) {
  if (!controlState.active) {
    Serial.println("[STATUS] Ignored because no session is active");
    return;
  }

  int score = statusToScore(status);

  liveState.sessionId = controlState.sessionId;
  liveState.status = status;
  liveState.score = score;
  liveState.updatedAt = millis();
  liveState.durationSeconds = (millis() - sessionState.startedAt) / 1000;

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

  pendingLiveUpload = true;
  pendingSessionUpload = true;

  Serial.print("[STATUS] Saved locally: ");
  Serial.print(status);
  Serial.print(" | score=");
  Serial.print(score);
  Serial.print(" | durationSeconds=");
  Serial.print(liveState.durationSeconds);
  Serial.print(" | totalReadings=");
  Serial.print(sessionState.totalReadings);
  Serial.print(" | good%=");
  Serial.println(sessionState.goodPercentage);
}

// server routes
// CORS support
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
}

// test route - shows esp is running
void handleRoot() {
  addCORSHeaders();
  Serial.println("[HTTP] GET /");
  server.send(200, "text/plain", "Output ESP running");
}

// starts new session, resets everything
void handleStart() {
  addCORSHeaders();
  Serial.println("[HTTP] GET /start");
  startSession();
  server.send(200, "text/plain", "Session started");
}

// ends session, stops led/buzzer
void handleEnd() {
  addCORSHeaders();
  Serial.println("[HTTP] GET /end");
  stopSession();
  server.send(200, "text/plain", "Session ended");
}

void handleStatusUpdate() {
  addCORSHeaders();
  Serial.println("[HTTP] GET /status");

  if (!server.hasArg("value")) {
    Serial.println("[HTTP] Missing value");
    server.send(400, "text/plain", "Missing value");
    return;
  }

  String status = server.arg("value");
  status.toUpperCase();

  lastReceivedStatus = status;
  lastStatusRequestMs = millis();

  Serial.print("[HTTP] Received value: ");
  Serial.println(status);

  // update local state immediately
  recordPostureStatus(status);

  // send response back fast so input does not time out
  Serial.println("[HTTP] Sending OK back to input");
  server.send(200, "text/plain", "OK");
}

// setup & loop
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("[SETUP] Output booting...");

  pinMode(REDled, OUTPUT);
  pinMode(GREENled, OUTPUT);
  pinMode(YELLOWled, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  digitalWrite(REDled, LOW);
  digitalWrite(GREENled, LOW);
  digitalWrite(YELLOWled, LOW);
  digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_AP_STA);
  Serial.println("[SETUP] WIFI_AP_STA mode enabled");

  // start hotspot first
  WiFi.softAP(apSSID, apPassword);
  Serial.print("[SETUP] AP started, AP IP: ");
  Serial.println(WiFi.softAPIP());

  // start server immediately
  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/end", handleEnd);
  server.on("/status", handleStatusUpdate);

  server.onNotFound([]() {
    Serial.print("[HTTP] Not found: ");
    Serial.println(server.uri());
    server.send(404, "text/plain", "Not found");
  });

  setLiveInactive();
  server.begin();
  Serial.println("[SETUP] Server started");

  // then try internet
  WiFi.begin(internetSSID, internetPassword);
  Serial.print("[SETUP] Connecting to internet");

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    tries++;
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[SETUP] Internet connected, STA IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[SETUP] Internet did not connect, but AP/server are still running");
  }
}

void loop() {
  // keep server running & listening
  bool currentButtonState = digitalRead(buttonPin);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(150);

    if (controlState.active) {
      Serial.println("[BUTTON] Press detected: ending session");
      stopSession();
    } else {
      Serial.println("[BUTTON] Press detected: starting session");
      startSession();
    }

    while (digitalRead(buttonPin) == LOW) {
      server.handleClient();
      delay(10);
    }
  }

  lastButtonState = currentButtonState;

  server.handleClient();

  // do firebase uploads AFTER responding to the input request (so that if there is a problem with internet connectivity, user can still get live ratings from the input uC)
  if (pendingLiveUpload) {
    Serial.println("[LOOP] Uploading pending live data...");
    uploadLiveToFirebase();
    pendingLiveUpload = false;
    Serial.println("[LOOP] Live data upload done");
  }

  if (pendingSessionUpload) {
    Serial.println("[LOOP] Uploading pending session summary...");
    uploadSessionSummaryToFirebase();
    pendingSessionUpload = false;
    Serial.println("[LOOP] Session summary upload done");
  }
}