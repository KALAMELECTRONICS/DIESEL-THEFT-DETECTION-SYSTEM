// FUELGUARD - SMART ANTI FUEL THEFT DETECTION SYSTEM
// Developed by KALAM ELECTRONICS
// Credit Date: 28.09.2026

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID = "Kalam_004";
const char* WIFI_PASSWORD = "kalam@202602";

const char* AP_SSID = "FuelGuard_ESP32";
const char* AP_PASSWORD = "12345678";

// Replace these with your NEW Telegram BotFather credentials.

#define BOT_TOKEN "8914420035:AAE1_24_JF_fBAW0MjmEWof5hoIXzXzhqNw"

#define CHAT_ID "8932646737"

// ---------------- HC-SR04 ----------------

#define TRIG_PIN 5
#define ECHO_PIN 18

// ---------------- SENSORS ----------------

#define VIBRATION_PIN 14
#define REED_PIN      13

// ---------------- OUTPUTS ----------------

#define BUZZER_PIN    25
#define RED_LED_PIN   26
#define GREEN_LED_PIN 27

// ---------------- BUTTONS ----------------

#define KEY_BUTTON_PIN 32

// ALARM RESET
#define ALARM_RESET_BUTTON_PIN 33

// ---------------- OLED ----------------

#define OLED_SDA 21
#define OLED_SCL 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

WebServer server(80);

WiFiClientSecure secured_client;

UniversalTelegramBot bot(
  BOT_TOKEN,
  secured_client
);

unsigned long lastTelegramCheck = 0;

const unsigned long TELEGRAM_INTERVAL = 2000;

const float TANK_HEIGHT_CM = 13.0;

// Distance from ultrasonic sensor to bottom of tank.
// Distance from ultrasonic sensor to maximum fuel level.
// start with approximately 1 cm for FULL.

const float TANK_EMPTY_DISTANCE = 13.0;
const float TANK_FULL_DISTANCE  = 1.0;

// Fuel percentage drop required for theft detection.

const float FUEL_DROP_THRESHOLD = 10.0;

const float FUEL_DEADBAND = 0.5;

const unsigned long FUEL_SENSOR_INTERVAL = 150;

const unsigned long HISTORY_INTERVAL = 5000;

// OLED update

const unsigned long OLED_INTERVAL = 250;

// ---------------- Fuel ----------------

float fuelPercent = 0.0;
float fuelLitres = 0.0;

float previousFuelPercent = 0.0;

float currentDistance = 0.0;

// ---------------- Key ----------------

bool keyOn = false;

// ---------------- Alarm states ----------------

// Overall alarm

bool theftDetected = false;

// Individual alarms

bool fuelTheftDetected = false;
bool vibrationAlert = false;
bool reedAlert = false;

// ---------------- Alarm reset ----------------

bool alarmMuted = false;

// ---------------- Sensor reset locks ----------------

// while sensor is still active.

bool vibrationResetReady = true;

// while switch remains activated.

bool reedResetReady = true;

// ---------------- Buttons ----------------

bool previousKeyButtonState = HIGH;
bool previousAlarmResetButtonState = HIGH;

// ---------------- Timing ----------------

unsigned long lastFuelSensorRead = 0;
unsigned long lastHistoryRecord = 0;
unsigned long lastOLEDUpdate = 0;

struct HistoryRecord {

  unsigned long time;

  float fuel;

  float litres;

  bool key;

  bool theft;

};

#define MAX_HISTORY 100

HistoryRecord historyData[MAX_HISTORY];

int historyCount = 0;

float readDistanceCM();

float calculateFuelPercent(
  float distance
);

void readFuelSensor();

void checkVibration();

void checkReedSwitch();

void handleButtons();

void triggerAlarm(
  const char* reason
);

void resetAlarm();

void updateOutputs();

void updateOLED();

void addHistoryRecord();

void sendFuelTheftAlert();

void sendVibrationAlert();

void sendReedAlert();

void sendKeyMessage(
  bool state
);

void handleTelegramMessages();

void setupWiFi();

void handleMainPage();

void handleStatus();

void handleHistory();

void handleNotFound();

float readDistanceCM() {

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  delayMicroseconds(2);

  digitalWrite(
    TRIG_PIN,
    HIGH
  );

  delayMicroseconds(10);

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  long duration = pulseIn(
    ECHO_PIN,
    HIGH,
    10000
  );

  if (duration <= 0) {

    return -1;
  }

  float distance =
    duration * 0.0343 / 2.0;

  // Reject impossible values

  if (
    distance < 1.0 ||
    distance > 20.0
  ) {

    return -1;
  }

  return distance;
}

float calculateFuelPercent(
  float distance
) {

  if (
    distance <= TANK_FULL_DISTANCE
  ) {

    return 100.0;
  }

  if (
    distance >= TANK_EMPTY_DISTANCE
  ) {

    return 0.0;
  }

  float percentage =

    (
      (
        TANK_EMPTY_DISTANCE -
        distance
      )
      /
      (
        TANK_EMPTY_DISTANCE -
        TANK_FULL_DISTANCE
      )
    )
    * 100.0;

  return constrain(
    percentage,
    0.0,
    100.0
  );
}

void readFuelSensor() {

  float distance =
    readDistanceCM();

  if (distance < 0) {

    return;
  }

  currentDistance =
    distance;

  float newFuelPercent =
    calculateFuelPercent(
      distance
    );

  if (
    abs(
      newFuelPercent -
      fuelPercent
    )
    <
    FUEL_DEADBAND
  ) {

    newFuelPercent =
      fuelPercent;
  }

  // FUEL DROP DETECTION

  float fuelDrop =
    previousFuelPercent -
    newFuelPercent;

  if (
    !keyOn &&
    fuelDrop >= FUEL_DROP_THRESHOLD &&
    !fuelTheftDetected
  ) {

    fuelTheftDetected = true;

    triggerAlarm(
      "SUDDEN FUEL DECREASE"
    );

    sendFuelTheftAlert();
  }

  // UPDATE FUEL VALUE

  fuelPercent =
    newFuelPercent;

  // use percentage only.

  const float TANK_CAPACITY_LITRES = 20.0;

  fuelLitres =
    (
      fuelPercent /
      100.0
    )
    *
    TANK_CAPACITY_LITRES;

  previousFuelPercent =
    fuelPercent;
}

void checkVibration() {

  int vibrationState =
    digitalRead(
      VIBRATION_PIN
    );

  // SW-420 normally becomes HIGH
  // when vibration is detected.

  if (
    !keyOn &&
    vibrationState == HIGH &&
    vibrationResetReady
  ) {

    vibrationResetReady =
      false;

    vibrationAlert =
      true;

    triggerAlarm(
      "VIBRATION DETECTED"
    );

    sendVibrationAlert();
  }

  // before another vibration event
  // can be detected.

  if (
    vibrationState == LOW
  ) {

    vibrationResetReady =
      true;
  }
}

