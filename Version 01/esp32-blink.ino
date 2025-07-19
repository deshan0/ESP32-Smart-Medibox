#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include "DHTesp.h"
#include <DHT.h>

// ----- Display Setup -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ----- DHT22 Sensor Setup -----
#define DHT_PIN 12
DHT dht(DHT_PIN, DHT22);
float temperature;
float humidity;

// ----- WiFi & NTP Setup -----
const char* ssid = "Wokwi-GUEST";
const char* password = "";
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// UTC Offset (user-configurable)
float utcOffset = 0.0;

// ----- Menu & Alarm Setup -----
const char *menuItems[] = {
  "1. Set Time Zone",
  "2. Set Alarm 1",
  "3. Set Alarm 2",
  "4. View Alarms",
  "5. Delete Alarm"
};
int menuIndex = 0;
int totalMenuItems = sizeof(menuItems) / sizeof(menuItems[0]);
DateTime alarm1, alarm2;
bool alarm1Set = false, alarm2Set = false;

// ----- Button & Interrupt Setup -----
#define UPWARD    33
#define DOWNWARD  35
#define SELECT    34
#define NEXT      32
#define WARNING_LED 15

// ----- Buzzer Setup -----
#define BUZZER    5
int melody[] = {262, 294, 330, 349, 392, 440, 494, 523};
int noteDurations[] = {400, 400, 400, 400, 400, 400, 400, 400};

volatile bool buttonPressed1 = false;
volatile bool buttonPressed2 = false;
volatile bool buttonPressed3 = false;
volatile bool buttonPressed4 = false;

unsigned long lastInterruptTime1 = 0;
unsigned long lastInterruptTime2 = 0;
unsigned long lastInterruptTime3 = 0;
unsigned long lastInterruptTime4 = 0;
const unsigned long debounceDelay = 50; // milliseconds

// Function to generate a tone for the buzzer
void generateBuzzerTone(int pin, int frequency, int duration) {
  if (frequency == 0) {
    digitalWrite(pin, LOW);
    delay(duration);
    return;
  }
  
  // Simple square wave tone generation
  unsigned long startTime = millis();
  unsigned long halfPeriod = 500000 / frequency;  // in microseconds
  
  while (millis() - startTime < duration) {
    digitalWrite(pin, HIGH);
    delayMicroseconds(halfPeriod);
    digitalWrite(pin, LOW);
    delayMicroseconds(halfPeriod);
  }
}

// Button ISR functions to handle button presses
void IRAM_ATTR handleButtonISR1() {
  unsigned long now = millis();
  if (now - lastInterruptTime1 > debounceDelay) {
    buttonPressed1 = true;
    lastInterruptTime1 = now;
  }
}

void IRAM_ATTR handleButtonISR2() {
  unsigned long now = millis();
  if (now - lastInterruptTime2 > debounceDelay) {
    buttonPressed2 = true;
    lastInterruptTime2 = now;
  }
}

void IRAM_ATTR handleButtonISR3() {
  unsigned long now = millis();
  if (now - lastInterruptTime3 > debounceDelay) {
    buttonPressed3 = true;
    lastInterruptTime3 = now;
  }
}

void IRAM_ATTR handleButtonISR4() {
  unsigned long now = millis();
  if (now - lastInterruptTime4 > debounceDelay) {
    buttonPressed4 = true;
    lastInterruptTime4 = now;
  }
}

// Function to play a melody when alarm is triggered
void playAlarmMelodyOnce() {
  for (int i = 0; i < 8; i++) {
    Serial.print("Playing note: ");
    Serial.println(melody[i]);
    generateBuzzerTone(BUZZER, melody[i], noteDurations[i]);  // Play note
    delay(50); // Small delay between notes
  }
}

