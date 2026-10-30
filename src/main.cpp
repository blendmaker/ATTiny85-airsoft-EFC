/*
 * Airsoft EFC - Advanced Firmware with EEPROM Menu & Pre-Cocking
 * MCU: ATtiny85 (Core: ATTinyCore by SpenceKonde)
 * Pins: PB0 = Trigger (Pullup), PB1 = Motor (FET Driver), PB2/A1 = ADC Voltage
 */
#include <Arduino.h>
#include <EEPROM.h>

// --- PIN DEFINITIONS ---
const int TRIGGER_PIN = 0;   // PB0 (Pin 5) - Abzug
const int MOTOR_PIN   = 1;   // PB1 (Pin 6) - MOSFET Treiber
const int VOLTAGE_PIN = A1;  // PB2 (Pin 7) - ADC1 (Spannungsteiler 47k / 10k)

// --- EEPROM ADDRESSES ---
const int EE_ADDR_SENSITIVITY = 0; // Werte 1..10
const int EE_ADDR_FIREMODE    = 1; // 1 = Semi, 2 = Binary, 3 = 3-Burst
const int EE_ADDR_PRECOCK     = 2; // Werte 1..5 (0% bis 40%)

// --- GLOBAL CONFIGURATION VARIABLES ---
int g_sensitivity = 5;
int g_fireMode    = 1;
int g_precockLvl  = 1;

int g_adcThreshold = 15;
unsigned long g_lastCycleDuration = 60; // Fallback-Startwert für 1. Schuss (60ms)

const unsigned long MAX_RUNTIME_MS  = 120; // Sicherheits-Cutoff pro Schuss
const unsigned long INRUSH_DELAY_MS = 15;  // Anlaufphase ignorieren