void checkReedSwitch() {

  int reedState =
    digitalRead(
      REED_PIN
    );

  // INPUT_PULLUP / module polarity:
  // HIGH = switch activated
  // LOW = normal

  if (
    !keyOn &&
    reedState == HIGH &&
    reedResetReady
  ) {

    reedResetReady =
      false;

    reedAlert =
      true;

    triggerAlarm(
      "REED SWITCH ACTIVATED"
    );

    sendReedAlert();
  }

  // accepting another event.

  if (
    reedState == LOW
  ) {

    reedResetReady =
      true;
  }
}

void triggerAlarm(
  const char* reason
) {

  theftDetected =
    true;

  alarmMuted =
    false;

  // IMMEDIATE HARDWARE RESPONSE

  digitalWrite(
    BUZZER_PIN,
    HIGH
  );

  digitalWrite(
    RED_LED_PIN,
    HIGH
  );

  digitalWrite(
    GREEN_LED_PIN,
    LOW
  );

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "!!! SECURITY ALERT !!!"
  );

  Serial.print(
    "Reason: "
  );

  Serial.println(
    reason
  );

  Serial.print(
    "Fuel: "
  );

  Serial.print(
    fuelPercent,
    1
  );

  Serial.println(
    "%"
  );

  Serial.println(
    "BUZZER -> ON"
  );

  Serial.println(
    "================================"
  );

}

void resetAlarm() {

  theftDetected =
    false;

  fuelTheftDetected =
    false;

  vibrationAlert =
    false;

  reedAlert =
    false;

  alarmMuted =
    false;

  // back to normal.

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  digitalWrite(
    RED_LED_PIN,
    LOW
  );

  digitalWrite(
    GREEN_LED_PIN,
    HIGH
  );

  previousFuelPercent =
    fuelPercent;

  // vibration/reed while the sensor
  // is still active.

  vibrationResetReady =
    false;

  reedResetReady =
    false;

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "ALARM RESET"
  );

  Serial.println(
    "BUZZER -> OFF"
  );

  Serial.println(
    "RED LED -> OFF"
  );

  Serial.println(
    "GREEN LED -> ON"
  );

  Serial.println(
    "SYSTEM -> NORMAL"
  );

  Serial.println(
    "================================"
  );
}

void handleButtons() {

  bool keyButtonState =
    digitalRead(
      KEY_BUTTON_PIN
    );

  bool resetButtonState =
    digitalRead(
      ALARM_RESET_BUTTON_PIN
    );

  if (
    previousKeyButtonState == HIGH &&
    keyButtonState == LOW
  ) {

    keyOn =
      !keyOn;

    // KEY ON

    if (keyOn) {

      Serial.println();
      Serial.println(
        "KEY -> ON"
      );

      Serial.println(
        "SECURITY -> DISARMED"
      );

      // Clear existing alarm

      theftDetected =
        false;

      fuelTheftDetected =
        false;

      vibrationAlert =
        false;

      reedAlert =
        false;

      alarmMuted =
        false;

      previousFuelPercent =
        fuelPercent;

      // Normal outputs

      digitalWrite(
        BUZZER_PIN,
        LOW
      );

      digitalWrite(
        RED_LED_PIN,
        LOW
      );

      digitalWrite(
        GREEN_LED_PIN,
        HIGH
      );

      sendKeyMessage(
        true
      );
    }

    // KEY OFF

    else {

      Serial.println();
      Serial.println(
        "KEY -> OFF"
      );

      Serial.println(
        "SECURITY -> ARMED"
      );

      // Start new fuel reference

      previousFuelPercent =
        fuelPercent;

      sendKeyMessage(
        false
      );
    }

    // Does NOT affect alarm response.

    delay(30);
  }

  previousKeyButtonState =
    keyButtonState;

  if (
    previousAlarmResetButtonState == HIGH &&
    resetButtonState == LOW
  ) {

    resetAlarm();

    delay(30);
  }

  previousAlarmResetButtonState =
    resetButtonState;
}

void updateOutputs() {

  if (theftDetected) {

    digitalWrite(
      RED_LED_PIN,
      HIGH
    );

    digitalWrite(
      GREEN_LED_PIN,
      LOW
    );

    if (!alarmMuted) {

      digitalWrite(
        BUZZER_PIN,
        HIGH
      );

    } else {

      digitalWrite(
        BUZZER_PIN,
        LOW
      );
    }

  }

  else {

    digitalWrite(
      RED_LED_PIN,
      LOW
    );

    digitalWrite(
      GREEN_LED_PIN,
      HIGH
    );

    digitalWrite(
      BUZZER_PIN,
      LOW
    );
  }
}

void updateOLED() {

  display.clearDisplay();

  display.setTextColor(
    SSD1306_WHITE
  );

  display.setTextSize(1);

  // TITLE

  display.setCursor(
    0,
    0
  );

  display.println(
    "FUELGUARD"
  );

  display.drawLine(
    0,
    10,
    127,
    10,
    SSD1306_WHITE
  );

  display.setCursor(
    0,
    15
  );

  display.print(
    "Fuel: "
  );

  display.print(
    fuelPercent,
    1
  );

  display.println(
    "%"
  );

  // DISTANCE

  display.setCursor(
    0,
    26
  );

  display.print(
    "Dist: "
  );

  display.print(
    currentDistance,
    1
  );

  display.println(
    " cm"
  );

  // KEY

  display.setCursor(
    0,
    37
  );

  display.print(
    "Key: "
  );

  if (keyOn) {

    display.println(
      "ON"
    );

  } else {

    display.println(
      "OFF"
    );
  }

  // SECURITY

  display.setCursor(
    0,
    48
  );

  display.print(
    "Security: "
  );

  if (theftDetected) {

    display.println(
      "ALERT"
    );

  } else {

    display.println(
      "SAFE"
    );
  }

  // ALARM

  display.setCursor(
    82,
    37
  );

  if (
    theftDetected &&
    !alarmMuted
  ) {

    display.println(
      "ALM ON"
    );

  } else {

    display.println(
      "ALM OFF"
    );
  }

  display.display();
}

void addHistoryRecord() {

  if (
    historyCount <
    MAX_HISTORY
  ) {

    historyData[
      historyCount
    ].time =
      millis();

    historyData[
      historyCount
    ].fuel =
      fuelPercent;

    historyData[
      historyCount
    ].litres =
      fuelLitres;

    historyData[
      historyCount
    ].key =
      keyOn;

    historyData[
      historyCount
    ].theft =
      theftDetected;

    historyCount++;
  }

  else {

    for (
      int i = 1;
      i < MAX_HISTORY;
      i++
    ) {

      historyData[i - 1] =
        historyData[i];
    }

    int last =
      MAX_HISTORY - 1;

    historyData[last].time =
      millis();

    historyData[last].fuel =
      fuelPercent;

    historyData[last].litres =
      fuelLitres;

    historyData[last].key =
      keyOn;

    historyData[last].theft =
      theftDetected;
  }
}

