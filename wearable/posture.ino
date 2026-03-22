const int sensorPin = 34;
const int ledPin = 25;

int threshold = 800;  // adjust if needed

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  delay(1000);
  Serial.println("Posture + LED test");
}

void loop() {
  int sensorValue = analogRead(sensorPin);

  Serial.print("Sensor: ");
  Serial.print(sensorValue);

  if (sensorValue < threshold) {
    Serial.println("  --> BAD POSTURE");
    digitalWrite(ledPin, HIGH);  // turn LED ON
  } else {
    Serial.println("  --> GOOD POSTURE");
    digitalWrite(ledPin, LOW);   // turn LED OFF
  }

  delay(200);
}