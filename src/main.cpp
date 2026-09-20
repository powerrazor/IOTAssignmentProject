#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define DHTTYPE DHT22

int ledPinR = 13;
int ledPinY = 26;
int ledPinG = 25;
int pirPin = 33;

int DHT22Pin = 23;
DHT dht(DHT22Pin, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Servo myServo;
int servoPin = 14;

int trigPin = 19;
int echoPin = 18;

const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* api_url = "https://api.open-meteo.com/v1/forecast?latitude=-28.0167&longitude=153.4000&current=temperature_2m,wind_speed_10m,relative_humidity_2m&timezone=auto";
WiFiClientSecure cloudClient;

// Timing and state variables for the non-blocking traffic light
unsigned long previousMillis = 0;
unsigned long currentInterval = 6000; // Starts with Red light delay
int trafficState = 0; // 0 = Red, 1 = Green, 2 = Yellow

// Timing used for speed check
unsigned long lastSpeedCheckMillis = 0;
unsigned long speedCheckInterval = 100; // Take a reading every 100ms
float lastDistance = -1;

// Timing used for non-blocking warning display
unsigned long lastWarningMillis = 0;
unsigned long WarningInterval = 1000;

bool pedestrianDetected = false;
bool barrierLowered = false;
int speedLimit = 80; // Default speed limit in km/h

struct weatherData {
  float temperature;
  float humidity;
};

weatherData onlineWeather;
float temperature;
float humidity;


// Upload Button Configuration (interupt)
int BUTTON_PIN = 27;
int LED_PIN = 32;

volatile bool buttonPressedStatus = false;
volatile unsigned long lastPressTime = 0;
int ledState = LOW;

void IRAM_ATTR handleButtonInterrupt() {
  if (millis() - lastPressTime > 300) {
    buttonPressedStatus = true;
    lastPressTime = millis();
  }
}

// Upload to ThingSpeak Configuration
const char* THINGSPEAK_WRITE_API_KEY = "";
const char* THINGSPEAK_UPDATE_URL = "https://api.thingspeak.com/update";
const unsigned long UPLOAD_INTERVAL = 20000;
unsigned long lastUpload = 0;
bool hasUploaded = false;

String buildUpdateUrl(const char* apiKey, int trafficState, int speedLimit, float temperature, float humidity) {
  return String(THINGSPEAK_UPDATE_URL) +
         "?api_key=" + apiKey +
         "&field1=" + String(trafficState) +
         "&field2=" + String(speedLimit) +
         "&field3=" + String(temperature, 2) +
         "&field4=" + String(humidity, 2);
}

bool uploadReading(int trafficState, int speedLimit, float temperature, float humidity) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Upload skipped: Wi-Fi is not connected.");
    return false;
  }

  cloudClient.setInsecure();  // Wokwi demo; pin ThingSpeak's CA in production.
  HTTPClient http;
  String url = buildUpdateUrl(THINGSPEAK_WRITE_API_KEY, trafficState, speedLimit, temperature, humidity);

  if (!http.begin(cloudClient, url)) {
    Serial.println("Could not initialise the HTTPS request.");
    return false;
  }

  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  int responseCode = http.GET();
  String entryId = http.getString();
  http.end();

  Serial.print("ThingSpeak HTTP response: ");
  Serial.print(responseCode);
  Serial.print(" | entry id: ");
  Serial.println(entryId);

  // ThingSpeak answers with the new entry id, or "0" when the update was
  // refused (bad key, or faster than the free-tier 15 s rate limit).
  return responseCode == HTTP_CODE_OK && entryId.toInt() > 0;
}

weatherData fetchOnlineWeatherData() {
  Serial.println("Fetching online weather data...");
  HTTPClient http;
  http.begin(api_url);
  int httpResponseCode = http.GET();
  float temperature;
  float humidity;
  if (httpResponseCode > 0) {
    String payload = http.getString();
    JsonDocument doc; 
    deserializeJson(doc, payload);
    temperature = doc["current"]["temperature_2m"];
    humidity = doc["current"]["relative_humidity_2m"];
    Serial.println("Online Weather Data:");
    Serial.print("Temperature: ");
    Serial.print(temperature);
    Serial.println(" °C");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" %");
  } else {
    Serial.print("Error on HTTP request: ");
    Serial.println(httpResponseCode);
    temperature = 0;
    humidity = -1;
  }

  http.end();
  return {temperature, humidity};
}
weatherData getDHT22Readout() {
  // Reading temperature or humidity takes about 250 milliseconds
  // Sensor readings may also be up to 2 seconds 'old'
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature(); // Reads Celsius by default
  // Check if any reads failed and exit early
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT22 sensor!");
    return {0, -1};
  }
  return {temperature, humidity};
}


