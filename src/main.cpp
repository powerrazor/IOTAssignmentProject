#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

int ledPinR = 13;
int ledPinY = 26;
int ledPinG = 25;
int pirPin = 34;
Servo myServo;
int servoPin = 14;

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* api_url = "https://api.open-meteo.com/v1/forecast?latitude=-28.0167&longitude=153.4000&current=temperature_2m,wind_speed_10m,relative_humidity_2m&timezone=auto";

// Timing and state variables for the non-blocking traffic light
unsigned long previousMillis = 0;
unsigned long currentInterval = 6000; // Starts with Red light delay
int trafficState = 0; // 0 = Red, 1 = Green, 2 = Yellow


bool pedestrianDetected = false;
bool barrierLowered = false;

void fetchWeatherData() {
  HTTPClient http;
  http.begin(api_url);
  int httpResponseCode = http.GET();

  if (httpResponseCode > 0) {
    String payload = http.getString();
    JsonDocument doc; 
    deserializeJson(doc, payload);
    Serial.println("Gold Coast Weather Data:");
    float temperature = doc["current"]["temperature_2m"];
    float windSpeed = doc["current"]["wind_speed_10m"];
    float humidity = doc["current"]["relative_humidity_2m"];
    Serial.print("Time: ");
    Serial.println(doc["current"]["time"].as<String>());
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    Serial.print("Wind Speed: ");
    Serial.print(windSpeed);
    Serial.println(" m/s");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  delay(100); 
  
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nIP Address: ");
  Serial.println(WiFi.localIP());
  
  pinMode(ledPinY, OUTPUT);
  pinMode(ledPinR, OUTPUT);
  pinMode(ledPinG, OUTPUT);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  myServo.setPeriodHertz(50); 
  myServo.attach(servoPin, 500, 2400);

  // Set initial servo position to close (0)
  myServo.write(0);

  pinMode(pirPin, INPUT);

  digitalWrite(ledPinR, HIGH);
  digitalWrite(ledPinY, LOW);
  digitalWrite(ledPinG, LOW);
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Non-blocking Traffic Light State Machine
  if (currentMillis - previousMillis >= currentInterval) {
    previousMillis = currentMillis;

    if (trafficState == 0) {
      digitalWrite(ledPinR, LOW);
      digitalWrite(ledPinG, HIGH);
      trafficState = 1;
      currentInterval = 8000; 
    } 
    else if (trafficState == 1) {
      digitalWrite(ledPinG, LOW);
      digitalWrite(ledPinY, HIGH);
      trafficState = 2;
      currentInterval = 4000; 
    } 
    else if (trafficState == 2) {
      digitalWrite(ledPinY, LOW);
      digitalWrite(ledPinR, HIGH);
      trafficState = 0;
      currentInterval = 6000; 
      if (barrierLowered) {
        trafficState = 3;
        currentInterval = 0;
      }
    }
    else if (trafficState == 3) {
      while (digitalRead(pirPin) == HIGH){
        delay(1000); // Pause the loop until the pedestrian leaves the detection zone
      }
      myServo.write(0); // Raise barrier back to 0 degrees
      Serial.println("Pedestrian left!");
      pedestrianDetected = false; // Reset the state
      barrierLowered = false; // Reset the barrier state
      trafficState = 0;
      currentInterval = 0; 
    }
  }
  if(digitalRead(pirPin) == HIGH && pedestrianDetected == false) {
    Serial.println("Pedestrian detected!");
    pedestrianDetected = true;
  }
  // 2. PIR Sensor and Servo Handling (Non-blocking)
  // Trigger if motion is detected during Yellow light, and barrier isn't already moving
  if (pedestrianDetected && trafficState == 0 && !barrierLowered) { 
    myServo.write(90); // Drop barrier to 90 degrees
    barrierLowered = true;
  }

  // Check if the barrier has been down for 5 seconds
  /*if ((currentMillis - previousMillis >= currentInterval) && trafficState == 0 && barrierLowered) {
    while (digitalRead(pirPin) == HIGH){
      delay(1000); // Pause the loop until the pedestrian leaves the detection zone
    }
    myServo.write(0); // Raise barrier back to 0 degrees
    Serial.println("Pedestrian left!");
    pedestrianDetected = false; // Reset the state
    barrierLowered = false; // Reset the barrier state
  }*/
}