input(one with sensor):
#include <WiFi.h>

const char* ssid = "PostureESP";
const char* password = "12345678";

const char* serverIP = "192.168.4.1";
const int serverPort = 80;

const int pressurePin = 34;

WiFiClient client;

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("Connecting to PostureESP...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Connected!");
}

void loop() {
  int pressureValue = analogRead(pressurePin);

  Serial.print("Pressure: ");
  Serial.println(pressureValue);

  String posture;

  // adjust if needed after testing
  if (pressureValue < 1000) {
    posture = "BAD";
  } else {
    posture = "GOOD";
  }

  if (client.connect(serverIP, serverPort)) {
    client.println(posture);
    client.stop();

    Serial.print("Sent: ");
    Serial.println(posture);
  } else {
    Serial.println("Connection failed");
  }

  delay(500);
}