// Alarm handler function: plays melody until SELECT is pressed, then snoozes
void handleAlarmTrigger(int alarmNumber) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("ALARM!");
  display.setCursor(0, 10);
  display.print("Press SELECT to snooze");
  display.setCursor(0, 20);
  display.print("Press NEXT to cancel");
  display.display();
  
  while (true) {
    playAlarmMelodyOnce();
    
    if (buttonPressed3) {  // SELECT pressed: Snooze
      delay(200);  // Allow button release
      buttonPressed3 = false;
      DateTime now = DateTime(timeClient.getEpochTime());
      DateTime newAlarm = now + TimeSpan(0, 0, 5, 0);
      if (alarmNumber == 1) {
        alarm1 = newAlarm;
      } else {
        alarm2 = newAlarm;
      }
      break;
    }
    if (buttonPressed4) {  // NEXT pressed: Cancel alarm
      delay(200);
      buttonPressed4 = false;
      if (alarmNumber == 1) {
        alarm1Set = false;
      } else {
        alarm2Set = false;
      }
      break;
    }
  }
}

// ----- Drawing Functions -----
void displayMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Select an Option:");
  display.setCursor(0, 16);
  display.print(menuItems[menuIndex]);
}

void displayStatus() {
  timeClient.setTimeOffset(utcOffset * 3600);
  timeClient.update();
  String timeStr = timeClient.getFormattedTime();
  
  // Read sensor data
  temperature = dht.readTemperature();
  humidity = dht.readHumidity();
  
  String sensorStr_temp = "Temp: ";
  sensorStr_temp += isnan(temperature) ? "NaN" : String(temperature, 2);
  sensorStr_temp += " C";
  
  String sensorStr_hum = "Hum: ";
  sensorStr_hum += isnan(humidity) ? "NaN" : String(humidity, 1);
  sensorStr_hum += "%";
  
  display.setCursor(0, 24);
  display.print("Time: ");
  display.print(timeStr);
  display.setCursor(0, 32);
  display.print(sensorStr_temp);
  display.setCursor(0, 40);
  display.print(sensorStr_hum);
  
  // Warnings if out of healthy ranges
  int warnY = 48;
  if (!isnan(temperature) && (temperature < 24 || temperature > 32)) {
    display.setCursor(0, warnY);
    display.print("Temp Warning!");
    digitalWrite(WARNING_LED, HIGH);
    warnY += 8;
  }
  else {
    digitalWrite(WARNING_LED, LOW);
  }
  if (!isnan(humidity) && (humidity < 65 || humidity > 80)) {
    display.setCursor(0, warnY);
    display.print("Hum Warning!");
    digitalWrite(WARNING_LED, HIGH);
  }
  else {
    digitalWrite(WARNING_LED, LOW);
  }
}

