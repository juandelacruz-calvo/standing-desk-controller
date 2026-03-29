#include <Arduino.h>
#include <EEPROM.h>
#include <NewPing.h>
#include <AceButton.h>

using namespace ace_button;

// --- Pin assignments ---
constexpr uint8_t PIN_TRIG      = 6;
constexpr uint8_t PIN_ECHO      = 7;
constexpr uint8_t PIN_MOTOR_UP  = 8;   // open-drain: LOW = move, float = stop
constexpr uint8_t PIN_MOTOR_DN  = 9;   // open-drain: LOW = move, float = stop
constexpr uint8_t PIN_BTN_UP    = 2;
constexpr uint8_t PIN_BTN_DN    = 3;
constexpr uint8_t PIN_BTN_MEM1  = 4;
constexpr uint8_t PIN_BTN_MEM2  = 5;
constexpr uint8_t PIN_BTN_MEM3  = 10;

// --- Sensor config ---
constexpr unsigned int MAX_DISTANCE_CM = 200;

// --- Closed-loop config ---
constexpr float HEIGHT_TOLERANCE_CM  = 0.5f;
constexpr unsigned long MOVE_TIMEOUT_MS = 30000;
constexpr unsigned long SENSOR_INTERVAL_MS = 100;

// --- EEPROM addresses (each float = 4 bytes) ---
constexpr int EEPROM_ADDR_MEM1 = 0;
constexpr int EEPROM_ADDR_MEM2 = 4;
constexpr int EEPROM_ADDR_MEM3 = 8;

// --- State machine ---
enum State : uint8_t {
  STATE_IDLE,
  STATE_MANUAL_UP,
  STATE_MANUAL_DOWN,
  STATE_MOVING_TO_TARGET
};

// --- Globals ---
NewPing sonar(PIN_TRIG, PIN_ECHO, MAX_DISTANCE_CM);

ButtonConfig manualBtnConfig;
AceButton btnUp(&manualBtnConfig, PIN_BTN_UP);
AceButton btnDown(&manualBtnConfig, PIN_BTN_DN);
AceButton btnMem1(PIN_BTN_MEM1);
AceButton btnMem2(PIN_BTN_MEM2);
AceButton btnMem3(PIN_BTN_MEM3);

State currentState = STATE_IDLE;
float currentHeight = 0.0f;
float targetHeight  = 0.0f;
float mem1Height    = NAN;
float mem2Height    = NAN;
float mem3Height    = NAN;
unsigned long moveStartTime   = 0;
unsigned long lastSensorRead  = 0;

// --- Forward declarations ---
void handleButtonEvent(AceButton* button, uint8_t eventType, uint8_t buttonState);
float readHeight();
void motorUp();
void motorDown();
void motorStop();
void loadMemory();
void saveMemory(int addr, float height);
void startMoveToTarget(float target);
void updateMoveToTarget();
void cancelMove();

// ---------- Motor control ----------

void motorStop() {
  pinMode(PIN_MOTOR_UP, INPUT);
  pinMode(PIN_MOTOR_DN, INPUT);
}

void motorUp() {
  pinMode(PIN_MOTOR_DN, INPUT);
  pinMode(PIN_MOTOR_UP, OUTPUT);
  digitalWrite(PIN_MOTOR_UP, LOW);
}

void motorDown() {
  pinMode(PIN_MOTOR_UP, INPUT);
  pinMode(PIN_MOTOR_DN, OUTPUT);
  digitalWrite(PIN_MOTOR_DN, LOW);
}

// ---------- Sensor ----------

float readHeight() {
  unsigned long us = sonar.ping_median(5);
  if (us == 0) return currentHeight;
  return (float)us / US_ROUNDTRIP_CM;
}

// ---------- EEPROM ----------

void loadMemory() {
  EEPROM.get(EEPROM_ADDR_MEM1, mem1Height);
  EEPROM.get(EEPROM_ADDR_MEM2, mem2Height);
  EEPROM.get(EEPROM_ADDR_MEM3, mem3Height);
  if (isnan(mem1Height) || mem1Height < 1.0f || mem1Height > (float)MAX_DISTANCE_CM)
    mem1Height = NAN;
  if (isnan(mem2Height) || mem2Height < 1.0f || mem2Height > (float)MAX_DISTANCE_CM)
    mem2Height = NAN;
  if (isnan(mem3Height) || mem3Height < 1.0f || mem3Height > (float)MAX_DISTANCE_CM)
    mem3Height = NAN;
}

