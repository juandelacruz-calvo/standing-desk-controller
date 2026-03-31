#include <Arduino.h>
#include <EEPROM.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <NewPing.h>
#include <AceButton.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef MQTT_SERVER
#define MQTT_SERVER ""
#endif
#ifndef MQTT_PORT
#define MQTT_PORT 1883
#endif
#ifndef MQTT_USER
#define MQTT_USER ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif
#ifndef MQTT_TOPIC_PREFIX
#define MQTT_TOPIC_PREFIX "standing-desk"
#endif

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

using namespace ace_button;

// --- Wemos D1 Mini (ESP8266) pin assignments ---
// D0..D8 map to GPIO16,5,4,0,2,14,12,13,15 — avoid holding D3/D8 low at reset.
constexpr uint8_t PIN_TRIG      = D5;   // GPIO14 — HC-SR04 TRIG
constexpr uint8_t PIN_ECHO     = D6;   // GPIO12 — HC-SR04 ECHO
constexpr uint8_t PIN_MOTOR_UP  = D7;   // GPIO13 — open-drain: LOW = move, float = stop
constexpr uint8_t PIN_MOTOR_DN  = D1;   // GPIO5
constexpr uint8_t PIN_BTN_UP    = D2;   // GPIO4
constexpr uint8_t PIN_BTN_DN    = D4;   // GPIO2 (onboard LED on some boards)
constexpr uint8_t PIN_BTN_MEM1  = D8;   // GPIO15
constexpr uint8_t PIN_BTN_MEM2  = D3;   // GPIO0 — boot strap; do not hold low at power-on
constexpr uint8_t PIN_BTN_MEM3  = D0;   // GPIO16

constexpr size_t EEPROM_SIZE = 512;

// EEPROM: 0–11 = mem heights (3× float); 12+ = MQTT config (magic + strings)
constexpr int EEPROM_MAGIC_A = 12;
constexpr int EEPROM_MAGIC_B = 13;
constexpr int EEPROM_ADDR_MQTT_SERVER = 14;
constexpr int EEPROM_ADDR_MQTT_PORT = 62;
constexpr int EEPROM_ADDR_MQTT_USER = 70;
constexpr int EEPROM_ADDR_MQTT_PASS = 102;
constexpr int EEPROM_ADDR_MQTT_PREFIX = 134;

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

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

char mqttTopicBuf[112];
char mqttPayloadBuf[896];
char mqttCmdBuf[96];
unsigned long lastMqttPublishMs = 0;
unsigned long lastWifiReconnectMs = 0;
unsigned long lastMqttReconnectMs = 0;

char wifiMqttServer[48] = "";
char wifiMqttPort[8] = "1883";
char wifiMqttUser[32] = "";
char wifiMqttPass[32] = "";
char wifiMqttPrefix[40] = "standing-desk";

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

const char* stateToString();
void publishMqttState(bool force = false);
void handleMqttCommand(char* cmd);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void loadMqttFromEeprom();
void saveMqttToEeprom();
void applySecretsDefaults();
uint16_t mqttPortNumber();
void wifiSetupPortal();
void wifiEnsureConnected();
void mqttEnsureConnected();
void publishHomeAssistantDiscovery();

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
#if defined(ESP8266)
  EEPROM.commit();
#endif
  Serial.print(F("Saved "));
  Serial.print(addr == EEPROM_ADDR_MEM1 ? F("MEM1") :
               addr == EEPROM_ADDR_MEM2 ? F("MEM2") : F("MEM3"));
  Serial.print(F(" = "));
  Serial.print(height, 1);
  Serial.println(F(" cm"));
  publishMqttState(true);
}

static void readStringFromEeprom(int addr, char* buf, size_t maxLen) {
  for (size_t i = 0; i < maxLen - 1; i++) {
    uint8_t c = EEPROM.read(addr + (int)i);
    buf[i] = (char)c;
    if (c == 0) return;
  }
  buf[maxLen - 1] = '\0';
}

static void writeStringToEeprom(int addr, const char* buf, size_t maxLen) {
  size_t i = 0;
  for (; i < maxLen - 1 && buf[i]; i++) EEPROM.write(addr + (int)i, (uint8_t)buf[i]);
  EEPROM.write(addr + (int)i, 0);
  for (i++; i < maxLen; i++) EEPROM.write(addr + (int)i, 0);
}

