// output/reading2/reading2.ino - LED andbuzzer

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

const int ledPin = 26;    // warning LED pin (okay/bad status)
const int buzzerPin = 27; // audible buzzer pin (bad status)

unsigned long lastBuzzToggle = 0;
bool buzzerState = false;


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

//stores stats for whole session 
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

//timeline tracking 
const int MAX_TIMELINE_POINTS = 120;

//each point stores time + posture info 
struct TimelinePoint {
  unsigned long timestamp = 0;
  String status = "UNKNOWN";
  int score = 0;
};
//array to store timeline history 
TimelinePoint timeline[MAX_TIMELINE_POINTS];
int timelineCount = 0;

ControlState controlState;
LiveState liveState;
SessionLiveState sessionState;

//firebase helpers

//get request from firebase, creates secure client & sends request
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

//put request - sends json to firebase
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

//patch request - updates data
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
//post request- adds new data 
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

//utility functions

//makes a unique session ID
String generateSessionId() {
  return "session_" + String(millis());
}
//converts posture status into score
int statusToScore(const String& status) {
  if (status == "GOOD") return 90;
  if (status == "OKAY") return 60;
  if (status == "BAD") return 30;
  return 0;
}
//percentage of good posture
void updateGoodPercentage() {
  if (sessionState.totalReadings == 0) {
    sessionState.goodPercentage = 0;
  } else {
    sessionState.goodPercentage =
      (sessionState.goodCount * 100) / sessionState.totalReadings;
  }
}
//add new posture reading to timeline
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
//control LED and buzzer based on posture
//bad = buzz + LED, okay = LED only, good = nothing
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
    digitalWrite(ledPin, HIGH);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  } else {
    digitalWrite(ledPin, LOW);
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  }
}
//resets for a new session
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
//ends current session
void endCurrentSession() {
  if (!controlState.active) return;
  sessionState.endedAt = millis();
  controlState.active = false;
}

//firebase upload functions
//uploads session control info
void uploadControlToFirebase() {
  String json =
    "{"
    "\"active\":" + String(controlState.active ? "true" : "false") + ","
    "\"sessionId\":\"" + controlState.sessionId + "\","
    "\"startedAt\":" + String(controlState.startedAt) +
    "}";

  httpsPUT("/control", json);
}
//uploads live posture data
void uploadLiveToFirebase() {
  String json =
    "{"
    "\"sessionId\":\"" + liveState.sessionId + "\","
    "\"status\":\"" + liveState.status + "\","
    "\"score\":" + String(liveState.score) + ","
    "\"updatedAt\":" + String(liveState.updatedAt) + ","
    "\"durationSeconds\":" + String(liveState.durationSeconds) +
    "}";

  httpsPUT("/live", json);
}
//uploads session summary stats
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

  httpsPUT("/sessions_live/" + controlState.sessionId, json);
}
//upload one timeline event
void uploadTimelineEventToFirebase(const String& status, int score) {
  if (controlState.sessionId == "") return;

  String json =
    "{"
    "\"timestamp\":" + String(millis()) + ","
    "\"status\":\"" + status + "\","
    "\"score\":" + String(score) +
    "}";

  httpsPOST("/sessions_live/" + controlState.sessionId + "/timeline", json);
}
//called when new posture data comes in
//starts session if needed, updates counts+stats, sends to firebase
void recordPostureStatus(const String& status) {
  if (!controlState.active) {
    String newSessionId = generateSessionId();
    resetSessionStats(newSessionId);
    uploadControlToFirebase();
    uploadSessionSummaryToFirebase();
  }

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

  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();
  uploadTimelineEventToFirebase(status, score);

  Serial.print("Uploaded to Firebase: ");
  Serial.println(status);
}

//server routes 

//test route - shows esp is running
void handleRoot() {
  server.send(200, "text/plain", "Output ESP running");
}

//starts new session, resets everything
void handleStart() {
  String newSessionId = generateSessionId();
  resetSessionStats(newSessionId);

  uploadControlToFirebase();
  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();

  server.send(200, "text/plain", "Session started");
}

//ends session, stops led/buzzer
void handleEnd() {
  endCurrentSession();

  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);
  buzzerState = false;

  uploadControlToFirebase();
  uploadSessionSummaryToFirebase();

  server.send(200, "text/plain", "Session ended");
}
//receives posture from input esp
void handleStatusUpdate() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }

  String status = server.arg("value");
  status.trim();
  status.toUpperCase();

  if (status != "GOOD" && status != "OKAY" && status != "BAD") {
    server.send(400, "text/plain", "Status must be GOOD, OKAY, or BAD");
    return;
  }

  recordPostureStatus(status);
  server.send(200, "text/plain", "OK");
}

//setup & loop


void setup() {
  Serial.begin(115200);
  delay(1000);
  //pins
  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);

  WiFi.mode(WIFI_AP_STA);
  //hotspot
  WiFi.softAP(apSSID, apPassword);
  Serial.println("AP started");
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  //connect to internet 
  WiFi.begin(internetSSID, internetPassword);
  Serial.print("Connecting to internet");
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() != WL_CONNECTED){
    Serial.println("skip");
  }
  Serial.println();
  Serial.println("Connected to internet");
  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());
  //server routes 
  server.on("/", HTTP_GET, handleRoot);
  server.on("/start", HTTP_GET, handleStart);
  server.on("/end", HTTP_GET, handleEnd);
  server.on("/status", HTTP_GET, handleStatusUpdate);
  //start server
  server.begin();
  Serial.println("Server started");
}

void loop() {
  //keep server running & listening 
  server.handleClient();
}