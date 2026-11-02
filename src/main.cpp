/*
 * Airsoft EFC - Advanced Firmware with EEPROM Menu & Pre-Cocking
 * MCU: ATtiny85 (Core: ATTinyCore by SpenceKonde)
 * Pins: PB0 = Trigger (Pullup), PB1 = Motor (FET Driver), PB2/A1 = ADC Voltage
 */
#include <Arduino.h>
#include <EEPROM.h>

// --- PINS ---
const int TRIGGER_PIN = 0;   // PB0 (Pin 5) - Trigger
const int MOTOR_PIN   = 1;   // PB1 (Pin 6) - Motor output
const int VOLTAGE_PIN = A1;  // PB2 (Pin 7) - Battery voltage (Voltage divider 47k / 10k)

// --- EEPROM ADDRESSES ---
const int EE_ADDR_SENSITIVITY = 0; // [1..10], 1 = least sensitive, 10 = most sensitive
const int EE_ADDR_FIREMODE    = 1; // 1 = Semi, 2 = Binary, 3 = 3-Burst
const int EE_ADDR_PRECOCK     = 2; // [1..5] (0% bis 40%) precocking

// --- GLOBAL CONFIGURATION VARIABLES ---
// will be set on startup from EEPROM, but can be changed in programming mode
int g_sensitivity = 5;
int g_fireMode    = 1;
int g_precockLvl  = 1;

int g_adcThreshold = 15;
unsigned long g_lastCycleDuration = 60; // will be used for Pre-Cocking calculation

const unsigned long MAX_RUNTIME_MS  = 100; // Safety-Cutoff if dedection fails
const unsigned long INRUSH_DELAY_MS = 15;  // Ignore voltage drop and rise during motor startup

// --- MOTOR BEEPER FUNCTIONS ---
void beepMotor(int frequency, int duration_ms) {
  int period_us = 1000000 / frequency;
  int high_time_us = 35;
  int low_time_us = period_us - high_time_us;
  if (low_time_us < 10) low_time_us = 10;
  
  unsigned long start = millis();
  while (millis() - start < (unsigned long)duration_ms) {
    digitalWrite(MOTOR_PIN, HIGH);
    delayMicroseconds(high_time_us);
    digitalWrite(MOTOR_PIN, LOW);
    delayMicroseconds(low_time_us);
  }
}

void beepShort() {
  beepMotor(1800, 80);
  delay(120);
}

void beepLong() {
  beepMotor(1300, 400);
  delay(150);
}

void beepCount(int count) {
  for (int i = 0; i < count; i++) {
    beepShort();
  }
}

// --- HELPER: WAITING & TRIGGER HOLD DETECTION ---
// Detect if user wants to enter menu item or select option. Returns true if within wait_window_ms trigger was held for at least 500ms.
// Otherwise returns false.
bool checkTriggerSelection(unsigned long wait_window_ms) {
  unsigned long start = millis();
  
  while (millis() - start < wait_window_ms) {
    if (digitalRead(TRIGGER_PIN) == LOW) {
      unsigned long pressStart = millis();
      while (digitalRead(TRIGGER_PIN) == LOW) {
        if (millis() - pressStart >= 500) {
          beepLong();
          while (digitalRead(TRIGGER_PIN) == LOW) {
            // wait till release
          }
          delay(50);
          return true;
        }
      }
    }
  }
  return false;
}

// --- PROGRAMMING MENU SYSTEM ---
// Let user select an option for the submenu he is in. When selected, the value is stored in EEPROM and the menu exits.
void runSubMenu(int option) {
  int maxValues = 10;
  if (option == 2) maxValues = 3;
  if (option == 3) maxValues = 5;

  bool selected = false;
  
  while (!selected) {
    for (int v = 1; v <= maxValues; v++) {
      beepCount(v);
      
      // Wartefenster für Eingabe
      if (checkTriggerSelection(2000)) {
        // Wert wurde ausgewählt! Im EEPROM speichern
        if (option == 1) EEPROM.update(EE_ADDR_SENSITIVITY, v);
        if (option == 2) EEPROM.update(EE_ADDR_FIREMODE, v);
        if (option == 3) EEPROM.update(EE_ADDR_PRECOCK, v);
        
        // Menü beenden mit 2 langen Tönen
        delay(200);
        beepLong();
        beepLong();
        selected = true;
        break;
      }
    }
  }
}