void applySecretsDefaults() {
  if (MQTT_SERVER[0] != '\0')
    strncpy(wifiMqttServer, MQTT_SERVER, sizeof(wifiMqttServer) - 1);
  wifiMqttServer[sizeof(wifiMqttServer) - 1] = '\0';
  char pb[16];
  snprintf(pb, sizeof(pb), "%d", (int)MQTT_PORT);
  strncpy(wifiMqttPort, pb, sizeof(wifiMqttPort) - 1);
  wifiMqttPort[sizeof(wifiMqttPort) - 1] = '\0';
  if (MQTT_USER[0] != '\0')
    strncpy(wifiMqttUser, MQTT_USER, sizeof(wifiMqttUser) - 1);
  wifiMqttUser[sizeof(wifiMqttUser) - 1] = '\0';
  if (MQTT_PASSWORD[0] != '\0')
    strncpy(wifiMqttPass, MQTT_PASSWORD, sizeof(wifiMqttPass) - 1);
  wifiMqttPass[sizeof(wifiMqttPass) - 1] = '\0';
  if (MQTT_TOPIC_PREFIX[0] != '\0')
    strncpy(wifiMqttPrefix, MQTT_TOPIC_PREFIX, sizeof(wifiMqttPrefix) - 1);
  wifiMqttPrefix[sizeof(wifiMqttPrefix) - 1] = '\0';
}

void loadMqttFromEeprom() {
  memset(wifiMqttServer, 0, sizeof(wifiMqttServer));
  memset(wifiMqttPort, 0, sizeof(wifiMqttPort));
  wifiMqttPort[0] = '1';
  wifiMqttPort[1] = '8';
  wifiMqttPort[2] = '8';
  wifiMqttPort[3] = '3';
  wifiMqttPort[4] = '\0';
  memset(wifiMqttUser, 0, sizeof(wifiMqttUser));
  memset(wifiMqttPass, 0, sizeof(wifiMqttPass));
  memset(wifiMqttPrefix, 0, sizeof(wifiMqttPrefix));
  strncpy(wifiMqttPrefix, "standing-desk", sizeof(wifiMqttPrefix) - 1);

  if (EEPROM.read(EEPROM_MAGIC_A) != 0xA5 || EEPROM.read(EEPROM_MAGIC_B) != 0x5A) {
    applySecretsDefaults();
    return;
  }
  readStringFromEeprom(EEPROM_ADDR_MQTT_SERVER, wifiMqttServer, sizeof(wifiMqttServer));
  readStringFromEeprom(EEPROM_ADDR_MQTT_PORT, wifiMqttPort, sizeof(wifiMqttPort));
  readStringFromEeprom(EEPROM_ADDR_MQTT_USER, wifiMqttUser, sizeof(wifiMqttUser));
  readStringFromEeprom(EEPROM_ADDR_MQTT_PASS, wifiMqttPass, sizeof(wifiMqttPass));
  readStringFromEeprom(EEPROM_ADDR_MQTT_PREFIX, wifiMqttPrefix, sizeof(wifiMqttPrefix));
}

void saveMqttToEeprom() {
  EEPROM.write(EEPROM_MAGIC_A, 0xA5);
  EEPROM.write(EEPROM_MAGIC_B, 0x5A);
  writeStringToEeprom(EEPROM_ADDR_MQTT_SERVER, wifiMqttServer, 48);
  writeStringToEeprom(EEPROM_ADDR_MQTT_PORT, wifiMqttPort, 8);
  writeStringToEeprom(EEPROM_ADDR_MQTT_USER, wifiMqttUser, 32);
  writeStringToEeprom(EEPROM_ADDR_MQTT_PASS, wifiMqttPass, 32);
  writeStringToEeprom(EEPROM_ADDR_MQTT_PREFIX, wifiMqttPrefix, 40);
#if defined(ESP8266)
  EEPROM.commit();
#endif
}

uint16_t mqttPortNumber() {
  int p = atoi(wifiMqttPort);
  if (p > 0 && p < 65536) return (uint16_t)p;
  return 1883;
}

// ---------- Closed-loop movement ----------

void startMoveToTarget(float target) {
  targetHeight = target;
  moveStartTime = millis();
  currentState = STATE_MOVING_TO_TARGET;
  Serial.print(F("Moving to "));
  Serial.print(targetHeight, 1);
  Serial.println(F(" cm"));
  publishMqttState(true);
}

void cancelMove() {
  motorStop();
  currentState = STATE_IDLE;
  Serial.println(F("Move cancelled"));
  publishMqttState(true);
}

