// output/reading2/reading2.ino - LED andbuzzer

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>

// Device and Wifi credentials
const char* apSSID = "PostureESP";
const char* apPassword = "12345678";


const char* internetSSID = "dise";
const char* internetPassword = "esp32meow";

// Firebase Realtime Database URL
const String firebaseBase = "https://iot-posture-monitoring-default-rtdb.firebaseio.com";

// local HTTP server on port 80
WebServer server(80);

const int ledPin = 26;    // warning LED pin (okay/bad status)
const int buzzerPin = 27; // audible buzzer pin (bad status)
const int buttonPin = 14;
bool lastButtonState = HIGH;

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
}

// shared session logic
void startSession() {
  String newSessionId = generateSessionId();
  resetSessionStats(newSessionId);

  uploadControlToFirebase();
  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();
}

void stopSession() {
  endCurrentSession();

  digitalWrite(ledPin, LOW);
  digitalWrite(buzzerPin, LOW);
  buzzerState = false;

  uploadControlToFirebase();
  uploadLiveToFirebase();
  uploadSessionSummaryToFirebase();
}

//firebase upload functions
void uploadControlToFirebase() {
  String json =
    "{"
    "\"active\":" + String(controlState.active ? "true" : "false") + ","
    "\"sessionId\":\"" + controlState.sessionId + "\","
    "\"startedAt\":" + String(controlState.startedAt) +
    "}";

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

  httpsPUT("/sessions_live/" + controlState.sessionId, json);
}

void recordPostureStatus(const String& status) {
  if (!controlState.active) return;

  int score = statusToScore(status);

  liveState.status = status;
  liveState.score = score;

  uploadLiveToFirebase();
}

//server routes 
// CORS support
void addCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
}

//test route - shows esp is running
void handleRoot() {
  addCORSHeaders();
  server.send(200, "text/plain", "Output ESP running");
}

//starts new session, resets everything
void handleStart() {
  addCORSHeaders();
  startSession();
  server.send(200, "text/plain", "Session started");
}

//ends session, stops led/buzzer
void handleEnd() {
  addCORSHeaders();
  stopSession();
  server.send(200, "text/plain", "Session ended");
}

void handleStatusUpdate() {
  addCORSHeaders();

  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }

  String status = server.arg("value");
  status.toUpperCase();

  recordPostureStatus(status);
  server.send(200, "text/plain", "OK");
}


//setup & loop

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledPin, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  WiFi.begin(internetSSID, internetPassword);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.println(WiFi.localIP());
  //server routes 

  server.on("/", handleRoot);
  server.on("/start", handleStart);
  server.on("/end", handleEnd);
  server.on("/status", handleStatusUpdate);

  setLiveInactive();
  server.begin();
}

void loop() {
  //keep server running & listening 
  bool currentButtonState = digitalRead(buttonPin);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    delay(150);

    if (controlState.active) {
      Serial.println("Button pressed: ending session");
      stopSession();
    } else {
      Serial.println("Button pressed: starting session");
      startSession();
    }

    while (digitalRead(buttonPin) == LOW) {
      delay(10);
    }
  }

  lastButtonState = currentButtonState;
  server.handleClient();
}
