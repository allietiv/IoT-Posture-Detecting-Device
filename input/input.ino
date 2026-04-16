// input.ino - tracks and analyzes accelerometer and flex sensor data
// DEMO DAY INPUT CODE

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

posture_input4-14.ino
9 KB
﻿
// input.ino - tracks and analyzes accelerometer and flex sensor data
// DEMO DAY INPUT CODE

#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#define sensorPin 34
#define buttonPin 0

// wi-fi credentials and output server URL
const char* ssid = "PostureESP";
const char* password = "12345678";
const char* outputServer = "http://192.168.4.1/status?value=";

Adafruit_MPU6050 mpu;

// PIN ASSIGNMENTS
//const int buttonPin = 2;  // reset button
int buttonState = 0;

// baseline readings for accelerometer
float baseAccX = 0;
float baseAccY = 0;
float baseAccZ = 0;
// current readings for accelerometer
float currAccX = 0;
float currAccY = 0;
float currAccZ = 0;

// baseline readings for gyroscope
float baseGyroX = 0;
float baseGyroY = 0;
float baseGyroZ = 0;
// current readings for gyroscope
float currGyroX = 0;
float currGyroY = 0;
float currGyroZ = 0;
// sum of gyro diffs
float totDiff= 0;

// baseline and current readings for flex sensor
float currFlex = 0;
float baseFlex = 0;

// a = acceleration, g = gyroscope, temp = temperature
sensors_event_t a,g,temp;

// thresholds for sensors
float goodThreshold = 10;
float okayThreshold = 20;
float VCC = 3.3; //corresponds to amount of volts
float R2 = 10000; // 10K resistor
float sensorMinRes = 1500; //might need adjustment to find value when flat
float sensorMaxRes = 3500; //might need adjustmentto find value when at 90 degrees

// timing variables for sending data to output ESP
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 30000;   // resend every 2 sec even if same status
const unsigned long readDelay = 250;       // sensor read speed
String lastStatus = "";

// FUNCTION - takes averaged score from gyro + flex -> GOOD, OKAY, BAD
String classifyPosture(float totVals) { //if 0 == good, 1 == ok, 2==bad
  Serial.print("totvals: ");
  Serial.println(totVals);
  if (totVals < 1) {
    return "GOOD";
  } else if (totVals < 2) {
    return "OKAY";
  } else {
    return "BAD";
  }
}
//.20
//.15
// FUNCTION - takes individual sensor value and classifies as good/ok/bad, returns int for easier averaging
int classifyPostureINT(float sensorValue, int gyroFlex) { //second val to signal if gyro or flex classification
  Serial.print("snesVal: ");
  Serial.println(sensorValue);
  if (!gyroFlex){
    Serial.print("Sensor Gyro: ");
    Serial.print(sensorValue);
    Serial.print(" "),
    Serial.println(goodThreshold/100);
    Serial.println(okayThreshold/100);
    if (sensorValue <= goodThreshold/100) {
      return 0;
    } else if (sensorValue <= okayThreshold/100) {
      return 1;
    } else {
      return 2;
    }
  }
  else{
    if (sensorValue <= goodThreshold) {
    return 0;
  } else if (sensorValue <= okayThreshold) {
    return 1;
  } else {
    return 2;
  }
  }
}

// FUNCTION - sets baseline values for accelerometer and gyroscope
void baseline(){
  Serial.println("in baseline");
  baseAccX  = currAccX;
  baseAccY = currAccY;
  baseAccZ = currAccZ;
  baseGyroX = currGyroX;
  baseGyroY = currGyroY;
  baseGyroZ = currGyroZ;
  baseFlex = currFlex;

}

// FUNCTION - joins the WiFi network of the output ESP
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

// FUNCTION - HTTP GET request to send posture status to output ESP
void sendStatusToOutput(const String& status) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected. Reconnecting...");
    connectToOutputESP();
  }

  HTTPClient http;
  String url = String(outputServer) + status;

  http.begin(url);
  int httpCode = http.GET();  // send GET request

  Serial.print("Sent status: ");
  Serial.print(status);
  Serial.print(" | HTTP response: ");
  Serial.println(httpCode);

  if (httpCode > 0) {                    // successful response              
    String payload = http.getString();
    Serial.println("Response:");
    Serial.println(payload);
  } else {                              // error in sending request
    Serial.println("Failed to send request");
  }

  http.end(); // free resources used by HTTPClient
}