// ----- Menu Functions -----
void configureTimeZone() {
  // Flush any previous button flags
  buttonPressed1 = buttonPressed2 = buttonPressed3 = buttonPressed4 = false;
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Set UTC Offset:");
  display.display();
  
  float offset = utcOffset; // start with current value
  bool exitMenu = false;
  
  while (!exitMenu) {
    display.clearDisplay();
    display.setCursor(0, 1);
    display.print("Offset: ");
    display.print(offset, 1);
    display.display();
    
    if (buttonPressed1) {  // UP increases
      offset += 0.5;
      buttonPressed1 = false;
    }
    if (buttonPressed2) {  // DOWN decreases
      offset -= 0.5;
      buttonPressed2 = false;
    }
    if (buttonPressed3) {  // SELECT saves and exits
      delay(200);
      utcOffset = offset;
      buttonPressed3 = false;
      exitMenu = true;
    }
    delay(200);
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("UTC Offset Saved!");
  display.display();
  delay(1000);
}


DateTime configureAlarm() {
  // Flush button flags
  buttonPressed1 = buttonPressed2 = buttonPressed3 = buttonPressed4 = false;
  
  int hour = 0;
  int minute = 0;
  bool confirmed = false;
  
  // --- Set Hour ---
  while (!confirmed) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Set Hour:");
    display.setCursor(0, 16);
    if (hour < 10) display.print("0");
    display.print(hour);
    display.setCursor(0, 32);
    display.print("UP/DOWN: Adjust");
    display.setCursor(0, 48);
    display.print("NEXT: Confirm");
    display.display();
    
    if (buttonPressed1) {
      hour = (hour + 1) % 24;
      buttonPressed1 = false;
    }
    if (buttonPressed2) {
      hour = (hour - 1 + 24) % 24;
      buttonPressed2 = false;
    }
    if (buttonPressed4) {
      buttonPressed4 = false;
      confirmed = true;
    }
    delay(200);
  }
  
  // --- Set Minute ---
  confirmed = false;
  while (!confirmed) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Set Minute:");
    display.setCursor(0, 16);
    if (minute < 10) display.print("0");
    display.print(minute);
    display.setCursor(0, 32);
    display.print("UP/DOWN: Adjust");
    display.setCursor(0, 48);
    display.print("NEXT: Confirm");
    display.display();
    
    if (buttonPressed1) {
      minute = (minute + 1) % 60;
      buttonPressed1 = false;
    }
    if (buttonPressed2) {
      minute = (minute - 1 + 60) % 60;
      buttonPressed2 = false;
    }
    if (buttonPressed4) {
      buttonPressed4 = false;
      confirmed = true;
    }
    delay(200);
  }
  
  // Final confirmation display
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Alarm Set:");
  if (hour < 10) display.print("0");
  display.print(hour);
  display.print(":");
  if (minute < 10) display.print("0");
  display.print(minute);
  display.display();
  delay(1000);
  
  // Use current date from NTP
  DateTime now = DateTime(timeClient.getEpochTime());
  return DateTime(now.year(), now.month(), now.day(), hour, minute, 0);
}

// View alarms until SELECT is pressed
void displayAlarms() {
  buttonPressed3 = false;
  while (true) {
    display.clearDisplay();
    display.setCursor(0, 0);
    if (alarm1Set) {
      display.print("Alarm 1: ");
      if (alarm1.hour() < 10) display.print("0");
      display.print(alarm1.hour());
      display.print(":");
      if (alarm1.minute() < 10) display.print("0");
      display.print(alarm1.minute());
    } else {
      display.print("Alarm 1: Not Set");
    }
    display.setCursor(0, 16);
    if (alarm2Set) {
      display.print("Alarm 2: ");
      if (alarm2.hour() < 10) display.print("0");
      display.print(alarm2.hour());
      display.print(":");
      if (alarm2.minute() < 10) display.print("0");
      display.print(alarm2.minute());
    } else {
      display.print("Alarm 2: Not Set");
    }
    display.setCursor(0, 48);
    display.print("SELECT to exit");
    display.display();
    
    if (buttonPressed3) {
      buttonPressed3 = false;
      break;
    }
    delay(200);
  }
}

void removeAlarm() {
  // Flush button flags
  buttonPressed1 = buttonPressed2 = buttonPressed3 = buttonPressed4 = false;
  // 0: Alarm 1, 1: Alarm 2, 2: Cancel
  int choice = 0;
  
  while (true) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Delete Alarm?");
    
    display.setCursor(0, 16);
    if (choice == 0) {
      display.print("> Alarm 1");
    } else {
      display.print("  Alarm 1");
    }
    
    display.setCursor(0, 32);
    if (choice == 1) {
      display.print("> Alarm 2");
    } else {
      display.print("  Alarm 2");
    }
    
    display.setCursor(0, 48);
    if (choice == 2) {
      display.print("> Cancel");
    } else {
      display.print("  Cancel");
    }
    display.display();
    
    // Navigate options
    if (buttonPressed1) { // UP
      choice = (choice - 1 + 3) % 3;
      buttonPressed1 = false;
    }
    if (buttonPressed2) { // DOWN
      choice = (choice + 1) % 3;
      buttonPressed2 = false;
    }
    
    // Confirm deletion with NEXT button
    if (buttonPressed4) {
      buttonPressed4 = false;
      break;
    }
    
    // Exit deletion menu immediately if SELECT is pressed
    if (buttonPressed3) {
      buttonPressed3 = false;
      return;
    }
    delay(100);
  }
  
  // Process choice
  if (choice == 0) {
    alarm1Set = false;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Alarm 1 deleted");
    display.display();
    delay(1000);
  } else if (choice == 1) {
    alarm2Set = false;
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Alarm 2 deleted");
    display.display();
    delay(1000);
  } else {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Delete Cancelled");
    display.display();
    delay(1000);
  }
}

