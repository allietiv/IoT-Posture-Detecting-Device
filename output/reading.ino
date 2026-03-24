#include <WiFi.h>

const char* ssid = "PostureESP";
const char* password = "12345678";

WiFiServer server(80);

const int ledPin = 26;
const int buzzerPin = 27;

unsigned long lastBuzzTime = 0;
bool buzzerState = false;

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

  server.begin();
  Serial.println("Server started");
}

void loop() {
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
        Serial.print("Received: ");
        Serial.println(message);

        if (message == "BAD") {
          digitalWrite(ledPin, HIGH);

          // softer pulsing buzzer (not constant annoying sound)
          unsigned long currentMillis = millis();
          if (currentMillis - lastBuzzTime > 600) {
            lastBuzzTime = currentMillis;
            buzzerState = !buzzerState;
            digitalWrite(buzzerPin, buzzerState);
          }
        } 
        else if (message == "GOOD") {
          digitalWrite(ledPin, LOW);
          digitalWrite(buzzerPin, LOW);
          buzzerState = false;
        }

        break;
      }
    }

    client.stop();
  }
}