void runProgrammingMode() {
  beepLong();
  beepLong();
  delay(500);

  bool inMenu = true;
  while (inMenu) {
    // 1. Option: Sensitivity
    beepCount(1);
    if (checkTriggerSelection(2000)) {
      runSubMenu(1);
      inMenu = false;
      break;
    }

    // 2. Option: Feuerwahl
    beepCount(2);
    if (checkTriggerSelection(2000)) {
      runSubMenu(2);
      inMenu = false;
      break;
    }

    // 3. Option: Pre-Cocking
    beepCount(3);
    if (checkTriggerSelection(2000)) {
      runSubMenu(3);
      inMenu = false;
      break;
    }
  }
}

// --- EEPROM LOADER ---
void loadEEPROM() {
  g_sensitivity = EEPROM.read(EE_ADDR_SENSITIVITY);
  g_fireMode    = EEPROM.read(EE_ADDR_FIREMODE);
  g_precockLvl  = EEPROM.read(EE_ADDR_PRECOCK);

  // Check if eeprom values are valid, otherwise set defaults
  if (g_sensitivity < 1 || g_sensitivity > 10) g_sensitivity = 5;
  if (g_fireMode < 1    || g_fireMode > 3)    g_fireMode = 1;
  if (g_precockLvl < 1  || g_precockLvl > 5)   g_precockLvl = 1;

  g_adcThreshold = map(g_sensitivity, 1, 10, 45, 15);
}

// Read the ADC 5 times with 100us delay. Only returns median value to remove noise and spikes.
int readVoltageMedian() {
  int samples[5];
  
  for (int i = 0; i < 5; i++) {
    samples[i] = analogRead(VOLTAGE_PIN);
    delayMicroseconds(100);
  }

  // Bubbelsort
  for (int i = 0; i < 4; i++) {
    for (int j = i + 1; j < 5; j++) {
      if (samples[i] > samples[j]) {
        int temp = samples[i];
        samples[i] = samples[j];
        samples[j] = temp;
      }
    }
  }

  return samples[2]; 
}

void fireSingleCycle() {
  unsigned long startTime = millis();
  int currentVoltage = 0;
  int valleyVoltage = 1023;

  digitalWrite(MOTOR_PIN, HIGH);
  while ((millis() - startTime) < INRUSH_DELAY_MS) { }
  while (true) {
    currentVoltage = readVoltageMedian();

    if (currentVoltage < valleyVoltage) {
      valleyVoltage = currentVoltage;
    }

    if (currentVoltage > (valleyVoltage + g_adcThreshold)) {
      break; // spring released, voltage jumped up
    }

    if ((millis() - startTime) > MAX_RUNTIME_MS) {
      break;
    }
  }

  digitalWrite(MOTOR_PIN, LOW);

  // Get cycle duration for precocking calculation
  unsigned long duration = millis() - startTime;
  if (duration < MAX_RUNTIME_MS) {
    g_lastCycleDuration = duration;
  }

  // Precocking
  // TODO: move up and remove possibly unnecessary motor stop/start and stop again. Then testing
  if (g_precockLvl > 1 && g_lastCycleDuration > 0) {
    int percent = (g_precockLvl - 1) * 10; 
    unsigned long precockTime = (g_lastCycleDuration * percent) / 100;

    if (precockTime > 0) {
      digitalWrite(MOTOR_PIN, HIGH);
      delay(precockTime);
      digitalWrite(MOTOR_PIN, LOW);
    }
  }
}

void setup() {
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  analogReference(INTERNAL2V56); // 2s lipo may drop below 5V. Use 2.56V internal reference instead

  // Check if programming mode should be entered (trigger held for 2 seconds)
  if (digitalRead(TRIGGER_PIN) == LOW) {
    unsigned long holdStart = millis();
    while (digitalRead(TRIGGER_PIN) == LOW) {
      if (millis() - holdStart >= 2000) {
        runProgrammingMode();
        break;
      }
    }
  }

  loadEEPROM();
}

void loop() {
  if (digitalRead(TRIGGER_PIN) == LOW) {

    // MODE 1: SEMI AUTO
    if (g_fireMode == 1) {
      fireSingleCycle();
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { }
      delay(10);
    }

    // MODE 2: BINARY TRIGGER
    else if (g_fireMode == 2) {
      fireSingleCycle();
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { }
      delay(10);
      fireSingleCycle();
    }

    // MODE 3: 3-ROUND BURST
    // TODO: consider letting 3 round burst run even if trigger is released. Testing
    // consideration: if trigger is released and pressed again before end, should it start another burst?
    else if (g_fireMode == 3) {
      for (int i = 0; i < 3; i++) {
        fireSingleCycle();
        if (digitalRead(TRIGGER_PIN) == HIGH) break;
      }
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { }
      delay(10);
    }
  }
}