void updateMoveToTarget() {
  if (millis() - moveStartTime > MOVE_TIMEOUT_MS) {
    motorStop();
    currentState = STATE_IDLE;
    Serial.println(F("Move timeout"));
    publishMqttState(true);
    return;
  }

  if (millis() - lastSensorRead < SENSOR_INTERVAL_MS) return;
  lastSensorRead = millis();

  currentHeight = readHeight();
  float error = targetHeight - currentHeight;

  if (fabsf(error) <= HEIGHT_TOLERANCE_CM) {
    motorStop();
    currentState = STATE_IDLE;
    Serial.print(F("Target reached: "));
    Serial.print(currentHeight, 1);
    Serial.println(F(" cm"));
    publishMqttState(true);
    return;
  }

  if (error > 0) {
    motorUp();
  } else {
    motorDown();
  }
  publishMqttState(false);
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
    publishMqttState(true);
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
    publishMqttState(true);
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

// ---------- WiFi / MQTT (Home Assistant) ----------

const char* stateToString() {
  switch (currentState) {
    case STATE_IDLE: return "idle";
    case STATE_MANUAL_UP: return "manual_up";
    case STATE_MANUAL_DOWN: return "manual_down";
    case STATE_MOVING_TO_TARGET: return "moving";
    default: return "unknown";
  }
}

void publishMqttState(bool force) {
  if (wifiMqttServer[0] == '\0') return;
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) return;
  unsigned long now = millis();
  if (!force && (now - lastMqttPublishMs) < 400) return;
  lastMqttPublishMs = now;

  char val[20];
  dtostrf(currentHeight, 1, 1, val);
  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/height", wifiMqttPrefix);
  mqttClient.publish(mqttTopicBuf, val, true);

  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/state", wifiMqttPrefix);
  mqttClient.publish(mqttTopicBuf, stateToString(), true);

  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/target", wifiMqttPrefix);
  if (currentState == STATE_MOVING_TO_TARGET) {
    dtostrf(targetHeight, 1, 1, val);
    mqttClient.publish(mqttTopicBuf, val, true);
  } else {
    mqttClient.publish(mqttTopicBuf, "", true);
  }

  for (int i = 0; i < 3; i++) {
    float m = (i == 0) ? mem1Height : (i == 1) ? mem2Height : mem3Height;
    snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/preset%d", wifiMqttPrefix, i + 1);
    if (isnan(m)) {
      mqttClient.publish(mqttTopicBuf, "unknown", true);
    } else {
      dtostrf(m, 1, 1, val);
      mqttClient.publish(mqttTopicBuf, val, true);
    }
  }
}