// TELEGRAM - FUEL

void sendFuelTheftAlert() {

  String message;

  message +=
    "FUELGUARD ALERT\n\n";

  message +=
    "SUDDEN FUEL DECREASE\n\n";

  message +=
    "Fuel: ";

  message +=
    String(
      fuelPercent,
      1
    );

  message +=
    "%\n";

  message +=
    "Volume: ";

  message +=
    String(
      fuelLitres,
      1
    );

  message +=
    " L\n";

  message +=
    "Key: OFF\n";

  message +=
    "Alarm: ON";

  bot.sendMessage(
    CHAT_ID,
    message,
    ""
  );
}

// TELEGRAM - VIBRATION

void sendVibrationAlert() {

  String message;

  message +=
    "FUELGUARD ALERT\n\n";

  message +=
    "VIBRATION DETECTED\n\n";

  message +=
    "Fuel: ";

  message +=
    String(
      fuelPercent,
      1
    );

  message +=
    "%\n";

  message +=
    "Key: OFF\n";

  message +=
    "Alarm: ON";

  bot.sendMessage(
    CHAT_ID,
    message,
    ""
  );
}

// TELEGRAM - REED

void sendReedAlert() {

  String message;

  message +=
    "FUELGUARD ALERT\n\n";

  message +=
    "REED SWITCH ACTIVATED\n\n";

  message +=
    "Fuel: ";

  message +=
    String(
      fuelPercent,
      1
    );

  message +=
    "%\n";

  message +=
    "Key: OFF\n";

  message +=
    "Alarm: ON";

  bot.sendMessage(
    CHAT_ID,
    message,
    ""
  );
}

// TELEGRAM - KEY

void sendKeyMessage(
  bool state
) {

  String message;

  message +=
    "FUELGUARD\n\n";

  message +=
    "KEY: ";

  if (state) {

    message +=
      "ON\n";

    message +=
      "Security: DISARMED";

  } else {

    message +=
      "OFF\n";

    message +=
      "Security: ARMED";
  }

  bot.sendMessage(
    CHAT_ID,
    message,
    ""
  );
}

void handleTelegramMessages() {

  if (
    millis() -
    lastTelegramCheck
    <
    TELEGRAM_INTERVAL
  ) {

    return;
  }

  lastTelegramCheck =
    millis();

  int messages =
    bot.getUpdates(
      bot.last_message_received + 1
    );

  while (messages) {

    for (
      int i = 0;
      i < messages;
      i++
    ) {

      String chatID =
        String(
          bot.messages[i].chat_id
        );

      String text =
        bot.messages[i].text;

      if (
        chatID !=
        String(CHAT_ID)
      ) {

        bot.sendMessage(
          chatID,
          "Unauthorized user.",
          ""
        );

        continue;
      }

      if (
        text == "/start"
      ) {

        String msg;

        msg +=
          "FUELGUARD\n\n";

        msg +=
          "/status\n";

        msg +=
          "/fuel\n";

        msg +=
          "/sensors\n";

        msg +=
          "/help";

        bot.sendMessage(
          chatID,
          msg,
          ""
        );
      }

      else if (
        text == "/help"
      ) {

        String msg;

        msg +=
          "FUELGUARD COMMANDS\n\n";

        msg +=
          "/status - System status\n";

        msg +=
          "/fuel - Fuel level\n";

        msg +=
          "/sensors - Sensor status\n";

        msg +=
          "/help - Commands";

        bot.sendMessage(
          chatID,
          msg,
          ""
        );
      }

      else if (
        text == "/status"
      ) {

        String msg;

        msg +=
          "FUELGUARD STATUS\n\n";

        msg +=
          "Fuel: ";

        msg +=
          String(
            fuelPercent,
            1
          );

        msg +=
          "%\n";

        msg +=
          "Distance: ";

        msg +=
          String(
            currentDistance,
            1
          );

        msg +=
          " cm\n";

        msg +=
          "Key: ";

        msg +=
          keyOn ?
          "ON\n" :
          "OFF\n";

        msg +=
          "Security: ";

        msg +=
          theftDetected ?
          "ALERT\n" :
          "SAFE\n";

        msg +=
          "Alarm: ";

        msg +=
          (
            theftDetected &&
            !alarmMuted
          ) ?
          "ON" :
          "OFF";

        bot.sendMessage(
          chatID,
          msg,
          ""
        );
      }

      else if (
        text == "/fuel"
      ) {

        String msg;

        msg +=
          "CURRENT FUEL\n\n";

        msg +=
          "Level: ";

        msg +=
          String(
            fuelPercent,
            1
          );

        msg +=
          "%\n";

        msg +=
          "Distance: ";

        msg +=
          String(
            currentDistance,
            1
          );

        msg +=
          " cm\n";

        msg +=
          "Volume: ";

        msg +=
          String(
            fuelLitres,
            1
          );

        msg +=
          " L";

        bot.sendMessage(
          chatID,
          msg,
          ""
        );
      }

      else if (
        text == "/sensors"
      ) {

        String msg;

        msg +=
          "SENSOR STATUS\n\n";

        msg +=
          "Vibration: ";

        msg +=
          digitalRead(
            VIBRATION_PIN
          ) ?
          "DETECTED\n" :
          "NORMAL\n";

        msg +=
          "Reed: ";

        msg +=
          digitalRead(
            REED_PIN
          ) == HIGH ?
          "ACTIVATED\n" :
          "NORMAL\n";

        msg +=
          "Fuel Alert: ";

        msg +=
          fuelTheftDetected ?
          "YES\n" :
          "NO\n";

        msg +=
          "Security: ";

        msg +=
          theftDetected ?
          "ALERT" :
          "SAFE";

        bot.sendMessage(
          chatID,
          msg,
          ""
        );
      }
    }

    messages =
      bot.getUpdates(
        bot.last_message_received + 1
      );
  }
}

const char MAIN_PAGE[] PROGMEM = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>FuelGuard | ESP32 Security</title>

<style>

/* =====================================================
   GLOBAL
===================================================== */

*{
  box-sizing:border-box;
}

:root{
  --bg:#f5f7fb;
  --card:#ffffff;
  --text:#172033;
  --muted:#7b8495;
  --border:#e7eaf0;
  --primary:#2563eb;
  --primary-light:#eff6ff;
  --green:#16a34a;
  --green-light:#ecfdf3;
  --red:#dc2626;
  --red-light:#fef2f2;
  --orange:#ea580c;
  --orange-light:#fff7ed;
  --shadow:
    0 8px 30px rgba(31,41,55,.07);
}

body{

  margin:0;

  font-family:
  Inter,
  Arial,
  sans-serif;

  background:
  linear-gradient(
    135deg,
    #f8fafc,
    #eef3f9
  );

  color:var(--text);

  min-height:100vh;
}

/* =====================================================
   PAGE ANIMATION
===================================================== */

