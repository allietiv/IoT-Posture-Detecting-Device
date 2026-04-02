#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#define sensorPin 26

const char* ssid = "PostureESP";
const char* password = "12345678";
const char* outputServer = "http://192.168.4.1/status?value=";

Adafruit_MPU6050 mpu;

const int buttonPin = 2; 
int buttonState = 0;
float baseAccX = 0;
float baseAccY = 0;
float baseAccZ = 0;
float currAccX = 0;
float currAccY = 0;
float currAccZ = 0;
float baseGyroX = 0;
float baseGyroY = 0;
float baseGyroZ = 0;
float currGyroX = 0;
float currGyroY = 0;
float currGyroZ = 0;
float totDiff= 0;
float currFlex = 0;
float baseFlex = 0;
sensors_event_t a,g,temp;
int goodThreshold = 5;
int okayThreshold = 10;
float VCC = 3.3; //corresponds to amount of volts
float R2 = 10000; // 10K resistor
float sensorMinRes = 6800; //might need adjustment to find value when flat
float sensorMaxRes = 9200; //might need adjustmentto find value when at 90 degrees


unsigned long lastSendTime = 0;
const unsigned long sendInterval = 2000;   // resend every 2 sec even if same status
const unsigned long readDelay = 250;       // sensor read speed
String lastStatus = "";

String classifyPosture(float totVals) { //if 0 == good, 1 == ok, 2==bad
  Serial.print("totvals: ");
  Serial.println(totVals);
  if (totVals <= 0.5) {
    return "GOOD";
  } else if (totVals <= 1.5) {
    return "OKAY";
  } else {
    return "BAD";
  }
}

int classifyPostureINT(float sensorValue, int gryoFlex) { //second val to signal if gyro or flex classification
  Serial.print("snesVal: ");
  Serial.println(sensorValue);
  if (sensorValue <= goodThreshold) {
    return 0;
  } else if (sensorValue <= okayThreshold) {
    return 1;
  } else {
    return 2;
  }
}

void baseline(){
  baseAccX = a.acceleration.x;
  baseAccY = a.acceleration.y;
  baseAccZ = a.acceleration.z;
  baseGyroX = g.gyro.x;
  baseGyroY = g.gyro.y;
  baseGyroZ = g.gyro.z;
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

void setup() {
  Serial.begin(115200);
  // pinMode(groundPin, OUTPUT);
  // pinMode(powerPin, OUTPUT);
  // digitalWrite(groundPin, LOW);
  // digitalWrite(powerPin, HIGH);
  //connectToOutputESP();
  Wire.begin(21, 22);
  if (!mpu.begin()){
    Serial.println("Failed to find mpu6050");
    delay(500);
  }
  Serial.print("mac: ");
  Serial.println(WiFi.macAddress());
  pinMode(buttonPin, INPUT);
  pinMode(sensorPin, INPUT);
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); //can be changed to higher g for more range of motion.
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ); //can be changed to higher hz for faster response

}

void loop() {
  String currStatGyro = "";
  String currStatFlex = "";
  String currStat = "";
  int currStatGyroINT = 0;
  int currStatFlexINT = 0;
  analogReadResolution(12);           // ESP32: set 12-bit resolution
  analogSetAttenuation(ADC_11db);  
  int ADCRaw = analogRead(sensorPin);
  Serial.print("ADCRAW: ");
  Serial.println(ADCRaw);
  mpu.getEvent(&a,&g,&temp);
  currAccX = a.acceleration.x;
  currAccY = a.acceleration.y;
  currAccZ = a.acceleration.z;
  currGyroX = g.gyro.x;
  currGyroY = g.gyro.y;
  currGyroZ = g.gyro.z;
  float ADCVoltage = (ADCRaw * VCC) / 4095;
  float Resistance = R2 * (VCC / ADCVoltage - 1);
  Serial.print("resistance: ");
  Serial.println(Resistance);
  currFlex = (Resistance - sensorMinRes) * 100.0 / (sensorMaxRes - sensorMinRes); //value between 0-100
  Serial.print("curflex: ");
  Serial.println(currFlex);
  Serial.println(a.acceleration.x);
  Serial.println(a.acceleration.y);
  Serial.println(a.acceleration.z);
  Serial.print("X: ");
  Serial.println(g.gyro.x);
  Serial.print("Y: ");
  Serial.println(g.gyro.y);
  Serial.print("Z: ");
  Serial.println(g.gyro.z);
  delay(10);
  buttonState = digitalRead(buttonPin);
  if (buttonState == LOW){
    baseline();
  }
  //Serial.println("moved");
  unsigned long now = millis();
  bool refreshNeeded = (now - lastSendTime >= sendInterval);
  //if accel notices movement, it checks with gyro to check for orientation diffs
  if ((fabs(currAccX - baseAccX) > 0.1) || (fabs(currAccY - baseAccY) > 0.1) || (fabs(currAccZ - baseAccZ) > 0.1) || refreshNeeded){ //for +/- errors
      Serial.print("X in: ");
      Serial.println(g.gyro.x);
      Serial.print("Y in : ");
      Serial.println(g.gyro.y);
      Serial.print("Z in: ");
      Serial.println(g.gyro.z);
      totDiff = fabs(currGyroX - baseGyroX) + fabs(currGyroY - baseGyroY) + fabs(currGyroZ - baseGyroZ); //values for good/great might need scaling
      Serial.print("totDiff: ");
      Serial.println(totDiff);
      currStatGyroINT = classifyPostureINT(totDiff, 0);
      Serial.println("moved");
  }
  else{
    Serial.println("no change in accel/gyro");
  }
  Serial.print("curr-b flex: ");
  Serial.println(currFlex-baseFlex);
  if (fabs(currFlex-baseFlex) > 2){//again the "2" might need changing
        currStatFlexINT = classifyPostureINT(fabs(currFlex-baseFlex), 1);
  }

  Serial.println((currStatGyroINT + currStatFlexINT)/2);
  currStat = classifyPosture((currStatGyroINT + currStatFlexINT)/2);
  sendStatusToOutput(currStat);
  Serial.println(currStat);
  lastStatus = currStat;
  lastSendTime = now;
  delay(2000); 
}