void handleMqttCommand(char* cmd) {
  char* p = cmd;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  size_t n = strlen(p);
  while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\r' || p[n - 1] == '\n')) p[--n] = 0;

  if (strcmp(p, "stop") == 0) {
    cancelMove();
    return;
  }
  if (strcmp(p, "up") == 0) {
    if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
    motorUp();
    currentState = STATE_MANUAL_UP;
    publishMqttState(true);
    return;
  }
  if (strcmp(p, "down") == 0) {
    if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
    motorDown();
    currentState = STATE_MANUAL_DOWN;
    publishMqttState(true);
    return;
  }
  if (strcmp(p, "preset1") == 0) {
    if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
    if (!isnan(mem1Height)) startMoveToTarget(mem1Height);
    return;
  }
  if (strcmp(p, "preset2") == 0) {
    if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
    if (!isnan(mem2Height)) startMoveToTarget(mem2Height);
    return;
  }
  if (strcmp(p, "preset3") == 0) {
    if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
    if (!isnan(mem3Height)) startMoveToTarget(mem3Height);
    return;
  }
  if (strcmp(p, "save1") == 0) {
    currentHeight = readHeight();
    mem1Height = currentHeight;
    saveMemory(EEPROM_ADDR_MEM1, mem1Height);
    return;
  }
  if (strcmp(p, "save2") == 0) {
    currentHeight = readHeight();
    mem2Height = currentHeight;
    saveMemory(EEPROM_ADDR_MEM2, mem2Height);
    return;
  }
  if (strcmp(p, "save3") == 0) {
    currentHeight = readHeight();
    mem3Height = currentHeight;
    saveMemory(EEPROM_ADDR_MEM3, mem3Height);
    return;
  }
  if (strncmp(p, "move_to", 7) == 0) {
    float v;
    if (sscanf(p + 7, " %f", &v) == 1) {
      if (currentState == STATE_MOVING_TO_TARGET) cancelMove();
      startMoveToTarget(v);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  (void)topic;
  unsigned int n = length < sizeof(mqttCmdBuf) - 1 ? length : sizeof(mqttCmdBuf) - 1;
  memcpy(mqttCmdBuf, payload, n);
  mqttCmdBuf[n] = '\0';
  handleMqttCommand(mqttCmdBuf);
}

void wifiSetupPortal() {
  WiFiManager wm;
  WiFiManagerParameter p_server("mqtt_server", "MQTT broker (HA host or IP)", wifiMqttServer, 48);
  WiFiManagerParameter p_port("mqtt_port", "MQTT port", wifiMqttPort, 8);
  WiFiManagerParameter p_user("mqtt_user", "MQTT user (optional)", wifiMqttUser, 32);
  WiFiManagerParameter p_pass("mqtt_pass", "MQTT password (optional)", wifiMqttPass, 32);
  WiFiManagerParameter p_prefix("mqtt_prefix", "MQTT topic prefix", wifiMqttPrefix, 40);

  wm.addParameter(&p_server);
  wm.addParameter(&p_port);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.addParameter(&p_prefix);

  wm.setConfigPortalTimeout(300);
  wm.setConnectTimeout(120);

  bool shouldSaveConfig = false;
  wm.setSaveConfigCallback([&]() { shouldSaveConfig = true; });

  Serial.println(F("WiFiManager: connect to AP \"StandingDesk-Setup\" if prompted, then set Wi‑Fi + MQTT."));
  if (!wm.autoConnect("StandingDesk-Setup")) {
    Serial.println(F("WiFiManager: portal timed out — running without Wi‑Fi/MQTT"));
    return;
  }

  strncpy(wifiMqttServer, p_server.getValue(), sizeof(wifiMqttServer) - 1);
  wifiMqttServer[sizeof(wifiMqttServer) - 1] = '\0';
  strncpy(wifiMqttPort, p_port.getValue(), sizeof(wifiMqttPort) - 1);
  wifiMqttPort[sizeof(wifiMqttPort) - 1] = '\0';
  strncpy(wifiMqttUser, p_user.getValue(), sizeof(wifiMqttUser) - 1);
  wifiMqttUser[sizeof(wifiMqttUser) - 1] = '\0';
  strncpy(wifiMqttPass, p_pass.getValue(), sizeof(wifiMqttPass) - 1);
  wifiMqttPass[sizeof(wifiMqttPass) - 1] = '\0';
  strncpy(wifiMqttPrefix, p_prefix.getValue(), sizeof(wifiMqttPrefix) - 1);
  wifiMqttPrefix[sizeof(wifiMqttPrefix) - 1] = '\0';

  if (shouldSaveConfig) saveMqttToEeprom();

  Serial.print(F("WiFi OK "));
  Serial.println(WiFi.localIP());
}

void wifiEnsureConnected() {
  if (WiFi.status() == WL_CONNECTED) return;
  unsigned long now = millis();
  if (now - lastWifiReconnectMs < 5000) return;
  lastWifiReconnectMs = now;
  Serial.println(F("WiFi reconnecting..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void mqttEnsureConnected() {
  if (wifiMqttServer[0] == '\0') return;
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqttClient.connected()) return;
  unsigned long now = millis();
  if (now - lastMqttReconnectMs < 4000) return;
  lastMqttReconnectMs = now;

  mqttClient.setServer(wifiMqttServer, mqttPortNumber());
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(1024);

  String clientId = String("standing-desk-") + String(ESP.getChipId(), HEX);
  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/status", wifiMqttPrefix);

  bool ok;
  if (wifiMqttUser[0] != '\0')
    ok = mqttClient.connect(clientId.c_str(), wifiMqttUser, wifiMqttPass,
                            mqttTopicBuf, 0, true, "offline");
  else
    ok = mqttClient.connect(clientId.c_str(), mqttTopicBuf, 0, true, "offline");

  if (!ok) {
    Serial.print(F("MQTT connect failed rc="));
    Serial.println(mqttClient.state());
    return;
  }

  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/cmd", wifiMqttPrefix);
  mqttClient.subscribe(mqttTopicBuf);
  Serial.print(F("MQTT cmd topic: "));
  Serial.println(mqttTopicBuf);

  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/status", wifiMqttPrefix);
  mqttClient.publish(mqttTopicBuf, "online", true);

  publishHomeAssistantDiscovery();
  publishMqttState(true);
  Serial.println(F("MQTT connected"));
}

void publishHomeAssistantDiscovery() {
  if (wifiMqttServer[0] == '\0' || !mqttClient.connected()) return;

  char uid[24];
  snprintf(uid, sizeof(uid), "sd_%08x", ESP.getChipId());
  const char* pre = wifiMqttPrefix;

  char statTopic[64];
  char cmdTopic[64];
  char heightTopic[64];
  char stateTopic[64];
  snprintf(statTopic, sizeof(statTopic), "%s/status", pre);
  snprintf(cmdTopic, sizeof(cmdTopic), "%s/cmd", pre);
  snprintf(heightTopic, sizeof(heightTopic), "%s/height", pre);
  snprintf(stateTopic, sizeof(stateTopic), "%s/state", pre);

  snprintf(mqttPayloadBuf, sizeof(mqttPayloadBuf),
           "{\"name\":\"Desk height\",\"unique_id\":\"%s_height\",\"state_topic\":\"%s\","
           "\"unit_of_measurement\":\"cm\",\"availability_topic\":\"%s\","
           "\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Standing Desk\",\"model\":\"D1 Mini\",\"sw_version\":\"1\"}}",
           uid, heightTopic, statTopic, uid);
  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "homeassistant/sensor/%s_height/config", uid);
  mqttClient.publish(mqttTopicBuf, mqttPayloadBuf, true);

  snprintf(mqttPayloadBuf, sizeof(mqttPayloadBuf),
           "{\"name\":\"Desk state\",\"unique_id\":\"%s_state\",\"state_topic\":\"%s\","
           "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Standing Desk\",\"model\":\"D1 Mini\",\"sw_version\":\"1\"}}",
           uid, stateTopic, statTopic, uid);
  snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "homeassistant/sensor/%s_state/config", uid);
  mqttClient.publish(mqttTopicBuf, mqttPayloadBuf, true);

  static const char* btnId[] = {"up", "down", "stop", "preset1", "preset2", "preset3",
                                "save1", "save2", "save3"};
  static const char* btnName[] = {"Desk up", "Desk down", "Desk stop", "Preset 1", "Preset 2", "Preset 3",
                                  "Save preset 1", "Save preset 2", "Save preset 3"};
  for (int i = 0; i < 9; i++) {
    snprintf(mqttPayloadBuf, sizeof(mqttPayloadBuf),
             "{\"name\":\"%s\",\"unique_id\":\"%s_btn_%s\",\"command_topic\":\"%s\","
             "\"payload_press\":\"%s\",\"availability_topic\":\"%s\","
             "\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Standing Desk\",\"model\":\"D1 Mini\",\"sw_version\":\"1\"}}",
             btnName[i], uid, btnId[i], cmdTopic, btnId[i], statTopic, uid);
    snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "homeassistant/button/%s_btn_%s/config", uid, btnId[i]);
    mqttClient.publish(mqttTopicBuf, mqttPayloadBuf, true);
  }
}