@keyframes pageLoad{

  from{
    opacity:0;
    transform:translateY(15px);
  }

  to{
    opacity:1;
    transform:translateY(0);
  }

}

.container{
  animation:
    pageLoad .7s ease;
}

/* =====================================================
   HEADER
===================================================== */

header{

  height:76px;

  background:
  rgba(255,255,255,.92);

  backdrop-filter:
  blur(15px);

  border-bottom:
  1px solid var(--border);

  display:flex;

  align-items:center;

  justify-content:space-between;

  padding:
  0 30px;

  position:sticky;

  top:0;

  z-index:100;

  box-shadow:
  0 4px 20px
  rgba(15,23,42,.04);
}

/* LOGO */

.brand{
  display:flex;
  align-items:center;
  gap:12px;
}

.logo-box{

  width:42px;
  height:42px;

  border-radius:12px;

  display:flex;

  align-items:center;

  justify-content:center;

  color:white;

  font-size:20px;

  font-weight:900;

  background:
  linear-gradient(
    135deg,
    #2563eb,
    #4f46e5
  );

  box-shadow:
  0 6px 18px
  rgba(37,99,235,.25);
}

.logo{

  font-size:22px;

  font-weight:900;

  letter-spacing:-.5px;
}

.subtitle{

  color:var(--muted);

  font-size:12px;

  margin-top:2px;
}

/* LIVE INDICATOR */

.live-status{

  display:flex;

  align-items:center;

  gap:8px;

  font-size:13px;

  font-weight:700;

  color:#475569;

  padding:
  9px 14px;

  background:#f8fafc;

  border:
  1px solid var(--border);

  border-radius:30px;
}

.live-dot{

  width:9px;

  height:9px;

  background:#22c55e;

  border-radius:50%;

  box-shadow:
  0 0 0 0
  rgba(34,197,94,.5);

  animation:
  livePulse 1.8s infinite;
}

@keyframes livePulse{

  0%{
    box-shadow:
    0 0 0 0
    rgba(34,197,94,.5);
  }

  70%{
    box-shadow:
    0 0 0 8px
    rgba(34,197,94,0);
  }

  100%{
    box-shadow:
    0 0 0 0
    rgba(34,197,94,0);
  }

}

/* =====================================================
   MAIN CONTAINER
===================================================== */

.container{

  max-width:1200px;

  margin:auto;

  padding:
  30px 22px 50px;
}

/* =====================================================
   PAGE TITLE
===================================================== */

.page-heading{

  margin-bottom:25px;
}

.page-heading h1{

  margin:0;

  font-size:28px;

  letter-spacing:-.7px;
}

.page-heading p{

  margin:
  7px 0 0;

  color:var(--muted);

  font-size:14px;
}

/* =====================================================
   STATUS CARDS
===================================================== */

.grid{

  display:grid;

  grid-template-columns:
  repeat(
    auto-fit,
    minmax(210px,1fr)
  );

  gap:16px;
}

/* CARD */

.card{

  background:
  rgba(255,255,255,.94);

  border:
  1px solid rgba(226,232,240,.8);

  border-radius:18px;

  box-shadow:
  var(--shadow);

  transition:
  transform .3s ease,
  box-shadow .3s ease,
  border-color .3s ease;

  position:relative;

  overflow:hidden;
}

.card:hover{

  transform:
  translateY(-5px);

  box-shadow:
  0 15px 35px
  rgba(15,23,42,.11);

  border-color:
  #dbe3ef;
}

/* CARD SHINE */

.card::before{

  content:"";

  position:absolute;

  width:120px;

  height:120px;

  right:-60px;

  top:-60px;

  background:
  rgba(37,99,235,.035);

  border-radius:50%;
}

/* STATUS CARD */

.status-card{

  padding:20px;
}

.card-top{

  display:flex;

  align-items:center;

  justify-content:space-between;

  margin-bottom:17px;
}

.title{

  font-size:12px;

  font-weight:800;

  letter-spacing:.8px;

  color:var(--muted);
}

.icon{

  width:38px;

  height:38px;

  border-radius:11px;

  display:flex;

  align-items:center;

  justify-content:center;

  background:
  var(--primary-light);

  color:
  var(--primary);

  font-size:17px;

  font-weight:900;
}

.value{

  font-size:27px;

  font-weight:900;

  letter-spacing:-.7px;

  transition:
  all .3s ease;
}

/* =====================================================
   STATUS BADGES
===================================================== */

.badge{

  display:inline-flex;

  align-items:center;

  gap:6px;

  padding:
  7px 11px;

  border-radius:20px;

  font-size:11px;

  font-weight:800;

  margin-top:10px;
}

.badge-dot{

  width:6px;

  height:6px;

  border-radius:50%;

  background:currentColor;
}

.badge.safe{

  color:
  var(--green);

  background:
  var(--green-light);
}

.badge.alert{

  color:
  var(--red);

  background:
  var(--red-light);

  animation:
  alertPulse 1.5s infinite;
}

@keyframes alertPulse{

  0%,100%{
    box-shadow:
    0 0 0 0
    rgba(220,38,38,.15);
  }

  50%{
    box-shadow:
    0 0 0 6px
    rgba(220,38,38,0);
  }

}

/* =====================================================
   FUEL DIGITAL METER
===================================================== */

.fuel-panel{

  margin-top:20px;

  padding:25px;
}

/* HEADER */

.section-header{

  display:flex;

  align-items:center;

  justify-content:space-between;

  margin-bottom:22px;
}

.section-header h2{

  margin:0;

  font-size:19px;
}

.section-header span{

  font-size:12px;

  color:var(--muted);
}

/* DIGITAL FUEL DISPLAY */

.digital-meter{

  text-align:center;

  padding:
  12px 10px 28px;
}

.digital-number{

  font-size:
  clamp(
    55px,
    9vw,
    88px
  );

  line-height:1;

  font-weight:900;

  letter-spacing:-4px;

  background:
  linear-gradient(
    135deg,
    #111827,
    #2563eb
  );

  -webkit-background-clip:text;

  -webkit-text-fill-color:
  transparent;

  animation:
  numberAppear .8s ease;
}

@keyframes numberAppear{

  from{
    opacity:0;
    transform:
    scale(.9);
  }

  to{
    opacity:1;
    transform:
    scale(1);
  }

}

.digital-label{

  margin-top:9px;

  font-size:11px;

  font-weight:800;

  letter-spacing:2px;

  color:var(--muted);
}

/* =====================================================
   FUEL PROGRESS BAR
===================================================== */

.fuel-progress-wrapper{

  max-width:850px;

  margin:auto;
}

.fuel-progress{

  height:18px;

  background:#e9edf3;

  border-radius:30px;

  overflow:hidden;

  box-shadow:
  inset 0 2px 5px
  rgba(15,23,42,.08);
}

.fuel-progress-bar{

  height:100%;

  width:0%;

  border-radius:30px;

  background:
  linear-gradient(
    90deg,
    #ef4444,
    #f59e0b,
    #22c55e
  );

  transition:
  width 1s
  cubic-bezier(
    .22,
    1,
    .36,
    1
  );

  position:relative;

  overflow:hidden;
}