// setup function runs at startup / reset
void setup() {
  Serial.begin(115200);
  // pinMode(groundPin, OUTPUT);
  // pinMode(powerPin, OUTPUT);
  // digitalWrite(groundPin, LOW);
  // digitalWrite(powerPin, HIGH);
  //connectToOutputESP();

  // start I2C on SDA=21, SCL=22
  Wire.begin(21, 22);
  //initialize MPU6050, retry until found
  if (!mpu.begin()){
    Serial.println("Failed to find mpu6050");
    delay(500);
  }

  Serial.print("mac: ");
  Serial.println(WiFi.macAddress());

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(sensorPin, INPUT);

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); //can be changed to higher g for more range of motion.
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ); //can be changed to higher hz for faster response

}

// main loop, runs repeatedly after setup
void loop() {
  // variables to hold current status and sensor values
  String currStatGyro = "";
  String currStatFlex = "";
  String currStat = "";
  int currStatGyroINT = 0;
  int currStatFlexINT = 0;

  // read flex sensor w/ ADC
  analogReadResolution(12);           // ESP32: set 12-bit resolution
  analogSetAttenuation(ADC_11db);  

  int ADCRaw = analogRead(sensorPin);
  Serial.print("ADCRAW: ");
  Serial.println(ADCRaw);

  // read MPU
  mpu.getEvent(&a,&g,&temp);
  currAccX = a.acceleration.x;
  currAccY = a.acceleration.y;
  currAccZ = a.acceleration.z;
  currGyroX = g.gyro.x;
  currGyroY = g.gyro.y;
  currGyroZ = g.gyro.z;

  // convert raw ADC -> 0-100 flex percentage
  float ADCVoltage = (ADCRaw * VCC) / 4095;
  Serial.println(ADCVoltage);
  float Resistance = R2 * (ADCVoltage / (VCC - ADCVoltage));

  Serial.print("resistance: ");
  Serial.println(Resistance);

  // map resistance range
  currFlex = (Resistance - sensorMinRes) * 100.0 / (sensorMaxRes - sensorMinRes); //value between 0-100
  Serial.print("curflex: ");
  Serial.println(currFlex);

  // debug for accel and gyro values
  Serial.println(currAccX- baseAccX);
  Serial.println(currAccY - baseAccY);
  Serial.println(currAccZ - baseAccZ);
  Serial.print("X: ");
  Serial.println(g.gyro.x);
  Serial.print("Y: ");
  Serial.println(g.gyro.y);
  Serial.print("Z: ");
  Serial.println(g.gyro.z);

  delay(10);

  // reset baseline if button is pressed
  buttonState = digitalRead(buttonPin);
  if (buttonState == LOW){
    baseline();
  }
  unsigned long now = millis();
  //Serial.println("moved");

  //bool refreshNeeded = (now - lastSendTime >= sendInterval);
  //if accel notices movement, it checks with gyro to check for orientation diffs
  if (fabs(currAccX - baseAccX) > 0.1 || fabs(currAccY - baseAccY) > 0.1 ||  fabs(currAccZ - baseAccZ) > 0.1){ //for +/- errors
      Serial.print("X in: ");
      Serial.println(g.gyro.x);
      Serial.print("Y in : ");
      Serial.println(g.gyro.y);
      Serial.print("Z in: ");
      Serial.println(g.gyro.z);
      totDiff = fabs(currGyroX - baseGyroX) + fabs(currGyroY - baseGyroY) + fabs(currGyroZ - baseGyroZ); //values for good/great might need scaling
      Serial.print("totDiff: ");
      Serial.println(totDiff);
      Serial.println("moved");
      if (totDiff > 0.05) {
      currStatGyroINT = classifyPostureINT(totDiff - 0.05 , 0);
      Serial.print("gyro state: ");
      Serial.println(currStatGyroINT);

      }
  }
  else{
    Serial.println("no change in accel/gyro");
  }
  Serial.print("curr-b flex: ");
  Serial.println(currFlex-baseFlex);
  if (fabs(currFlex-baseFlex) > 4){//again the "2" might need changing
        currStatFlexINT = classifyPostureINT(fabs(currFlex-baseFlex) - 4, 1); //compares diff minus (+/-)
        Serial.print("flex state: ");
        Serial.println(currStatFlexINT);
  }

  // average gyro and flex classifications for overall posture status
  Serial.println((currStatGyroINT + currStatFlexINT)/2);
  bool refreshNeeded = (now - lastSendTime >= sendInterval);
  String classification = classifyPosture((currStatGyroINT + currStatFlexINT)/2);
  Serial.println(classification);
  Serial.println(lastStatus);
  if (refreshNeeded || classification != lastStatus ){
      lastStatus = classification; //changes last status to curr status
      sendStatusToOutput(lastStatus); //sends curr status
      lastSendTime = now;
  }
  Serial.println(currStat);
  delay(2000); 
}