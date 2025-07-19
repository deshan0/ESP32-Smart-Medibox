#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <DHTesp.h>

WiFiClient espClient;
PubSubClient mqttClient(espClient);
Servo slideServo;
DHTesp dhtSensor;

#define LDR_PIN 34      // Analog pin for Light Dependent Resistor (LDR)
#define SERVO_PIN 13    // Servo control pin
#define DHT_PIN 12      // DHT22 sensor pin

// Sampling and averaging intervals (in milliseconds)
float sampling_interval_ms = 5000;     // Sample every 5 seconds
float averaging_interval_ms = 120000;   // Average and send data every 2 minutes

float theta_offset = 30.0;  // Minimum servo angle offset
float gammaValue = 0.75;    // Gamma factor for servo angle calculation
float T_med = 30.0;         // Ideal temperature (reference for servo control)

float currentLightIntensity = 0.0;  // Latest light intensity reading (normalized and inverted)
float currentTemp = 0.0;            // Latest temperature reading
float currentAvg = 0.0;             // Average light intensity over averaging interval

unsigned long lastSampleTime = 0;   // Timestamp of last sensor sample
unsigned long lastSendTime = 0;     // Timestamp of last MQTT publish

float sumReadings = 0;   // Sum of light intensity readings (for averaging)
int numReadings = 0;     // Number of readings accumulated

// MQTT topics for publishing sensor data
const char *mqtt_light_intensity = "medibox/sensor/light_intensity";
const char *mqtt_temp = "medibox/sensor/temperature";
const char *mqtt_servo_angle = "medibox/actuator/servo_angle";

// MQTT topics for receiving remote configuration
const char *mqtt_sampling_interval = "medibox/config/sampling_interval";
const char *mqtt_averaging_interval = "medibox/config/averaging_interval";
const char *mqtt_theta_offset = "medibox/config/theta_offset";
const char *mqtt_gamma = "medibox/config/gamma";
const char *mqtt_T_med = "medibox/config/T_med";

void setup() {
  Serial.begin(115200);
  Serial.println("Hello, ESP32!");

  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  pinMode(LDR_PIN, INPUT);

  setupWiFi();
  setupMqtt();

  slideServo.attach(SERVO_PIN);
  slideServo.write(theta_offset);  // Initialize servo position
}

void loop() {
  if (!mqttClient.connected()) {
    connectToBroker();  // Ensure MQTT connection is active
  }

  handleLightMonitoring();  // Sample sensors and publish data

  mqttClient.loop();  // Process MQTT client tasks

  delay(10);  // Small delay to ease simulation timing
}

void setupWiFi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println("Wokwi-GUEST");

  WiFi.begin("Wokwi-GUEST", "");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
}

void setupMqtt() {
  mqttClient.setServer("broker.hivemq.com", 1883);
  mqttClient.setCallback(receiveCallback);  // Set MQTT message handler callback
}

void connectToBroker() {
  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");
    if (mqttClient.connect("ESP-9813247900")) {
      Serial.println("Connected");

      // Subscribe to configuration topics for remote parameter updates
      mqttClient.subscribe(mqtt_sampling_interval);
      mqttClient.subscribe(mqtt_averaging_interval);
      mqttClient.subscribe(mqtt_theta_offset);
      mqttClient.subscribe(mqtt_gamma);
      mqttClient.subscribe(mqtt_T_med);
    } else {
      Serial.println("Failed");
      Serial.println(mqttClient.state());
      delay(5000);  // Wait before retrying
    }
  }
}

// Reads and normalizes the LDR sensor value, then inverts it so higher brightness = higher value
float readLightIntensity() {
  float rawValue = (float)analogRead(LDR_PIN) / 4095.0;  // Normalize 12-bit ADC reading (0-4095) to 0-1
  return 1.0 - rawValue;  // Invert so bright light yields high values
}

void handleLightMonitoring() {
  unsigned long now = millis();

  // Sample sensors every sampling_interval_ms
  if (now - lastSampleTime >= sampling_interval_ms) {
    currentLightIntensity = readLightIntensity();
    sumReadings += currentLightIntensity;
    numReadings++;
    lastSampleTime = now;

    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    if (!isnan(data.temperature)) {
      currentTemp = data.temperature;
    } else {
      Serial.println("DHT sensor read error");
    }
  }

  // Publish averaged data every averaging_interval_ms
  if (now - lastSendTime >= averaging_interval_ms && numReadings > 0) {
    currentAvg = sumReadings / numReadings;

    float angle = computeServoAngle(currentLightIntensity, currentTemp);
    slideServo.write(angle);

    // Prepare MQTT payloads as JSON strings
    char lightBuf[64], tempBuf[64], angleBuf[64];
    snprintf(lightBuf, sizeof(lightBuf), "{\"light\":%.3f}", currentAvg);
    snprintf(tempBuf, sizeof(tempBuf), "{\"temperature\":%.2f}", currentTemp);
    snprintf(angleBuf, sizeof(angleBuf), "{\"angle\":%.1f}", angle);

    bool success = true;
    if (!mqttClient.publish(mqtt_light_intensity, lightBuf)) success = false;
    if (!mqttClient.publish(mqtt_temp, tempBuf)) success = false;
    if (!mqttClient.publish(mqtt_servo_angle, angleBuf)) success = false;

    if (success) {
      Serial.println("Data published: Light=" + String(lightBuf) + ", Temp=" + String(tempBuf) + ", Angle=" + String(angleBuf));
    } else {
      Serial.println("Publish error occurred");
    }

    // Reset accumulators for next averaging window
    sumReadings = 0;
    numReadings = 0;
    lastSendTime = now;
  }
}

// Callback to handle incoming MQTT messages for remote config updates
void receiveCallback(char *topic, byte *payload, unsigned int length) {
  char message[length + 1];
  memcpy(message, payload, length);
  message[length] = '\0';

  Serial.println("\n=== MQTT Message Received ===");
  Serial.println("Topic: " + String(topic));
  Serial.println("Payload: " + String(message));

  char *endptr;
  float value = strtof(message, &endptr);
  if (endptr == message || *endptr != '\0') {
    Serial.println("Invalid message format");
    return;
  }

  if (strcmp(topic, mqtt_sampling_interval) == 0) {
    sampling_interval_ms = value * 1000;
    Serial.println("Sampling interval set to: " + String(sampling_interval_ms / 1000) + " s");
  } else if (strcmp(topic, mqtt_averaging_interval) == 0) {
    averaging_interval_ms = value * 1000;
    Serial.println("Averaging interval set to: " + String(averaging_interval_ms / 1000) + " s");
  } else if (strcmp(topic, mqtt_theta_offset) == 0) {
    theta_offset = value;
    Serial.println("Theta offset adjusted to: " + String(theta_offset) + " degrees");
  } else if (strcmp(topic, mqtt_gamma) == 0) {
    gammaValue = value;
    Serial.println("Gamma factor updated to: " + String(gammaValue));
  } else if (strcmp(topic, mqtt_T_med) == 0) {
    T_med = value;
    Serial.println("Ideal temperature set to: " + String(T_med) + "°C");
  }
}

// Calculates the servo angle based on light intensity and temperature
float computeServoAngle(float light, float temp) {
  float angle = theta_offset
                + (180 - theta_offset) * light * gammaValue * log(sampling_interval_ms / averaging_interval_ms) * (temp / T_med);
  return constrain(angle, theta_offset, 180);  // Clamp to valid servo angle range
}