// ---------- Setup ----------

void setup() {
  Serial.begin(115200);
#if defined(ESP8266)
  EEPROM.begin(EEPROM_SIZE);
#endif

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
  loadMqttFromEeprom();
  wifiSetupPortal();

  Serial.println(F("==========================="));
  Serial.println(F(" Standing Desk Controller"));
  Serial.println(F("==========================="));

  Serial.println(F("[Pins] Wemos D1 Mini"));
  Serial.println(F("  Sensor:  TRIG=D5(GPIO14) ECHO=D6(GPIO12)"));
  Serial.println(F("  Motor:   UP=D7(GPIO13) DN=D1(GPIO5)"));
  Serial.println(F("  Buttons: UP=D2(GPIO4) DN=D4(GPIO2) M1=D8(GPIO15) M2=D3(GPIO0) M3=D0(GPIO16)"));

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

  if (wifiMqttServer[0] != '\0') {
    Serial.println(F("[MQTT]"));
    Serial.print(F("  Broker:  ")); Serial.print(wifiMqttServer);
    Serial.print(F(":")); Serial.println(mqttPortNumber());
    Serial.print(F("  Prefix:  ")); Serial.println(wifiMqttPrefix);
    snprintf(mqttTopicBuf, sizeof(mqttTopicBuf), "%s/cmd", wifiMqttPrefix);
    Serial.print(F("  Command: publish to ")); Serial.println(mqttTopicBuf);
    Serial.println(F("  HA: enable MQTT integration; discovery is sent on connect"));
  } else {
    Serial.println(F("[MQTT] disabled (empty broker in WiFiManager portal)"));
  }

  Serial.println(F("==========================="));
  Serial.println(F("Ready."));
}

// ---------- Loop ----------

void loop() {
  wifiEnsureConnected();
  mqttEnsureConnected();
  mqttClient.loop();

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
    publishMqttState(false);
  }

  yield();
}
