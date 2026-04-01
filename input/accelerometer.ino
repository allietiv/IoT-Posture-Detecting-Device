#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

// const int groundPin = ;
// const int powerPin = ; //vcc
// const int xpin = #;
// const int ypin = #;
// const int zpin = #;
   const int buttonPin = 2; 
   const int ledPin = 13;
   int buttonState = 0;
    int basex = 0;
    int basey = 0;
    int basez = 0;
    int currx = 0;
    int curry = 0;
    int currz = 0;
    sensors_event_t a,g,temp;

void setup() {
  Serial.begin(115200);
  // pinMode(groundPin, OUTPUT);
  // pinMode(powerPin, OUTPUT);
  // digitalWrite(groundPin, LOW);
  // digitalWrite(powerPin, HIGH);
  if (!mpu.begin()){
    Serial.println("Failed to find mpu6050");
  }
  pinMode(buttonPin, INPUT);
  pinMode(ledPin, OUTPUT);
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); //can be changed to higher g for more range of motion.
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ); //can be changed to higher hz for faster response

}

void loop() {
  mpu.getEvent(&a,&g,&temp);
  currx = a.acceleration.x;
  curry = a.acceleration.y;
  currz = a.acceleration.z;
  Serial.println(a.acceleration.x);
  Serial.println(a.acceleration.y);
  Serial.println(a.acceleration.z);
  delay(10);
  buttonState = digitalRead(buttonPin);
  if (buttonState == LOW){
    baseline();
  }
  Serial.println("moved");
  if ((currx - basex > 0.1) || (curry - basey > 0.1) || (currz - basez > 0.1)){ //for +/- errors
      digitalWrite(ledPin, HIGH);
      Serial.println("moved");
  }
  else{
    digitalWrite(ledPin, LOW);
    Serial.println("no change");
  }
}

void baseline(){
  basex = a.acceleration.x;
  basey = a.acceleration.y;
  basez = a.acceleration.z;
}