// --- MOTOR BEEPER FUNCTIONS ---
void beepMotor(int frequency, int duration_ms) {
  int period_us = 1000000 / frequency;
  int high_time_us = 35; // Sehr kurz, damit Motor nicht dreht
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
// Stoppt das Durchlaufen der Optionen sofort, wenn der Abzug gedrückt wird.
// Gibt 'true' zurück, wenn der Abzug mindestens 500ms gehalten wurde (Auswahl).
bool checkTriggerSelection(unsigned long wait_window_ms) {
  unsigned long start = millis();
  
  while (millis() - start < wait_window_ms) {
    if (digitalRead(TRIGGER_PIN) == LOW) {
      // Abzug gedrückt -> Menü-Timer stoppen & Haltezeit messen
      unsigned long pressStart = millis();
      while (digitalRead(TRIGGER_PIN) == LOW) {
        if (millis() - pressStart >= 500) {
          beepLong(); // Bestätigungston: Option ausgewählt!
          while (digitalRead(TRIGGER_PIN) == LOW) {
            // Warten bis Abzug losgelassen wird
          }
          delay(50);
          return true;
        }
      }
      // Wenn kürzer als 500ms gedrückt wurde: Ignorieren und im Timer fortfahren
    }
  }
  return false;
}

// --- PROGRAMMING MENU SYSTEM ---
void runSubMenu(int option) {
  int maxValues = 10;
  if (option == 2) maxValues = 3;
  if (option == 3) maxValues = 5;

  bool selected = false;
  
  while (!selected) {
    for (int v = 1; v <= maxValues; v++) {
      beepCount(v); // Signalisiert den aktuellen Wert (z.B. 3 kurze Töne)
      
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
  // Startsignal für Programmiermodus: 2 lange Töne
  beepLong();
  beepLong();
  delay(500);

  bool inMenu = true;
  while (inMenu) {
    // 1. Option: Sensitivity (1 kurzer Ton)
    beepCount(1);
    if (checkTriggerSelection(2000)) {
      runSubMenu(1);
      inMenu = false;
      break;
    }

    // 2. Option: Feuerwahl (2 kurze Töne)
    beepCount(2);
    if (checkTriggerSelection(2000)) {
      runSubMenu(2);
      inMenu = false;
      break;
    }

    // 3. Option: Pre-Cocking (3 kurze Töne)
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

  // Plausibilitätsprüfung für frisches EEPROM
  if (g_sensitivity < 1 || g_sensitivity > 10) g_sensitivity = 5;
  if (g_fireMode < 1    || g_fireMode > 3)    g_fireMode = 1;
  if (g_precockLvl < 1  || g_precockLvl > 5)   g_precockLvl = 1;

  // Sensitivität auf ADC-Schritte umrechnen (10 = empfindlich = kleiner Threshold)
  g_adcThreshold = map(g_sensitivity, 1, 10, 25, 5);
}

// --- CYCLE DETECTION & FIRING LOGIC ---
int readVoltageSmoothed() {
  long sum = 0;
  for (int i = 0; i < 4; i++) {
    sum += analogRead(VOLTAGE_PIN);
  }
  return sum / 4;
}

void fireSingleCycle() {
  unsigned long startTime = millis();
  int currentVoltage = 0;
  int valleyVoltage = 1023;

  // 1. Motor starten
  digitalWrite(MOTOR_PIN, HIGH);

  // 2. Inrush ignorieren
  while ((millis() - startTime) < INRUSH_DELAY_MS) { }

  // 3. Zyklus überwachen
  while (true) {
    currentVoltage = readVoltageSmoothed();

    if (currentVoltage < valleyVoltage) {
      valleyVoltage = currentVoltage;
    }

    if (currentVoltage > (valleyVoltage + g_adcThreshold)) {
      break; // Zyklus regulär beendet!
    }

    if ((millis() - startTime) > MAX_RUNTIME_MS) {
      break; // Sicherheits-Timeout
    }
  }

  // Motor stoppen
  digitalWrite(MOTOR_PIN, LOW);

  // Messung der Zyklusdauer für Pre-Cocking (nur wenn kein Timeout)
  unsigned long duration = millis() - startTime;
  if (duration < MAX_RUNTIME_MS) {
    g_lastCycleDuration = duration;
  }

  // 4. Pre-Cocking ausführen
  if (g_precockLvl > 1 && g_lastCycleDuration > 0) {
    // Stufe 2 = 10%, Stufe 3 = 20%, Stufe 4 = 30%, Stufe 5 = 40%
    int percent = (g_precockLvl - 1) * 10; 
    unsigned long precockTime = (g_lastCycleDuration * percent) / 100;

    if (precockTime > 0) {
      digitalWrite(MOTOR_PIN, HIGH);
      delay(precockTime);
      digitalWrite(MOTOR_PIN, LOW);
    }
  }
}

// --- SETUP & MAIN LOOP ---
void setup() {
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  pinMode(MOTOR_PIN, OUTPUT);
  digitalWrite(MOTOR_PIN, LOW);

  analogReference(INTERNAL2V56); // Interne Referenz für ADC

  // Überprüfung auf Programmiermodus beim Start
  if (digitalRead(TRIGGER_PIN) == LOW) {
    unsigned long holdStart = millis();
    while (digitalRead(TRIGGER_PIN) == LOW) {
      if (millis() - holdStart >= 2000) {
        runProgrammingMode();
        break;
      }
    }
  }

  // Werte aus EEPROM laden
  loadEEPROM();
}

void loop() {
  // Regulärer Schussbetrieb
  if (digitalRead(TRIGGER_PIN) == LOW) {

    // MODE 1: SEMI AUTO
    if (g_fireMode == 1) {
      fireSingleCycle();
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { } // Warten auf Release
      delay(10);
    }

    // MODE 2: BINARY TRIGGER
    else if (g_fireMode == 2) {
      fireSingleCycle(); // 1. Schuss bei Druck
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { } // Warten auf Release
      delay(10);
      fireSingleCycle(); // 2. Schuss bei Loslassen
    }

    // MODE 3: 3-ROUND BURST
    else if (g_fireMode == 3) {
      for (int i = 0; i < 3; i++) {
        fireSingleCycle();
        if (digitalRead(TRIGGER_PIN) == HIGH) break; // Abgebrochen, wenn Abzug losgelassen wird
      }
      delay(10);
      while (digitalRead(TRIGGER_PIN) == LOW) { } // Warten auf Release
      delay(10);
    }
  }
}