/* MOVING SHINE */

.fuel-progress-bar::after{

  content:"";

  position:absolute;

  top:0;

  left:-80px;

  width:70px;

  height:100%;

  background:
  linear-gradient(
    90deg,
    transparent,
    rgba(255,255,255,.65),
    transparent
  );

  animation:
  progressShine 2.2s
  infinite;
}

@keyframes progressShine{

  from{
    left:-80px;
  }

  to{
    left:100%;
  }

}

.fuel-scale{

  display:flex;

  justify-content:space-between;

  margin-top:9px;

  color:#94a3b8;

  font-size:11px;

  font-weight:700;
}

/* =====================================================
   SYSTEM STATUS
===================================================== */

.system-card{

  margin-top:20px;

  padding:25px;
}

.status-list{

  display:grid;

  grid-template-columns:
  repeat(
    auto-fit,
    minmax(180px,1fr)
  );

  gap:12px;

  margin-top:20px;
}

.status-item{

  padding:15px;

  border:
  1px solid var(--border);

  border-radius:13px;

  background:#fafbfc;

  transition:
  all .25s ease;
}

.status-item:hover{

  background:#f8fafc;

  transform:
  translateX(3px);
}

.status-item-label{

  font-size:11px;

  font-weight:700;

  color:var(--muted);

  margin-bottom:7px;
}

.status-item-value{

  font-size:15px;

  font-weight:900;
}

/* =====================================================
   HISTORY DOWNLOAD BUTTON
===================================================== */

.download-btn{

  border:0;

  background:
  linear-gradient(
    135deg,
    #2563eb,
    #4f46e5
  );

  color:#ffffff;

  padding:
  10px 15px;

  border-radius:10px;

  font-size:11px;

  font-weight:800;

  letter-spacing:.4px;

  cursor:pointer;

  box-shadow:
  0 5px 15px
  rgba(37,99,235,.20);

  transition:
  transform .2s ease,
  box-shadow .2s ease,
  opacity .2s ease;
}

.download-btn:hover{

  transform:
  translateY(-2px);

  box-shadow:
  0 8px 20px
  rgba(37,99,235,.30);
}

.download-btn:active{

  transform:
  translateY(0);
}

.download-btn:disabled{

  opacity:.6;

  cursor:not-allowed;
}

/* =====================================================
   HISTORY
===================================================== */

.history-card{

  margin-top:20px;

  padding:25px;
}

.table-wrapper{

  overflow-x:auto;

  margin-top:18px;
}

table{

  width:100%;

  border-collapse:
  separate;

  border-spacing:0;

  min-width:600px;
}

th{

  padding:
  13px 14px;

  background:#f8fafc;

  color:#64748b;

  font-size:11px;

  text-transform:
  uppercase;

  letter-spacing:.7px;

  text-align:left;

  border-bottom:
  1px solid var(--border);
}

th:first-child{
  border-radius:10px 0 0 0;
}

th:last-child{
  border-radius:0 10px 0 0;
}

td{

  padding:
  14px;

  border-bottom:
  1px solid #edf0f4;

  font-size:13px;

  font-weight:600;
}

tbody tr{

  transition:
  background .2s ease;
}

tbody tr:hover{

  background:#f8fafc;
}

/* =====================================================
   FOOTER
===================================================== */

.footer{

  text-align:center;

  margin-top:30px;

  color:#94a3b8;

  font-size:11px;
}

/* =====================================================
   RESPONSIVE
===================================================== */

@media(max-width:600px){

  header{

    padding:
    0 15px;

    height:68px;
  }

  .subtitle{

    display:none;
  }

  .live-status{

    padding:
    7px 10px;

    font-size:11px;
  }

  .container{

    padding:
    22px 14px 40px;
  }

  .page-heading h1{

    font-size:24px;
  }

  .fuel-panel,
  .system-card,
  .history-card{

    padding:18px;
  }

  .digital-number{

    font-size:60px;

    letter-spacing:-3px;
  }

  .section-header{

    gap:12px;

    align-items:flex-start;
  }

  .download-btn{

    padding:
    9px 10px;

    font-size:10px;
  }

}

</style>

</head>

<body>

<!-- =====================================================
     HEADER
===================================================== -->

<header>

<div class="brand">

<div class="logo-box">
F
</div>

<div>

<div class="logo">
FuelGuard
</div>

<div class="subtitle">
ESP32 Vehicle Security System
</div>

</div>

</div>

<div class="live-status">

<div class="live-dot"></div>

LIVE SYSTEM

</div>

</header>

<!-- =====================================================
     MAIN
===================================================== -->

<div class="container">

<!-- PAGE TITLE -->

<div class="page-heading">

<h1>
Vehicle Security Dashboard
</h1>

<p>
Real-time fuel monitoring and vehicle security status
</p>

</div>

<!-- =====================================================
     STATUS GRID
===================================================== -->

<div class="grid">

<!-- FUEL -->

<div class="card status-card">

<div class="card-top">

<div class="title">
FUEL LEVEL
</div>

<div class="icon">
F
</div>

</div>

<div
class="value"
id="fuel">
--
</div>

<div
class="badge safe"
id="fuelBadge">

<span class="badge-dot"></span>

MONITORING

</div>

</div>

<!-- DISTANCE -->

<div class="card status-card">

<div class="card-top">

<div class="title">
DISTANCE
</div>

<div class="icon">
D
</div>

</div>

<div
class="value"
id="distance">
--
</div>

<div class="badge safe">

<span class="badge-dot"></span>

SENSOR ACTIVE

</div>

</div>

<!-- KEY -->

<div class="card status-card">

<div class="card-top">

<div class="title">
VEHICLE KEY
</div>

<div class="icon">
K
</div>

</div>

<div
class="value"
id="key">
--
</div>

<div
class="badge safe"
id="keyBadge">

<span class="badge-dot"></span>

SYSTEM READY

</div>

</div>

<!-- SECURITY -->

<div class="card status-card">

<div class="card-top">

<div class="title">
SECURITY
</div>

<div class="icon">
S
</div>

</div>

<div
class="value"
id="security">
--
</div>

<div
class="badge safe"
id="securityBadge">

<span class="badge-dot"></span>

SECURE

</div>

</div>

<!-- ALARM -->

<div class="card status-card">

<div class="card-top">

<div class="title">
ALARM
</div>

<div class="icon">
A
</div>

</div>

<div
class="value"
id="alarm">
--
</div>

<div
class="badge safe"
id="alarmBadge">

<span class="badge-dot"></span>

NORMAL

</div>

</div>

<!-- VIBRATION -->

<div class="card status-card">

<div class="card-top">

<div class="title">
VIBRATION
</div>

<div class="icon">
V
</div>

</div>

<div
class="value"
id="vibration">
--
</div>

<div
class="badge safe"
id="vibrationBadge">