void saveMemory(int addr, float height) {
  EEPROM.put(addr, height);
  Serial.print(F("Saved "));
  Serial.print(addr == EEPROM_ADDR_MEM1 ? F("MEM1") :
               addr == EEPROM_ADDR_MEM2 ? F("MEM2") : F("MEM3"));
  Serial.print(F(" = "));
  Serial.print(height, 1);
  Serial.println(F(" cm"));
}

// ---------- Closed-loop movement ----------

void startMoveToTarget(float target) {
  targetHeight = target;
  moveStartTime = millis();
  currentState = STATE_MOVING_TO_TARGET;
  Serial.print(F("Moving to "));
  Serial.print(targetHeight, 1);
  Serial.println(F(" cm"));
}

void cancelMove() {
  motorStop();
  currentState = STATE_IDLE;
  Serial.println(F("Move cancelled"));
}

void updateMoveToTarget() {
  if (millis() - moveStartTime > MOVE_TIMEOUT_MS) {
    motorStop();
    currentState = STATE_IDLE;
    Serial.println(F("Move timeout"));
    return;
  }

  if (millis() - lastSensorRead < SENSOR_INTERVAL_MS) return;
  lastSensorRead = millis();

  currentHeight = readHeight();
  float error = targetHeight - currentHeight;

  if (abs(error) <= HEIGHT_TOLERANCE_CM) {
    motorStop();
    currentState = STATE_IDLE;
    Serial.print(F("Target reached: "));
    Serial.print(currentHeight, 1);
    Serial.println(F(" cm"));
    return;
  }

  if (error > 0) {
    motorUp();
  } else {
    motorDown();
  }
}

// ---------- Button handler ----------

void handleButtonEvent(AceButton* button, uint8_t eventType, uint8_t /* buttonState */) {
  uint8_t pin = button->getPin();

  // --- UP / DOWN: manual hold-to-move ---
  if (pin == PIN_BTN_UP) {
    if (eventType == AceButton::kEventPressed) {
      Serial.println(F("BTN UP pressed"));
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      motorUp();
      currentState = STATE_MANUAL_UP;
    } else if (eventType == AceButton::kEventReleased) {
      Serial.println(F("BTN UP released"));
      motorStop();
      currentState = STATE_IDLE;
    }
    return;
  }

  if (pin == PIN_BTN_DN) {
    if (eventType == AceButton::kEventPressed) {
      Serial.println(F("BTN DN pressed"));
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      motorDown();
      currentState = STATE_MANUAL_DOWN;
    } else if (eventType == AceButton::kEventReleased) {
      Serial.println(F("BTN DN released"));
      motorStop();
      currentState = STATE_IDLE;
    }
    return;
  }

  // --- MEM1 / MEM2 / MEM3: click = recall, long press = save ---
  if (pin == PIN_BTN_MEM1) {
    if (eventType == AceButton::kEventClicked) {
      Serial.println(F("BTN MEM1 clicked"));
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      if (!isnan(mem1Height)) {
        startMoveToTarget(mem1Height);
      } else {
        Serial.println(F("MEM1 not set"));
      }
    } else if (eventType == AceButton::kEventLongPressed) {
      Serial.println(F("BTN MEM1 long press"));
      currentHeight = readHeight();
      mem1Height = currentHeight;
      saveMemory(EEPROM_ADDR_MEM1, mem1Height);
    }
    return;
  }

  if (pin == PIN_BTN_MEM2) {
    if (eventType == AceButton::kEventClicked) {
      Serial.println(F("BTN MEM2 clicked"));
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      if (!isnan(mem2Height)) {
        startMoveToTarget(mem2Height);
      } else {
        Serial.println(F("MEM2 not set"));
      }
    } else if (eventType == AceButton::kEventLongPressed) {
      Serial.println(F("BTN MEM2 long press"));
      currentHeight = readHeight();
      mem2Height = currentHeight;
      saveMemory(EEPROM_ADDR_MEM2, mem2Height);
    }
    return;
  }

  if (pin == PIN_BTN_MEM3) {
    if (eventType == AceButton::kEventClicked) {
      Serial.println(F("BTN MEM3 clicked"));
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      if (!isnan(mem3Height)) {
        startMoveToTarget(mem3Height);
      } else {
        Serial.println(F("MEM3 not set"));
      }
    } else if (eventType == AceButton::kEventLongPressed) {
      Serial.println(F("BTN MEM3 long press"));
      currentHeight = readHeight();
      mem3Height = currentHeight;
      saveMemory(EEPROM_ADDR_MEM3, mem3Height);
    }
    return;
  }
}