int newSpeedLimit() {
  weatherData DHT22Weather = getDHT22Readout();

  if (DHT22Weather.humidity == -1 && onlineWeather.humidity == -1) {
    return speedLimit; // Return the last known speed limit if sensor fails
  }
  else if (DHT22Weather.humidity == -1) {
    temperature = onlineWeather.temperature;
    humidity = onlineWeather.humidity;
  }
  else if (onlineWeather.humidity == -1) {
    temperature = DHT22Weather.temperature;
    humidity = DHT22Weather.humidity;
  }
  else {
    temperature = (DHT22Weather.temperature + onlineWeather.temperature) / 2;
    humidity = (DHT22Weather.humidity + onlineWeather.humidity) / 2;
  }
  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  if (temperature <= 0 && humidity > 70) {
    return 40; // Possible icy conditions
  } else if (temperature > 0 && humidity > 70) {
    return 60; // Possible Wet / Foggy conditions
  } else {
    return 80; // Default speed limit
  }
}

void showSpeedLimit(int speed) {
  display.clearDisplay();

  // circular sign outline
  display.drawCircle(64, 32, 30, SSD1306_WHITE);
  display.drawCircle(64, 32, 31, SSD1306_WHITE);
  display.drawCircle(64, 32, 32, SSD1306_WHITE);

  // Print speed number
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  
  // Center alignment offset
  if (speed < 100) {
    display.setCursor(48, 22);
  } else {
    display.setCursor(40, 22);
  }

  display.print(speed);

  display.display();
}

float getDistanceCM() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read the echo pulse, timeout after 30ms to prevent lag if no object is found
  long duration = pulseIn(echoPin, HIGH, 30000); 
  
  if (duration == 0) return -1; // Returns -1 if out of range
  
  // Calculate distance in cm (Speed of sound is ~0.034 cm/microsecond)
  return duration * 0.034 / 2; 
}
bool isSpeeding() {
  bool speeding = false;
  if (millis() - lastSpeedCheckMillis >= speedCheckInterval) {
    lastSpeedCheckMillis = millis();
    
    float currentDistance = getDistanceCM();

    // Ensure we have valid readings for both current and previous distances
    if (currentDistance > 0 && lastDistance > 0) {
      
      // Calculate how far the car moved (in cm)
      float distanceChanged = lastDistance - currentDistance; 

      if (distanceChanged > 0) { // Positive change means the car is moving towards the sensor
        // Convert distance to meters
        float distance_m = distanceChanged / 100.0; 
        
        // Convert interval to seconds
        float time_s = speedCheckInterval / 1000.0; 
        
        // Speed = d/t (m/s)
        float speed_m_s = distance_m / time_s; 
        
        // Convert to km/h
        float speed = speed_m_s * 3.6;
        speeding = speed > speedLimit;
      }
    }
    lastDistance = currentDistance;
  }
  return speeding;
}


void setup() {
  // wifi connection
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  
  delay(100); 

  int wifiTimeCounter = 0;
  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
    wifiTimeCounter++;
    if (wifiTimeCounter > 20) { // Timeout after 10 seconds
      break;
    }
  }

  Serial.println("\nWiFi Status: ");
  Serial.println(WiFi.status() == WL_CONNECTED? "Connected" : "Not Connected");
  
  pinMode(ledPinY, OUTPUT);
  pinMode(ledPinR, OUTPUT);
  pinMode(ledPinG, OUTPUT);

  // servo setup
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  myServo.setPeriodHertz(50); 
  myServo.attach(servoPin, 500, 2400);

  // Set initial servo position to close (0)
  myServo.write(0);

  // sensor setup
  pinMode(pirPin, INPUT);
  dht.begin();

  // Initiate traffic light state
  digitalWrite(ledPinR, HIGH);
  digitalWrite(ledPinY, LOW);
  digitalWrite(ledPinG, LOW);

  // Speed limit sign setup
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  showSpeedLimit(speedLimit); // Set initial speed sign

  // ultrasound sensor setup
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Upload button setup
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  // get online weather data at each bootup
  onlineWeather = fetchOnlineWeatherData();
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
      speedLimit = newSpeedLimit(); // Update speed limit based on weather conditions
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
    trafficState = 3;
    currentInterval = 8000;
  }

  if (isSpeeding()){
    display.clearDisplay();
    display.setTextSize(3);
    display.setCursor(30, 22);
    display.print("SLOW!");
    display.display();
    lastWarningMillis = currentMillis;
    //display.print(speedLimit);
    } // Check speed every loop iteration
  else if (currentMillis - lastWarningMillis >= WarningInterval) {
    showSpeedLimit(speedLimit); // Reset to speed limit sign after warning
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

  if (buttonPressedStatus == true) {
    buttonPressedStatus = false;
    ledState = !ledState;
    digitalWrite(LED_PIN, ledState);
    Serial.println(ledState?"Upload enabled":"Upload disabled");
  }
  if (ledState == HIGH && currentMillis - lastUpload >= UPLOAD_INTERVAL) {
    lastUpload = currentMillis;
    uploadReading(trafficState, speedLimit, temperature, humidity);
  }
}