<span class="badge-dot"></span>

MONITORING

</div>

</div>

<!-- REED -->

<div class="card status-card">

<div class="card-top">

<div class="title">
REED SWITCH
</div>

<div class="icon">
R
</div>

</div>

<div
class="value"
id="reed">
--
</div>

<div
class="badge safe"
id="reedBadge">

<span class="badge-dot"></span>

NORMAL

</div>

</div>

</div>

<!-- =====================================================
     DIGITAL FUEL METER
===================================================== -->

<div class="card fuel-panel">

<div class="section-header">

<h2>
Fuel Level
</h2>

<span>
Real-time reading
</span>

</div>

<div class="digital-meter">

<div
class="digital-number"
id="gaugeNumber">

--%

</div>

<div class="digital-label">

CURRENT FUEL LEVEL

</div>

</div>

<div class="fuel-progress-wrapper">

<div class="fuel-progress">

<div
class="fuel-progress-bar"
id="fuelProgress">

</div>

</div>

<div class="fuel-scale">

<span>
EMPTY
</span>

<span>
50%
</span>

<span>
FULL
</span>

</div>

</div>

</div>

<!-- =====================================================
     SYSTEM STATUS
===================================================== -->

<div class="card system-card">

<div class="section-header">

<h2>
System Status
</h2>

<span>
Live sensor data
</span>

</div>

<div class="status-list">

<div class="status-item">

<div class="status-item-label">
FUEL
</div>

<div
class="status-item-value"
id="fuel2">
--
</div>

</div>

<div class="status-item">

<div class="status-item-label">
KEY
</div>

<div
class="status-item-value"
id="key2">
--
</div>

</div>

<div class="status-item">

<div class="status-item-label">
SECURITY
</div>

<div
class="status-item-value"
id="security2">
--
</div>

</div>

<div class="status-item">

<div class="status-item-label">
ALARM
</div>

<div
class="status-item-value"
id="alarm2">
--
</div>

</div>

<div class="status-item">

<div class="status-item-label">
VIBRATION
</div>

<div
class="status-item-value"
id="vibration2">
--
</div>

</div>

<div class="status-item">

<div class="status-item-label">
REED
</div>

<div
class="status-item-value"
id="reed2">
--
</div>

</div>

</div>

</div>

<!-- =====================================================
     HISTORY
===================================================== -->

<div class="card history-card">

<div class="section-header">

<div>
<h2>
Recent History
</h2>

<span>
Latest sensor records
</span>
</div>

<button
  class="download-btn"
  id="downloadHistoryBtn"
  onclick="downloadHistory()">
  DOWNLOAD HISTORY
</button>

</div>

<div class="table-wrapper">

<table>

<thead>

<tr>

<th>
Time
</th>

<th>
Fuel
</th>

<th>
Key
</th>

<th>
Security
</th>

</tr>

</thead>

<tbody id="records">

</tbody>

</table>

</div>

</div>

<div class="footer">

FuelGuard • ESP32 Vehicle Security System

</div>

</div>

<!-- =====================================================
     JAVASCRIPT
===================================================== -->

<script>

/* =====================================================
   REFRESH LIVE DATA
===================================================== */

function refreshData(){

fetch('/api/status')

.then(
response =>
response.json()
)

.then(data => {

/* -----------------------------------------------------
   FUEL
----------------------------------------------------- */

let fuel =
Math.max(
0,
Math.min(
100,
Number(data.fuel)
)
);

let fuelText =
fuel.toFixed(1)
+
'%';

document
.getElementById('fuel')
.innerText =
fuelText;

document
.getElementById('fuel2')
.innerText =
fuelText;

document
.getElementById('gaugeNumber')
.innerText =
fuelText;

/* FUEL PROGRESS */

document
.getElementById('fuelProgress')
.style.width =
fuel + '%';

/* -----------------------------------------------------
   DISTANCE
----------------------------------------------------- */

document
.getElementById('distance')
.innerText =

Number(data.distance)
.toFixed(1)
+
' cm';

/* -----------------------------------------------------
   KEY
----------------------------------------------------- */

let keyStatus =
data.key
?
'ON'
:
'OFF';

document
.getElementById('key')
.innerText =
keyStatus;

document
.getElementById('key2')
.innerText =
keyStatus;

/* -----------------------------------------------------
   SECURITY
----------------------------------------------------- */

let securityStatus =
data.theft
?
'ALERT'
:
'SAFE';

document
.getElementById('security')
.innerText =
securityStatus;

document
.getElementById('security2')
.innerText =
securityStatus;

/* SECURITY BADGE */

let securityBadge =
document
.getElementById(
'securityBadge'
);

if(data.theft){

  securityBadge
  .className =
  'badge alert';

  securityBadge.innerHTML =
  '<span class="badge-dot"></span> ALERT';

}
else{

  securityBadge
  .className =
  'badge safe';

  securityBadge.innerHTML =
  '<span class="badge-dot"></span> SECURE';

}

/* -----------------------------------------------------
   ALARM
----------------------------------------------------- */

let alarmStatus =
data.alarm
?
'ON'
:
'OFF';

document
.getElementById('alarm')
.innerText =
alarmStatus;

document
.getElementById('alarm2')
.innerText =
alarmStatus;

let alarmBadge =
document
.getElementById(
'alarmBadge'
);

if(data.alarm){

  alarmBadge
  .className =
  'badge alert';

  alarmBadge.innerHTML =
  '<span class="badge-dot"></span> ACTIVE';

}
else{

  alarmBadge
  .className =
  'badge safe';

  alarmBadge.innerHTML =
  '<span class="badge-dot"></span> NORMAL';

}

/* -----------------------------------------------------
   VIBRATION
----------------------------------------------------- */

let vibrationStatus =
data.vibration
?
'DETECTED'
:
'NORMAL';

document
.getElementById('vibration')
.innerText =
vibrationStatus;

document
.getElementById('vibration2')
.innerText =
vibrationStatus;

let vibrationBadge =
document
.getElementById(
'vibrationBadge'
);

if(data.vibration){

  vibrationBadge
  .className =
  'badge alert';

  vibrationBadge.innerHTML =
  '<span class="badge-dot"></span> DETECTED';

}
else{

  vibrationBadge
  .className =
  'badge safe';

  vibrationBadge.innerHTML =
  '<span class="badge-dot"></span> MONITORING';

}

/* -----------------------------------------------------
   REED
----------------------------------------------------- */

let reedStatus =
data.reed
?
'ACTIVATED'
:
'NORMAL';

document
.getElementById('reed')
.innerText =
reedStatus;

document
.getElementById('reed2')
.innerText =
reedStatus;

let reedBadge =
document
.getElementById(
'reedBadge'
);

if(data.reed){

  reedBadge
  .className =
  'badge alert';

  reedBadge.innerHTML =
  '<span class="badge-dot"></span> ACTIVATED';

}
else{

  reedBadge
  .className =
  'badge safe';

  reedBadge.innerHTML =
  '<span class="badge-dot"></span> NORMAL';

}

/* -----------------------------------------------------
   KEY BADGE
----------------------------------------------------- */

let keyBadge =
document
.getElementById(
'keyBadge'
);

if(data.key){

  keyBadge
  .className =
  'badge safe';

  keyBadge.innerHTML =
  '<span class="badge-dot"></span> KEY ON';

}
else{

  keyBadge
  .className =
  'badge safe';

  keyBadge.innerHTML =
  '<span class="badge-dot"></span> KEY OFF';

}

/* -----------------------------------------------------
   FUEL BADGE
----------------------------------------------------- */

let fuelBadge =
document
.getElementById(
'fuelBadge'
);

if(fuel <= 20){

  fuelBadge
  .className =
  'badge alert';

  fuelBadge.innerHTML =
  '<span class="badge-dot"></span> LOW FUEL';

}
else{

  fuelBadge
  .className =
  'badge safe';

  fuelBadge.innerHTML =
  '<span class="badge-dot"></span> MONITORING';

}

})

.catch(
error =>
console.log(error)
);

}