// ---------- Setup ----------

void setup() {
  Serial.begin(9600);

  motorStop();

  pinMode(PIN_BTN_UP,   INPUT_PULLUP);
  pinMode(PIN_BTN_DN,   INPUT_PULLUP);
  pinMode(PIN_BTN_MEM1, INPUT_PULLUP);
  pinMode(PIN_BTN_MEM2, INPUT_PULLUP);
  pinMode(PIN_BTN_MEM3, INPUT_PULLUP);

  manualBtnConfig.setEventHandler(handleButtonEvent);

  ButtonConfig* cfg = ButtonConfig::getSystemButtonConfig();
  cfg->setEventHandler(handleButtonEvent);
  cfg->setFeature(ButtonConfig::kFeatureClick);
  cfg->setFeature(ButtonConfig::kFeatureLongPress);
  cfg->setFeature(ButtonConfig::kFeatureSuppressAfterLongPress);
  cfg->setFeature(ButtonConfig::kFeatureSuppressAfterClick);

  loadMemory();

  Serial.println(F("==========================="));
  Serial.println(F(" Standing Desk Controller"));
  Serial.println(F("==========================="));

  Serial.println(F("[Pins]"));
  Serial.print(F("  Sensor:  TRIG=D")); Serial.print(PIN_TRIG);
  Serial.print(F(" ECHO=D"));           Serial.println(PIN_ECHO);
  Serial.print(F("  Motor:   UP=D"));   Serial.print(PIN_MOTOR_UP);
  Serial.print(F(" DN=D"));             Serial.println(PIN_MOTOR_DN);
  Serial.print(F("  Buttons: UP=D"));   Serial.print(PIN_BTN_UP);
  Serial.print(F(" DN=D"));             Serial.print(PIN_BTN_DN);
  Serial.print(F(" M1=D"));             Serial.print(PIN_BTN_MEM1);
  Serial.print(F(" M2=D"));             Serial.print(PIN_BTN_MEM2);
  Serial.print(F(" M3=D"));             Serial.println(PIN_BTN_MEM3);

  Serial.println(F("[Sensor test]"));
  currentHeight = readHeight();
  if (currentHeight > 0.0f) {
    Serial.print(F("  HC-SR04: OK  Height="));
    Serial.print(currentHeight, 1);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("  HC-SR04: NO ECHO - check wiring"));
  }

  Serial.println(F("[Memory]"));
  if (!isnan(mem1Height)) {
    Serial.print(F("  MEM1: "));
    Serial.print(mem1Height, 1);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("  MEM1: not set"));
  }
  if (!isnan(mem2Height)) {
    Serial.print(F("  MEM2: "));
    Serial.print(mem2Height, 1);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("  MEM2: not set"));
  }
  if (!isnan(mem3Height)) {
    Serial.print(F("  MEM3: "));
    Serial.print(mem3Height, 1);
    Serial.println(F(" cm"));
  } else {
    Serial.println(F("  MEM3: not set"));
  }

  Serial.println(F("[Config]"));
  Serial.print(F("  Tolerance: ")); Serial.print(HEIGHT_TOLERANCE_CM, 1); Serial.println(F(" cm"));
  Serial.print(F("  Timeout:   ")); Serial.print(MOVE_TIMEOUT_MS / 1000); Serial.println(F(" s"));
  Serial.print(F("  Max dist:  ")); Serial.print(MAX_DISTANCE_CM);        Serial.println(F(" cm"));

  Serial.println(F("==========================="));
  Serial.println(F("Ready."));
}

// ---------- Loop ----------

void loop() {
  btnUp.check();
  btnDown.check();
  btnMem1.check();
  btnMem2.check();
  btnMem3.check();

  if (currentState == STATE_MOVING_TO_TARGET) {
    updateMoveToTarget();
  }

  static unsigned long lastDebug = 0;
  if (currentState == STATE_IDLE && millis() - lastDebug > 2000) {
    lastDebug = millis();
    unsigned long us = sonar.ping_median(5);
    float cm = (us == 0) ? 0.0f : (float)us / US_ROUNDTRIP_CM;
    currentHeight = (us == 0) ? currentHeight : cm;
    Serial.print(F("H: "));
    Serial.print(cm, 1);
    Serial.print(F(" cm  raw="));
    Serial.print(us);
    Serial.println(F(" us"));
  }
}
