#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "PostureESP";
const char* password = "12345678";
const char* outputServer = "http://192.168.4.1/status?value=";

const int flexPin = 34;
const int debugLedPin = 25;

// adjust these after testing real sensor values
int goodThreshold = 2600;
int okayThreshold = 2000;

// send rules
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;   // resend every 2 sec even if same status
const unsigned long readDelay = 250;       // sensor read speed

String lastStatus = "";

String classifyPosture(int sensorValue) {
  if (sensorValue >= goodThreshold) {
    return "GOOD";
  } else if (sensorValue >= okayThreshold) {
    return "OKAY";
  } else {
    return "BAD";
  }
}

void connectToOutputESP() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to output ESP");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected to output ESP Wi-Fi");
  Serial.print("Input ESP IP: ");
  Serial.println(WiFi.localIP());
}

void sendStatusToOutput(const String& status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    connectToOutputESP();
  }

  HTTPClient http;
  String url = String(outputServer) + status;

  http.begin(url);
  int httpCode = http.GET();

  Serial.print("Sent status: ");
  Serial.print(status);
  Serial.print(" | HTTP response: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.println("Response:");
    Serial.println(payload);
  } else {
    Serial.println("Failed to send request");
  }

  http.end();
}

void updateDebugLed(const String& status) {
  if (status == "BAD") {
    digitalWrite(debugLedPin, HIGH);
  } else if (status == "OKAY") {
    digitalWrite(debugLedPin, HIGH);
  } else {
    digitalWrite(debugLedPin, LOW);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(debugLedPin, OUTPUT);
  digitalWrite(debugLedPin, LOW);

  Serial.println("Flex sensor posture input starting...");
  connectToOutputESP();
}

void loop() {
  int sensorValue = analogRead(flexPin);
  String currentStatus = classifyPosture(sensorValue);

  Serial.print("Flex reading: ");
  Serial.print(sensorValue);
  Serial.print(" -> ");
  Serial.println(currentStatus);

  updateDebugLed(currentStatus);

  unsigned long now = millis();
  bool statusChanged = (currentStatus != lastStatus);
  bool refreshNeeded = (now - lastSendTime >= sendInterval);

  if (statusChanged || refreshNeeded) {
    sendStatusToOutput(currentStatus);
    lastStatus = currentStatus;
    lastSendTime = now;
  }

  delay(readDelay);
}