/* =====================================================
   LOAD HISTORY
===================================================== */

function formatHistoryTime(value){

  /*
     The ESP32 history stores millis().

     Convert milliseconds into a clean
     HH:MM:SS elapsed-time display.
  */

  let ms = Number(value);

  if(!Number.isFinite(ms)){
    return "--:--:--";
  }

  /*
     Backward compatibility:
     If the backend sends seconds instead of
     milliseconds, convert it to milliseconds.
  */

  if(ms < 100000){
    ms = ms * 1000;
  }

  let totalSeconds =
    Math.floor(ms / 1000);

  let hours =
    Math.floor(totalSeconds / 3600);

  let minutes =
    Math.floor(
      (totalSeconds % 3600) / 60
    );

  let seconds =
    totalSeconds % 60;

  return (
    String(hours).padStart(2,'0')
    + ':' +
    String(minutes).padStart(2,'0')
    + ':' +
    String(seconds).padStart(2,'0')
  );
}

let historyCache = [];

async function loadHistory(){

  try{

    const response =
      await fetch(
        '/api/history?ts=' +
        Date.now(),
        {
          cache:'no-store'
        }
      );

    if(!response.ok){
      throw new Error(
        'History HTTP error: ' +
        response.status
      );
    }

    const data =
      await response.json();

    if(!Array.isArray(data)){
      throw new Error(
        'History API did not return an array'
      );
    }

    /*
       Keep a local copy for the download button.
    */

    historyCache =
      data.map(row => ({
        time:
          row.time,
        fuel:
          Number(row.fuel) || 0,
        litres:
          Number(row.litres) || 0,
        key:
          Boolean(row.key),
        theft:
          Boolean(row.theft)
      }));

    /*
       Show newest records first.
       This does not modify the ESP32 history array.
    */

    const displayData =
      [...historyCache].reverse();

    let html = '';

    if(displayData.length === 0){

      html =
        '<tr>' +
        '<td colspan="4" ' +
        'style="text-align:center;' +
        'color:#94a3b8;' +
        'padding:25px;">' +
        'No history records available' +
        '</td>' +
        '</tr>';

    }
    else{

      displayData.forEach(row => {

        const securityClass =
          row.theft
          ? 'alert'
          : 'safe';

        const securityText =
          row.theft
          ? 'ALERT'
          : 'SAFE';

        html +=

          '<tr>' +

          '<td>' +
          formatHistoryTime(row.time) +
          '</td>' +

          '<td>' +
          row.fuel.toFixed(1) +
          '%' +
          '</td>' +

          '<td>' +
          (row.key ? 'ON' : 'OFF') +
          '</td>' +

          '<td>' +

          '<span class="badge ' +
          securityClass +
          '">' +

          '<span class="badge-dot"></span>' +

          securityText +

          '</span>' +

          '</td>' +

          '</tr>';

      });

    }

    const records =
      document.getElementById(
        'records'
      );

    if(records){
      records.innerHTML = html;
    }

  }
  catch(error){

    console.error(
      'History loading failed:',
      error
    );

    const records =
      document.getElementById(
        'records'
      );

    if(records){

      records.innerHTML =
        '<tr>' +
        '<td colspan="4" ' +
        'style="text-align:center;' +
        'color:#dc2626;' +
        'padding:25px;">' +
        'Unable to load history' +
        '</td>' +
        '</tr>';

    }

  }

}

/* =====================================================
   DOWNLOAD HISTORY
===================================================== */

function csvEscape(value){

  const str =
    String(value ?? '');

  if(
    str.includes(',') ||
    str.includes('"') ||
    str.includes('\n')
  ){

    return '"' +
      str.replace(
        /"/g,
        '""'
      ) +
      '"';

  }

  return str;
}

function downloadHistory(){

  if(
    !Array.isArray(historyCache) ||
    historyCache.length === 0
  ){

    alert(
      'No history records are available to download.'
    );

    return;
  }

  /*
     Newest record first in the downloaded file.
  */

  const rows =
    [...historyCache].reverse();

  let csv =
    'FuelGuard History Report\r\n';

  csv +=
    'Time,Fuel Level (%),Fuel (Litres),Key,Security\r\n';

  rows.forEach(row => {

    csv +=

      csvEscape(
        formatHistoryTime(row.time)
      ) +

      ',' +

      csvEscape(
        row.fuel.toFixed(1)
      ) +

      ',' +

      csvEscape(
        row.litres.toFixed(1)
      ) +

      ',' +

      csvEscape(
        row.key ? 'ON' : 'OFF'
      ) +

      ',' +

      csvEscape(
        row.theft ? 'ALERT' : 'SAFE'
      ) +

      '\r\n';

  });

  /*
     Add UTF-8 BOM so Excel opens the
     CSV cleanly on Windows.
  */

  const blob =
    new Blob(
      ['\uFEFF' + csv],
      {
        type:
        'text/csv;charset=utf-8;'
      }
    );

  const url =
    URL.createObjectURL(blob);

  const link =
    document.createElement('a');

  const now =
    new Date();

  const date =
    now.getFullYear() +
    '-' +
    String(
      now.getMonth() + 1
    ).padStart(2,'0') +
    '-' +
    String(
      now.getDate()
    ).padStart(2,'0');

  link.href = url;

  link.download =
    'FuelGuard_History_' +
    date +
    '.csv';

  document.body.appendChild(link);

  link.click();

  document.body.removeChild(link);

  URL.revokeObjectURL(url);

}

/* =====================================================
   INITIAL LOAD
===================================================== */

refreshData();

loadHistory();

/* =====================================================
   AUTO REFRESH
===================================================== */

setInterval(
  refreshData,
  1000
);

setInterval(
  loadHistory,
  5000
);

/* =====================================================
   INITIAL LOAD
===================================================== */

refreshData();

loadHistory();

/* =====================================================
   AUTO REFRESH
===================================================== */