// Function to execute the selected menu option
void executeSelectedMenuOption(int option) {
  switch (option) {
    case 0:
      configureTimeZone();
      break;
    case 1: {
      DateTime temp = configureAlarm();
      if (temp.year() != 0) {
        alarm1 = temp;
        alarm1Set = true;
      }
      break;
    }
    case 2: {
      DateTime temp = configureAlarm();
      if (temp.year() != 0) {
        alarm2 = temp;
        alarm2Set = true;
      }
      break;
    }
    case 3:
      displayAlarms();
      break;
    case 4:
      removeAlarm();
      break;
  }
  displayMenu();
  display.display();
}

// ----- Setup & Loop ----- 
void setup() {
  Serial.begin(115200);

  // Initialize WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");

  // Initialize I2C and OLED
  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed");
    for (;;);
  }
  display.clearDisplay();
  display.display();

  // Initialize DHT sensor
  dht.begin();
  Serial.println("DHT sensor initialized");

  // Initialize NTP client
  timeClient.begin();

  // Setup button pins
  pinMode(UPWARD, INPUT_PULLUP);
  pinMode(DOWNWARD, INPUT_PULLUP);
  pinMode(SELECT, INPUT_PULLUP);
  pinMode(NEXT, INPUT_PULLUP);
  pinMode(WARNING_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  // Display welcome message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Medibox System");
  display.display();

  // Test buzzer at startup
  Serial.println("Testing buzzer...");
  generateBuzzerTone(BUZZER, 1000, 200);
  Serial.println("Buzzer test complete");

  delay(2000);

  displayMenu();
  display.display();

  // Attach interrupts for buttons
  attachInterrupt(digitalPinToInterrupt(UPWARD), handleButtonISR1, FALLING);
  attachInterrupt(digitalPinToInterrupt(DOWNWARD), handleButtonISR2, FALLING);
  attachInterrupt(digitalPinToInterrupt(SELECT), handleButtonISR3, FALLING);
  attachInterrupt(digitalPinToInterrupt(NEXT), handleButtonISR4, FALLING);
}

void loop() {
  timeClient.update();
  DateTime now = DateTime(timeClient.getEpochTime());

  // Check Alarm 1
  if (alarm1Set && now.hour() == alarm1.hour() && now.minute() == alarm1.minute() && now.second() < 10) {
    Serial.println("Alarm 1 Triggered!");
    handleAlarmTrigger(1);
  }
  // Check Alarm 2
  if (alarm2Set && now.hour() == alarm2.hour() && now.minute() == alarm2.minute() && now.second() < 10) {
    Serial.println("Alarm 2 Triggered!");
    handleAlarmTrigger(2);
  }
  
  // Menu navigation
  if (buttonPressed1) {
    menuIndex = (menuIndex - 1 + totalMenuItems) % totalMenuItems;
    buttonPressed1 = false;
  }
  if (buttonPressed2) {
    menuIndex = (menuIndex + 1) % totalMenuItems;
    buttonPressed2 = false;
  }
  if (buttonPressed3) {
    executeSelectedMenuOption(menuIndex);
    buttonPressed3 = false;
  }
  if (buttonPressed4) {
    executeSelectedMenuOption(menuIndex);
    buttonPressed4 = false;
  }

  displayMenu();
  displayStatus();
  display.display();
  delay(500);
}