setInterval(
refreshData,
1000
);

setInterval(
loadHistory,
5000
);

</script>

<div class="footer"><strong>KALAM ELECTRONICS</strong><br>Developed &amp; Integrated by Kalam Electronics | 28.09.2026</div>
</body>

</html>

)rawliteral";

// WEB STATUS

void handleStatus() {

  String json =
    "{";

  json +=
    "\"fuel\":";

  json +=
    String(
      fuelPercent,
      1
    );

  json +=
    ",\"litres\":";

  json +=
    String(
      fuelLitres,
      1
    );

  json +=
    ",\"distance\":";

  json +=
    String(
      currentDistance,
      1
    );

  json +=
    ",\"key\":";

  json +=
    keyOn ?
    "true" :
    "false";

  json +=
    ",\"theft\":";

  json +=
    theftDetected ?
    "true" :
    "false";

  json +=
    ",\"alarm\":";

  json +=
    (
      theftDetected &&
      !alarmMuted
    ) ?
    "true" :
    "false";

  json +=
    ",\"vibration\":";

  json +=
    digitalRead(
      VIBRATION_PIN
    ) ?
    "true" :
    "false";

  json +=
    ",\"reed\":";

  json +=
    (
      digitalRead(
        REED_PIN
      ) == LOW
    ) ?
    "true" :
    "false";

  json +=
    "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// WEB HISTORY

void handleHistory() {

  /*
     IMPORTANT:
     Send the timestamp as a numeric millisecond value.

     The old version converted it to:
        "123s"

     That made the browser treat time as text and made
     history display/download handling unreliable.

     Now the browser receives:
        123456

     and converts it into HH:MM:SS.
  */

  String json = "[";

  for(
    int i = 0;
    i < historyCount;
    i++
  ){

    if(i > 0){
      json += ",";
    }

    json += "{";

    json +=
      "\"time\":";

    json +=
      String(
        historyData[i].time
      );

    json +=
      ",\"fuel\":";

    json +=
      String(
        historyData[i].fuel,
        1
      );

    json +=
      ",\"litres\":";

    json +=
      String(
        historyData[i].litres,
        1
      );

    json +=
      ",\"key\":";

    json +=
      historyData[i].key
      ?
      "true"
      :
      "false";

    json +=
      ",\"theft\":";

    json +=
      historyData[i].theft
      ?
      "true"
      :
      "false";

    json +=
      "}";

  }

  json += "]";

  /*
     Prevent the browser from using an old cached
     history response.
  */

  server.sendHeader(
    "Cache-Control",
    "no-store, no-cache, must-revalidate"
  );

  server.sendHeader(
    "Pragma",
    "no-cache"
  );

  server.send(
    200,
    "application/json",
    json
  );

}

// MAIN PAGE

void handleMainPage() {

  server.send_P(
    200,
    "text/html",
    MAIN_PAGE
  );
}

// 404

void handleNotFound() {

  server.send(
    404,
    "text/plain",
    "Not Found"
  );
}

void setupWiFi() {

  WiFi.mode(
    WIFI_AP_STA
  );

  // Access Point

  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );

  Serial.println();

  Serial.print(
    "AP IP: "
  );

  Serial.println(
    WiFi.softAPIP()
  );

  // Router WiFi

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.print(
    "Connecting WiFi"
  );

  unsigned long start =
    millis();

  while (
    WiFi.status() !=
    WL_CONNECTED
    &&
    millis() - start <
    10000
  ) {

    delay(250);

    Serial.print(
      "."
    );
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    Serial.print(
      "WiFi IP: "
    );

    Serial.println(
      WiFi.localIP()
    );

  } else {

    Serial.println(
      "Router WiFi not connected"
    );
  }
}

void setup() {

  Serial.begin(
    115200
  );

  // PIN MODES

  pinMode(
    TRIG_PIN,
    OUTPUT
  );

  pinMode(
    ECHO_PIN,
    INPUT
  );

  pinMode(
    VIBRATION_PIN,
    INPUT
  );

  pinMode(
    REED_PIN,
    INPUT_PULLUP
  );

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

  pinMode(
    RED_LED_PIN,
    OUTPUT
  );

  pinMode(
    GREEN_LED_PIN,
    OUTPUT
  );

  pinMode(
    KEY_BUTTON_PIN,
    INPUT_PULLUP
  );

  pinMode(
    ALARM_RESET_BUTTON_PIN,
    INPUT_PULLUP
  );

  // INITIAL OUTPUT

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  digitalWrite(
    RED_LED_PIN,
    LOW
  );

  digitalWrite(
    GREEN_LED_PIN,
    HIGH
  );

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  if (
    !display.begin(
      SSD1306_SWITCHCAPVCC,
      0x3C
    )
  ) {

    Serial.println(
      "OLED not found"
    );

  } else {

    display.clearDisplay();

    display.setTextColor(
      SSD1306_WHITE
    );

    display.setTextSize(
      1
    );

    display.setCursor(
      0,
      0
    );

    display.println(
      "FUELGUARD"
    );

    display.println();

    display.println(
      "System Starting..."
    );

    display.display();
  }

  setupWiFi();

  secured_client.setInsecure();

  server.on(
    "/",
    handleMainPage
  );

  server.on(
    "/api/status",
    handleStatus
  );

  server.on(
    "/api/history",
    handleHistory
  );

  server.onNotFound(
    handleNotFound
  );

  server.begin();

  Serial.println();
  Serial.println(
    "================================"
  );

  Serial.println(
    "FUELGUARD READY"
  );

  Serial.println(
    "================================"
  );

  Serial.println(
    "KEY = OFF -> SECURITY ARMED"
  );

  Serial.println(
    "KEY = ON  -> SECURITY DISARMED"
  );

  Serial.println(
    "Tank Height = 13 cm"
  );

  Serial.println(
    "================================"
  );

  // FIRST FUEL READING

  delay(100);

  readFuelSensor();

  previousFuelPercent =
    fuelPercent;

  updateOLED();
}

void loop() {

  server.handleClient();

  // BUTTONS

  handleButtons();

  // VIBRATION
  // FOR FAST RESPONSE

  checkVibration();

  // REED SWITCH

  checkReedSwitch();

  // FUEL SENSOR

  if (
    millis() -
    lastFuelSensorRead
    >=
    FUEL_SENSOR_INTERVAL
  ) {

    lastFuelSensorRead =
      millis();

    readFuelSensor();
  }

  updateOutputs();

  if (
    millis() -
    lastOLEDUpdate
    >=
    OLED_INTERVAL
  ) {

    lastOLEDUpdate =
      millis();

    updateOLED();
  }

  if (
    millis() -
    lastHistoryRecord
    >=
    HISTORY_INTERVAL
  ) {

    lastHistoryRecord =
      millis();

    addHistoryRecord();
  }

  // Therefore Telegram/network delay
  // does NOT occur before buzzer activation.

  handleTelegramMessages();
}