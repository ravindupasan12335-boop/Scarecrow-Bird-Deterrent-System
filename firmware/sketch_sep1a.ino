/*
  ============================================================
  SCARECROW V5
  ESP32 + INMP441 + Edge Impulse + Radar + PIR + DFPlayer
  ============================================================

  MICROPHONE:
    WS  = GPIO 25
    SCK = GPIO 33
    SD  = GPIO 32

  SENSORS:
    Radar = GPIO 4
    PIR   = GPIO 26

  DFPLAYER:
    ESP32 RX = GPIO 16  -> DFPlayer TX
    ESP32 TX = GPIO 17  -> DFPlayer RX

  CONTROLS:
    Button = GPIO 27 -> GND
    LED    = GPIO 21

  AI:
    Edge Impulse local inference
    AI crow trigger confidence = 95%

  WIFI:
    AP:
      SSID     = Scarecrow
      Password = scarecrow123

  TIME:
    NTP time from local Wi-Fi/router
    Sri Lanka timezone = UTC+5:30

  SCHEDULE:
    Optional
    Start time
    End time
    Saved using Preferences

  ============================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DFRobotDFPlayerMini.h>
#include <time.h>
#include <math.h>
#include <driver/i2s.h>

#include <ravindupasan12335-boop-project-1_inferencing.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


// ============================================================
// PINS
// ============================================================

#define RADAR_PIN       4
#define PIR_PIN         26

#define DF_RX_PIN       16
#define DF_TX_PIN       17

#define BUTTON_PIN      27
#define LED_PIN         21

#define I2S_WS          25
#define I2S_SCK         33
#define I2S_SD          32

#define MIC_I2S_PORT    I2S_NUM_0


// ============================================================
// WIFI ACCESS POINT
// ============================================================

const char* AP_SSID     = "Scarecrow";
const char* AP_PASSWORD = "scarecrow123";


// ============================================================
// AI SETTINGS
// ============================================================

// IMPORTANT:
// AI must reach 95% before it is considered a crow/raven.
#define CROW_CONFIDENCE_MIN       95

#define AI_PREDICTION_INTERVAL_MS 250

#define MIC_READ_SAMPLES           256


// ============================================================
// OBJECTS
// ============================================================

WebServer server(80);

Preferences prefs;

HardwareSerial dfSerial(2);

DFRobotDFPlayerMini player;

TaskHandle_t CaptureTaskHandle = nullptr;


// ============================================================
// SYSTEM SETTINGS
// ============================================================

bool systemEnabled = true;

bool aiMicEnabled = true;

bool dfReady = false;

bool ntpConfigured = false;

volatile bool playing = false;

volatile bool crowDetected = false;


// ============================================================
// AUDIO SETTINGS
// ============================================================

int soundChoice = 0;

int currentPlayingTrack = 1;

int volumeLevel = 25;

int playDuration = 10;

int repeatCount = 1;

int cooldownSeconds = 5;

int currentPlayCount = 0;


// ============================================================
// WIFI SETTINGS
// ============================================================

String routerSSID = "";

String routerPassword = "";


// ============================================================
// SCHEDULE SETTINGS
// ============================================================

bool scheduleEnabled = false;

// Default schedule:
// 06:00 -> 18:00

int scheduleStartHour = 6;

int scheduleStartMinute = 0;

int scheduleEndHour = 18;

int scheduleEndMinute = 0;


// ============================================================
// SYSTEM TIMERS
// ============================================================

unsigned long detectionCount = 0;

unsigned long lastTriggerEndMs = 0;

unsigned long playStartedMs = 0;

unsigned long lastButtonMs = 0;

unsigned long lastSerialVolMs = 0;

unsigned long lastPredictionMs = 0;

unsigned long lastNtpAttemptMs = 0;

const unsigned long OVERLAP_WINDOW = 3000UL;


// ============================================================
// SENSOR STATE
// ============================================================

bool radarSeen = false;

bool pirSeen = false;

unsigned long radarSeenMs = 0;

unsigned long pirSeenMs = 0;


// ============================================================
// BUTTON
// ============================================================

bool previousButtonState = HIGH;


// ============================================================
// AI STATE
// ============================================================

volatile int currentMicVolume = 0;

portMUX_TYPE aiMux = portMUX_INITIALIZER_UNLOCKED;

char aiStatus[32] = "INITIALIZING";

char aiLabel[24] = "NONE";

char aiLastTime[12] = "--:--:--";

int aiConfidence = -1;

int aiConfHistory[15] = {0};


// ============================================================
// EDGE IMPULSE AUDIO BUFFER
// ============================================================

typedef struct {

  int16_t* buffers[2];

  volatile uint8_t buf_select;

  volatile uint8_t buf_ready;

  volatile uint32_t buf_count;

  uint32_t n_samples;

} inference_t;


static inference_t inference = {};

static bool record_status = false;

static bool microphoneReady = false;

static int16_t sampleBuffer[MIC_READ_SAMPLES];

static int32_t rawMicBuffer[MIC_READ_SAMPLES];

static bool debug_nn = false;

static int print_results = 0;


// ============================================================
// TIME HELPERS
// ============================================================

bool clockIsValid() {

  return time(nullptr) > 1700000000;
}


void makeTimeStr(char* buf, size_t len) {

  struct tm t;

  if (clockIsValid() && getLocalTime(&t, 50)) {

    strftime(buf, len, "%H:%M:%S", &t);

  } else {

    unsigned long s = millis() / 1000UL;

    snprintf(
      buf,
      len,
      "%02lu:%02lu:%02lu",
      (s / 3600UL) % 100UL,
      (s / 60UL) % 60UL,
      s % 60UL
    );
  }
}


String currentTimeString() {

  char buf[16];

  makeTimeStr(buf, sizeof(buf));

  return String(buf);
}


// ============================================================
// SCHEDULE TIME HELPERS
// ============================================================

int currentMinutesOfDay() {

  if (!clockIsValid()) {
    return -1;
  }

  struct tm t;

  if (!getLocalTime(&t, 50)) {
    return -1;
  }

  return t.tm_hour * 60 + t.tm_min;
}


// ============================================================
// CHECK WHETHER SCHEDULE IS ACTIVE
// ============================================================

bool isScheduleActive() {

  // Schedule disabled = always allowed
  if (!scheduleEnabled) {
    return true;
  }

  int current = currentMinutesOfDay();

  // No valid NTP time yet.
  // Fail-safe: do not allow automatic trigger.
  if (current < 0) {
    return false;
  }

  int start =
    scheduleStartHour * 60 +
    scheduleStartMinute;

  int end =
    scheduleEndHour * 60 +
    scheduleEndMinute;


  // Same start/end = 24 hours
  if (start == end) {
    return true;
  }


  // Normal schedule
  // Example: 06:00 -> 18:00
  if (start < end) {

    return current >= start &&
           current < end;
  }


  // Overnight schedule
  // Example: 18:00 -> 06:00
  return current >= start ||
         current < end;
}


// ============================================================
// SCHEDULE STATUS TEXT
// ============================================================

String scheduleStatusText() {

  if (!scheduleEnabled) {
    return "DISABLED";
  }

  if (!clockIsValid()) {
    return "WAITING FOR TIME";
  }

  return isScheduleActive()
         ? "ACTIVE"
         : "OUTSIDE SCHEDULE";
}


// ============================================================
// FORMAT TIME HH:MM
// ============================================================

String formatScheduleTime(int hour, int minute) {

  char buf[8];

  snprintf(
    buf,
    sizeof(buf),
    "%02d:%02d",
    hour,
    minute
  );

  return String(buf);
}


// ============================================================
// AI STATUS HELPERS
// ============================================================

void setAiStatus(const char* status) {

  portENTER_CRITICAL(&aiMux);

  strlcpy(
    aiStatus,
    status,
    sizeof(aiStatus)
  );

  portEXIT_CRITICAL(&aiMux);
}


void setAiResult(
  const char* label,
  int confidence
) {

  char ts[12];

  makeTimeStr(
    ts,
    sizeof(ts)
  );

  portENTER_CRITICAL(&aiMux);

  strlcpy(
    aiLabel,
    label,
    sizeof(aiLabel)
  );

  strlcpy(
    aiLastTime,
    ts,
    sizeof(aiLastTime)
  );

  aiConfidence = confidence;


  for (int i = 0; i < 14; i++) {

    aiConfHistory[i] =
      aiConfHistory[i + 1];
  }


  aiConfHistory[14] =
    confidence;

  portEXIT_CRITICAL(&aiMux);
}


// ============================================================
// EDGE IMPULSE LABEL CHECK
// ============================================================

bool isCrowClass(const char* label) {

  if (!label) {
    return false;
  }

  String s(label);

  s.toLowerCase();

  return
    s.indexOf("crow") >= 0 ||
    s.indexOf("raven") >= 0 ||
    s.indexOf("rook") >= 0;
}


// ============================================================
// SETTINGS STORAGE
// ============================================================

void loadSettings() {

  systemEnabled =
    prefs.getBool(
      "enabled",
      true
    );


  aiMicEnabled =
    prefs.getBool(
      "aiEnabled",
      true
    );


  soundChoice =
    prefs.getInt(
      "soundChoice",
      0
    );


  volumeLevel =
    prefs.getInt(
      "volume",
      25
    );


  playDuration =
    prefs.getInt(
      "duration",
      10
    );


  repeatCount =
    prefs.getInt(
      "repeatCount",
      1
    );


  routerSSID =
    prefs.getString(
      "routerSSID",
      ""
    );


  routerPassword =
    prefs.getString(
      "routerPass",
      ""
    );


  // -------------------------
  // Schedule
  // -------------------------

  scheduleEnabled =
    prefs.getBool(
      "schedEnabled",
      false
    );


  scheduleStartHour =
    prefs.getInt(
      "schedStartH",
      6
    );


  scheduleStartMinute =
    prefs.getInt(
      "schedStartM",
      0
    );


  scheduleEndHour =
    prefs.getInt(
      "schedEndH",
      18
    );


  scheduleEndMinute =
    prefs.getInt(
      "schedEndM",
      0
    );


  // Safety limits

  soundChoice =
    constrain(
      soundChoice,
      0,
      3
    );


  volumeLevel =
    constrain(
      volumeLevel,
      0,
      30
    );


  playDuration =
    constrain(
      playDuration,
      5,
      60
    );


  repeatCount =
    constrain(
      repeatCount,
      1,
      10
    );


  scheduleStartHour =
    constrain(
      scheduleStartHour,
      0,
      23
    );


  scheduleStartMinute =
    constrain(
      scheduleStartMinute,
      0,
      59
    );


  scheduleEndHour =
    constrain(
      scheduleEndHour,
      0,
      23
    );


  scheduleEndMinute =
    constrain(
      scheduleEndMinute,
      0,
      59
    );
}


// ============================================================
// SAVE SETTINGS
// ============================================================

void saveSettings() {

  prefs.putBool(
    "enabled",
    systemEnabled
  );


  prefs.putBool(
    "aiEnabled",
    aiMicEnabled
  );


  prefs.putInt(
    "soundChoice",
    soundChoice
  );


  prefs.putInt(
    "volume",
    volumeLevel
  );


  prefs.putInt(
    "duration",
    playDuration
  );


  prefs.putInt(
    "repeatCount",
    repeatCount
  );


  // -------------------------
  // Schedule
  // -------------------------

  prefs.putBool(
    "schedEnabled",
    scheduleEnabled
  );


  prefs.putInt(
    "schedStartH",
    scheduleStartHour
  );


  prefs.putInt(
    "schedStartM",
    scheduleStartMinute
  );


  prefs.putInt(
    "schedEndH",
    scheduleEndHour
  );


  prefs.putInt(
    "schedEndM",
    scheduleEndMinute
  );
}


// ============================================================
// DFPLAYER
// ============================================================

void playSelectedTrack() {

  if (playing) {
    return;
  }


  if (!dfReady) {

    if (
      player.begin(
        dfSerial,
        false,
        false
      )
    ) {

      dfReady = true;

      player.volume(
        volumeLevel
      );

    } else {

      Serial.println(
        "DFPlayer ERROR"
      );

      return;
    }
  }


  if (soundChoice == 0) {

    currentPlayingTrack =
      random(1, 4);

  } else {

    currentPlayingTrack =
      constrain(
        soundChoice,
        1,
        3
      );
  }


  currentPlayCount = 1;


  player.volume(
    volumeLevel
  );


  delay(20);


  player.play(
    currentPlayingTrack
  );


  playing = true;

  playStartedMs =
    millis();


  radarSeen = false;

  pirSeen = false;

  crowDetected = false;


  Serial.printf(
    "PLAYING TRACK %d/%d\n",
    currentPlayCount,
    repeatCount
  );
}


// ============================================================
// STOP SOUND
// ============================================================

void stopSound() {

  if (dfReady) {

    player.stop();
  }


  playing = false;

  currentPlayCount = 0;

  lastTriggerEndMs =
    millis();


  radarSeen = false;

  pirSeen = false;

  crowDetected = false;


  Serial.println(
    "PLAYBACK STOPPED"
  );
}


// ============================================================
// I2S INITIALIZATION
// ============================================================

bool setupI2S() {

  i2s_config_t config = {

    .mode =
      (i2s_mode_t)(
        I2S_MODE_MASTER |
        I2S_MODE_RX
      ),

    .sample_rate =
      EI_CLASSIFIER_FREQUENCY,

    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_32BIT,

    .channel_format =
      I2S_CHANNEL_FMT_ONLY_LEFT,

    .communication_format =
      I2S_COMM_FORMAT_STAND_I2S,

    .intr_alloc_flags =
      ESP_INTR_FLAG_LEVEL1,

    .dma_buf_count = 8,

    .dma_buf_len = 256,

    .use_apll = false,

    .tx_desc_auto_clear = false,

    .fixed_mclk = 0
  };


  i2s_pin_config_t pins = {

    .mck_io_num =
      I2S_PIN_NO_CHANGE,

    .bck_io_num =
      I2S_SCK,

    .ws_io_num =
      I2S_WS,

    .data_out_num =
      I2S_PIN_NO_CHANGE,

    .data_in_num =
      I2S_SD
  };


  esp_err_t result =
    i2s_driver_install(
      MIC_I2S_PORT,
      &config,
      0,
      nullptr
    );


  if (result != ESP_OK) {

    Serial.printf(
      "I2S driver install failed: %d\n",
      result
    );

    return false;
  }


  result =
    i2s_set_pin(
      MIC_I2S_PORT,
      &pins
    );


  if (result != ESP_OK) {

    Serial.printf(
      "I2S pin setup failed: %d\n",
      result
    );

    i2s_driver_uninstall(
      MIC_I2S_PORT
    );

    return false;
  }


  i2s_zero_dma_buffer(
    MIC_I2S_PORT
  );


  Serial.println(
    "INMP441 initialized."
  );


  Serial.printf(
    "I2S WS=%d SCK=%d SD=%d RATE=%d\n",
    I2S_WS,
    I2S_SCK,
    I2S_SD,
    EI_CLASSIFIER_FREQUENCY
  );


  return true;
}


// ============================================================
// EDGE IMPULSE AUDIO CALLBACK
// ============================================================

static void audioInferenceCallback(
  size_t sampleCount
) {

  for (
    size_t i = 0;
    i < sampleCount;
    i++
  ) {

    inference
      .buffers[
        inference.buf_select
      ]
      [
        inference.buf_count++
      ] =
      sampleBuffer[i];


    if (
      inference.buf_count >=
      inference.n_samples
    ) {

      inference.buf_select ^= 1;

      inference.buf_count = 0;

      inference.buf_ready = 1;
    }
  }
}


// ============================================================
// MICROPHONE CAPTURE TASK
// ============================================================

void captureSamplesTask(
  void* parameter
) {

  while (record_status) {

    size_t bytesRead = 0;


    esp_err_t result =
      i2s_read(
        MIC_I2S_PORT,
        rawMicBuffer,
        sizeof(rawMicBuffer),
        &bytesRead,
        portMAX_DELAY
      );


    if (
      result != ESP_OK ||
      bytesRead == 0
    ) {

      continue;
    }


    size_t count =
      bytesRead /
      sizeof(int32_t);


    if (
      count >
      MIC_READ_SAMPLES
    ) {

      count =
        MIC_READ_SAMPLES;
    }


    double sumSq = 0;


    for (
      size_t i = 0;
      i < count;
      i++
    ) {

      // INMP441 32-bit -> 16-bit

      int32_t sample =
        rawMicBuffer[i] >> 14;


      if (sample > 32767) {
        sample = 32767;
      }


      if (sample < -32768) {
        sample = -32768;
      }


      sampleBuffer[i] =
        (int16_t)sample;


      sumSq +=
        (double)sample *
        (double)sample;
    }


    if (count > 0) {

      currentMicVolume =
        (int)sqrt(
          sumSq / count
        );
    }


    if (
      record_status &&
      count > 0
    ) {

      audioInferenceCallback(
        count
      );
    }
  }


  vTaskDelete(nullptr);
}


// ============================================================
// START MICROPHONE
// ============================================================

bool microphoneInferenceStart(
  uint32_t samples
) {

  inference.buffers[0] =
    (int16_t*)malloc(
      samples *
      sizeof(int16_t)
    );


  inference.buffers[1] =
    (int16_t*)malloc(
      samples *
      sizeof(int16_t)
    );


  if (
    !inference.buffers[0] ||
    !inference.buffers[1]
  ) {

    if (inference.buffers[0]) {
      free(inference.buffers[0]);
    }


    if (inference.buffers[1]) {
      free(inference.buffers[1]);
    }


    inference.buffers[0] = nullptr;

    inference.buffers[1] = nullptr;


    Serial.println(
      "Unable to allocate Edge Impulse buffers."
    );


    return false;
  }


  inference.buf_select = 0;

  inference.buf_count = 0;

  inference.buf_ready = 0;

  inference.n_samples = samples;


  if (!setupI2S()) {

    free(inference.buffers[0]);

    free(inference.buffers[1]);


    inference.buffers[0] = nullptr;

    inference.buffers[1] = nullptr;


    return false;
  }


  record_status = true;


  BaseType_t created =
    xTaskCreatePinnedToCore(
      captureSamplesTask,
      "EICapture",
      8192,
      nullptr,
      2,
      &CaptureTaskHandle,
      0
    );


  if (created != pdPASS) {

    record_status = false;


    i2s_driver_uninstall(
      MIC_I2S_PORT
    );


    free(inference.buffers[0]);

    free(inference.buffers[1]);


    inference.buffers[0] = nullptr;

    inference.buffers[1] = nullptr;


    Serial.println(
      "Failed to create microphone task."
    );


    return false;
  }


  return true;
}


// ============================================================
// WAIT FOR AUDIO SLICE
// ============================================================

bool microphoneInferenceRecord() {

  while (
    record_status &&
    inference.buf_ready == 0
  ) {

    vTaskDelay(
      1 /
      portTICK_PERIOD_MS
    );
  }


  if (!record_status) {

    return false;
  }


  inference.buf_ready = 0;


  return true;
}


// ============================================================
// EDGE IMPULSE SIGNAL DATA
// ============================================================

static int microphoneAudioSignalGetData(
  size_t offset,
  size_t length,
  float* outPtr
) {

  numpy::int16_to_float(

    &inference
      .buffers[
        inference.buf_select ^ 1
      ][offset],

    outPtr,

    length
  );


  return 0;
}


// ============================================================
// RUN EDGE IMPULSE
// ============================================================

void runEdgeImpulseInference() {

  if (!microphoneReady) {
    return;
  }


  if (!microphoneInferenceRecord()) {
    return;
  }


  signal_t signal;


  signal.total_length =
    EI_CLASSIFIER_SLICE_SIZE;


  signal.get_data =
    &microphoneAudioSignalGetData;


  ei_impulse_result_t result = {0};


  EI_IMPULSE_ERROR err =
    run_classifier_continuous(
      &signal,
      &result,
      debug_nn
    );


  if (
    err != EI_IMPULSE_OK
  ) {

    Serial.printf(
      "Classifier error: %d\n",
      err
    );

    setAiStatus(
      "CLASSIFIER ERROR"
    );

    return;
  }


  if (
    millis() -
    lastPredictionMs <
    AI_PREDICTION_INTERVAL_MS
  ) {

    return;
  }


  lastPredictionMs =
    millis();


  if (!aiMicEnabled) {

    setAiStatus(
      "AI DISABLED"
    );

    crowDetected = false;

    return;
  }


  float bestScore = 0.0f;

  const char* bestLabel =
    "UNKNOWN";

  bool bestIsCrow = false;


  for (
    size_t i = 0;
    i < EI_CLASSIFIER_LABEL_COUNT;
    i++
  ) {

    float score =
      result
        .classification[i]
        .value;


    if (
      score >
      bestScore
    ) {

      bestScore =
        score;

      bestLabel =
        result
          .classification[i]
          .label;

      bestIsCrow =
        isCrowClass(
          bestLabel
        );
    }
  }


  int confidence =
    constrain(
      (int)(
        bestScore *
        100.0f +
        0.5f
      ),
      0,
      100
    );


  Serial.print(
    "Edge Impulse: "
  );


  for (
    size_t i = 0;
    i < EI_CLASSIFIER_LABEL_COUNT;
    i++
  ) {

    Serial.print(
      result
        .classification[i]
        .label
    );


    Serial.print(
      "="
    );


    Serial.print(
      result
        .classification[i]
        .value,
      3
    );


    Serial.print(
      " | "
    );
  }


  Serial.println();


  // ========================================================
  // IMPORTANT AI GATE
  //
  // Crow/raven must be >= 95%.
  // ========================================================

  if (
    bestIsCrow &&
    confidence >=
    CROW_CONFIDENCE_MIN
  ) {

    setAiResult(
      "CROW",
      confidence
    );


    setAiStatus(
      "CROW DETECTED"
    );


    crowDetected = true;


    Serial.printf(
      ">>> CROW CONFIRMED: %d%%\n",
      confidence
    );

  } else {

    setAiResult(
      "NO CROW",
      confidence
    );


    setAiStatus(
      "LISTENING"
    );


    crowDetected = false;
  }
}


// ============================================================
// WEB DASHBOARD
// ============================================================

const char webpage[] PROGMEM = R"HTML(

<!DOCTYPE html>

<html lang="en">

<head>

<meta charset="UTF-8">

<meta name="viewport"
content="width=device-width,initial-scale=1">

<meta name="theme-color"
content="#111111">

<title>Scarecrow | Control Panel</title>


<style>

:root {

--bg:#111111;
--card:#1a1a1a;
--border:rgba(255,127,98,.3);
--text:#fff;
--muted:#aaa;
--accent:#ff7f62;
--input:#222;

}


body.light-mode {

--bg:#f5f5f5;
--card:#fff;
--border:rgba(255,127,98,.4);
--text:#111;
--muted:#666;
--input:#eee;

}


* {
box-sizing:border-box;
}


body {

margin:0;

background:var(--bg);

color:var(--text);

font-family:Arial,sans-serif;

border:6px solid var(--accent);

min-height:100vh;

}


nav {

position:sticky;

top:0;

background:var(--bg);

z-index:2;

padding:15px;

border-bottom:1px solid var(--border);

display:flex;

justify-content:space-between;

align-items:center;

gap:12px;

flex-wrap:wrap;

}


h1 {

font-size:23px;

color:var(--accent);

margin:0;

}


h2 {

font-size:19px;

margin:0;

}


small,
.muted {

color:var(--muted);

}


nav a {

color:var(--muted);

text-decoration:none;

font-size:12px;

}


main {

max-width:950px;

margin:24px auto;

padding:0 14px;

}


.card {

background:var(--card);

border:1px solid var(--border);

padding:20px;

margin-bottom:18px;

}


.heading {

display:flex;

justify-content:space-between;

align-items:center;

gap:10px;

flex-wrap:wrap;

padding-bottom:14px;

margin-bottom:16px;

border-bottom:1px solid var(--border);

}


.grid {

display:grid;

grid-template-columns:
repeat(auto-fit,minmax(130px,1fr));

gap:12px;

}


.box {

background:var(--input);

border:1px solid var(--border);

padding:18px 10px;

text-align:center;

}


.box.active {

border-color:var(--accent);

}


.label {

display:block;

font-size:11px;

color:var(--muted);

text-transform:uppercase;

letter-spacing:1px;

}


.value {

display:block;

margin-top:10px;

color:var(--accent);

font-size:17px;

font-weight:bold;

overflow-wrap:anywhere;

}


.switch-row {

display:flex;

align-items:center;

justify-content:space-between;

gap:12px;

margin-bottom:16px;

}


input,
select,
button {

font:inherit;

}


select,
input[type=text],
input[type=password],
input[type=time] {

width:100%;

padding:12px;

margin-top:8px;

background:var(--input);

color:var(--text);

border:1px solid var(--border);

}


input[type=range] {

width:100%;

accent-color:var(--accent);

}


input[type=checkbox] {

accent-color:var(--accent);

width:20px;

height:20px;

}


.control {

margin-bottom:17px;

}


button {

background:var(--accent);

color:#111;

padding:13px;

border:0;

font-weight:bold;

cursor:pointer;

}


.primary {

width:100%;

}


.track {

height:10px;

background:var(--input);

border:1px solid var(--border);

margin:10px 0 18px;

}


.fill {

height:100%;

width:0;

background:var(--accent);

}


.bars {

height:50px;

display:flex;

gap:4px;

align-items:flex-end;

margin:12px 0;

border-bottom:1px solid var(--border);

}


.bar {

flex:1;

min-height:2px;

background:var(--accent);

opacity:.35;

}


.bar.high {

opacity:1;

}


.schedule-status {

padding:12px;

background:var(--input);

border:1px solid var(--border);

margin-top:12px;

font-weight:bold;

color:var(--accent);

}


.time-grid {

display:grid;

grid-template-columns:1fr 1fr;

gap:15px;

}


#toast {

position:fixed;

bottom:-80px;

left:50%;

transform:translateX(-50%);

background:var(--accent);

color:#111;

padding:12px 20px;

font-weight:bold;

transition:bottom .3s;

z-index:5;

}


#toast.show {

bottom:25px;

}


@media(max-width:500px) {

body {

border-width:3px;

}

.card {

padding:14px;

}

.time-grid {

grid-template-columns:1fr;

}

}

</style>

</head>


<body>


<nav>

<div>

<h1>SCARECROW</h1>

<small>
V5.0 | Edge Impulse Bird Detection
</small>

</div>


<div style="display:flex;gap:10px;
flex-wrap:wrap;align-items:center">

<a href="#live">LIVE</a>

<a href="#ai">AI</a>

<a href="#audio">AUDIO</a>

<a href="#schedule">SCHEDULE</a>

<a href="#network">NETWORK</a>


<select
id="themeSelect"
onchange="changeTheme()"
style="width:auto;margin:0">

<option value="dark">Dark</option>

<option value="light">Light</option>

</select>

</div>

</nav>


<main>


<!-- =====================================================
     MASTER
===================================================== -->

<section class="card">


<div class="switch-row">

<div>

<b>System Master Switch</b>

<br>

<small>
Enable automatic playback
</small>

</div>


<input
type="checkbox"
id="mainToggle"
onchange="toggleState('system')">

</div>


<div class="switch-row">

<div>

<b>Edge Impulse AI</b>

<br>

<small>
Local crow/raven sound classification
</small>

</div>


<input
type="checkbox"
id="aiToggle"
onchange="toggleState('ai')">

</div>


</section>


<!-- =====================================================
     LIVE
===================================================== -->

<section
class="card"
id="live">


<div class="heading">

<h2>Live Status</h2>

<span>
Triggers:
<b id="val-count">0</b>
</span>

</div>


<div class="grid">


<div
class="box"
id="box-radar">

<span class="label">
Radar
</span>

<span
class="value"
id="val-radar">

CLEAR

</span>

</div>


<div
class="box"
id="box-pir">

<span class="label">
PIR Sensor
</span>

<span
class="value"
id="val-pir">

CLEAR

</span>

</div>


<div
class="box"
id="box-ai">

<span class="label">
Edge Impulse
</span>

<span
class="value"
id="val-ai">

STARTING

</span>

</div>


<div
class="box"
id="box-audio">

<span class="label">
Speaker
</span>

<span
class="value"
id="val-audio">

IDLE

</span>

</div>


</div>


</section>


<!-- =====================================================
     AI
===================================================== -->

<section
class="card"
id="ai">


<div class="heading">

<h2>AI Prediction</h2>

<span
id="mp-status"
class="muted">

Starting

</span>

</div>


<h2 id="mp-label">

Waiting for audio

</h2>


<p
id="mp-desc"
class="muted">

The trained model classifies microphone audio locally.

</p>


<div class="switch-row">

<span>
Live microphone volume
</span>

<b id="mp-vol">
0
</b>

</div>


<div class="track">

<div
class="fill"
id="mp-vol-bar">
</div>

</div>


<div class="switch-row">

<span>
Crow confidence
</span>

<b id="mp-conf">
--%
</b>

</div>


<div class="bars"
id="conf-graph">
</div>


<small>

AI trigger threshold:
<b>95%</b>

<br>

Last prediction:
<span id="mp-last">
--:--:--
</span>

</small>


</section>


<!-- =====================================================
     AUDIO
===================================================== -->

<section
class="card"
id="audio">


<div class="heading">

<h2>
Audio Configuration
</h2>

</div>


<div class="control">

<label>
SD Card Sound
</label>


<select
id="soundChoice"
onchange="saveSettings()">

<option value="0">
Random Sound (1-3)
</option>

<option value="1">
Track 1 (0001.mp3)
</option>

<option value="2">
Track 2 (0002.mp3)
</option>

<option value="3">
Track 3 (0003.mp3)
</option>

</select>

</div>


<div class="control">

<label>
Play Duration per Repeat
</label>


<select
id="duration"
onchange="saveSettings()">

<option value="5">
5 Seconds
</option>

<option value="10">
10 Seconds
</option>

<option value="15">
15 Seconds
</option>

<option value="30">
30 Seconds
</option>

<option value="60">
60 Seconds
</option>

</select>

</div>


<div class="control">

<label>
Repeat Sound
</label>


<select
id="repeatCount"
onchange="saveSettings()">

<option value="1">
1 Time
</option>

<option value="2">
2 Times
</option>

<option value="3">
3 Times
</option>

<option value="4">
4 Times
</option>

<option value="5">
5 Times
</option>

<option value="10">
10 Times
</option>

</select>

</div>


<div class="control">

<label>

Speaker Volume:

<span id="volValue">
25
</span>

/30

</label>


<input
type="range"
id="volume"
min="0"
max="30"
value="25"
oninput="
document.getElementById('volValue')
.textContent=this.value
"
onchange="saveSettings()">

</div>


<button
class="primary"
onclick="testSound()">

TEST SOUND

</button>


</section>


<!-- =====================================================
     SCHEDULE
===================================================== -->

<section
class="card"
id="schedule">


<div class="heading">

<h2>
Operating Schedule
</h2>

<span
id="scheduleStatus"
class="muted">

DISABLED

</span>

</div>


<div class="switch-row">

<div>

<b>
Enable Schedule
</b>

<br>

<small>
Use real router/NTP time to control when automatic triggering is allowed.
</small>

</div>


<input
type="checkbox"
id="scheduleToggle"
onchange="saveSchedule()">

</div>


<div class="time-grid">


<div class="control">

<label>
Start Time
</label>

<input
type="time"
id="scheduleStart"
value="06:00"
onchange="saveSchedule()">

</div>


<div class="control">

<label>
End Time
</label>

<input
type="time"
id="scheduleEnd"
value="18:00"
onchange="saveSchedule()">

</div>


</div>


<div class="schedule-status">

Current Time:
<span id="scheduleClock">
--:--:--
</span>

<br>

Schedule:
<span id="scheduleStatusText">
DISABLED
</span>

</div>


<p class="muted">

Example:
06:00 → 18:00 means the Scarecrow
automatically operates only during those hours.

<br><br>

If Start Time and End Time are identical,
the schedule operates 24 hours.

<br><br>

Overnight schedules are also supported.
Example: 18:00 → 06:00.

</p>


</section>


<!-- =====================================================
     NETWORK
===================================================== -->

<section
class="card"
id="network">


<div class="heading">

<h2>
Network Setup
</h2>

</div>


<div class="control">

<label>
Wi-Fi Name (SSID)
</label>


<input
type="text"
id="ssid"
placeholder="Router Wi-Fi name">

</div>


<div class="control">

<label>
Wi-Fi Password
</label>


<input
type="password"
id="password"
placeholder="Wi-Fi password">

</div>


<button
class="primary"
onclick="saveWiFi()">

SAVE NETWORK

</button>


<p class="muted">

Dashboard Access Point:
192.168.4.1

</p>


<p class="muted">

Wi-Fi Status:
<span id="wifiStatus">
NOT CONNECTED
</span>

</p>


<p class="muted">

Current Real Time:
<span id="clock">
--:--:--
</span>

</p>


</section>


</main>


<div id="toast">
Saved
</div>


<script>


// =====================================================
// THEME
// =====================================================

function changeTheme() {

const t =
document.getElementById(
'themeSelect'
).value;


document.body.classList.toggle(
'light-mode',
t === 'light'
);


try {

localStorage.setItem(
'scarecrow-theme',
t
);

} catch(e) {}

}


// =====================================================
// TOAST
// =====================================================

function showToast(m) {

const t =
document.getElementById(
'toast'
);

t.textContent = m;

t.classList.add(
'show'
);


setTimeout(
() => t.classList.remove('show'),
2200
);

}


// =====================================================
// SENSOR DISPLAY
// =====================================================

function updateSensor(
id,
active,
waiting
) {

const b =
document.getElementById(
'box-' + id
);


const v =
document.getElementById(
'val-' + id
);


b.classList.remove(
'active'
);


if(active) {

b.classList.add(
'active'
);

v.textContent =
'DETECTED';

}

else if(waiting) {

v.textContent =
'WAITING';

}

else {

v.textContent =
'CLEAR';

}

}


// =====================================================
// REFRESH
// =====================================================

let dataLoaded = false;


async function refreshData() {

try {

const r =
await fetch(
'/api/state',
{
cache:'no-store'
}
);


if(!r.ok) return;


const d =
await r.json();


document.getElementById(
'mainToggle'
).checked =
d.enabled;


document.getElementById(
'aiToggle'
).checked =
d.aiEnabled;


document.getElementById(
'scheduleToggle'
).checked =
d.scheduleEnabled;


document.getElementById(
'val-count'
).textContent =
d.count;


updateSensor(
'radar',
d.radarInstant,
d.radarWindow
);


updateSensor(
'pir',
d.pirInstant,
d.pirWindow
);


// -------------------------
// AI
// -------------------------

document.getElementById(
'val-ai'
).textContent =
d.aiStatus;


document.getElementById(
'mp-status'
).textContent =
d.aiStatus;


document.getElementById(
'mp-last'
).textContent =
d.aiLast;


document.getElementById(
'mp-label'
).textContent =

d.aiLabel === 'CROW'
?
'CROW / RAVEN DETECTED'
:
d.aiLabel === 'NO CROW'
?
'NO CROW DETECTED'
:
d.aiStatus;


document.getElementById(
'mp-desc'
).textContent =

d.aiStatus ===
'AI DISABLED'

?

'AI classification is disabled.'

:

'Classification is running locally using your trained Edge Impulse model.';


const vol =
Number(d.micVolume) || 0;


document.getElementById(
'mp-vol'
).textContent =
vol;


document.getElementById(
'mp-vol-bar'
).style.width =
Math.min(
vol / 1500 * 100,
100
) + '%';


document.getElementById(
'mp-conf'
).textContent =
d.aiConf >= 0
?
d.aiConf + '%'
:
'--%';


const g =
document.getElementById(
'conf-graph'
);


g.innerHTML = '';


(d.confHistory || [])
.forEach(v => {

const bar =
document.createElement(
'div'
);


bar.className =
'bar' +
(v >= 95 ? ' high' : '');


bar.style.height =
Math.max(2,v) + '%';


g.appendChild(bar);

});


document.getElementById(
'box-ai'
).classList.toggle(
'active',
d.aiLabel === 'CROW'
);


// -------------------------
// AUDIO
// -------------------------

const a =
document.getElementById(
'box-audio'
);


if(d.playing) {

a.classList.add(
'active'
);


document.getElementById(
'val-audio'
).textContent =

'PLAYING (' +
d.currentPlayCount +
'/' +
d.repeatCount +
')';

}

else {

a.classList.remove(
'active'
);


document.getElementById(
'val-audio'
).textContent =
d.dfReady
?
'READY'
:
'ERROR';

}


// -------------------------
// TIME
// -------------------------

document.getElementById(
'clock'
).textContent =
d.time;


document.getElementById(
'scheduleClock'
).textContent =
d.time;


document.getElementById(
'scheduleStatus'
).textContent =
d.scheduleStatus;


document.getElementById(
'scheduleStatusText'
).textContent =
d.scheduleStatus;


document.getElementById(
'wifiStatus'
).textContent =
d.wifiConnected
?
'CONNECTED'
:
'NOT CONNECTED';


// -------------------------
// SCHEDULE INPUTS
// -------------------------

if(!dataLoaded) {

document.getElementById(
'volume'
).value =
d.volume;


document.getElementById(
'volValue'
).textContent =
d.volume;


document.getElementById(
'soundChoice'
).value =
d.soundChoice;


document.getElementById(
'duration'
).value =
d.duration;


document.getElementById(
'repeatCount'
).value =
d.repeatCount;


document.getElementById(
'ssid'
).value =
d.savedSSID || '';


document.getElementById(
'scheduleStart'
).value =
d.scheduleStart;


document.getElementById(
'scheduleEnd'
).value =
d.scheduleEnd;


dataLoaded = true;

}


} catch(e) {}

}


// =====================================================
// TOGGLE
// =====================================================

async function toggleState(type) {

const el =
document.getElementById(
type === 'system'
?
'mainToggle'
:
'aiToggle'
);


try {

await fetch(
'/api/toggle?type=' +
type +
'&state=' +
el.checked
);


showToast(
'State updated'
);


refreshData();

} catch(e) {

showToast(
'Request failed'
);

}

}


// =====================================================
// AUDIO SETTINGS
// =====================================================

async function saveSettings() {

const p =
new URLSearchParams({

soundChoice:
document.getElementById(
'soundChoice'
).value,

duration:
document.getElementById(
'duration'
).value,

repeatCount:
document.getElementById(
'repeatCount'
).value,

volume:
document.getElementById(
'volume'
).value

});


try {

await fetch(
'/api/settings?' +
p
);


showToast(
'Settings saved'
);

} catch(e) {

showToast(
'Save failed'
);

}

}


// =====================================================
// TEST SOUND
// =====================================================

async function testSound() {

try {

const r =
await fetch(
'/api/test_sound'
);


showToast(
await r.text()
);


refreshData();

} catch(e) {

showToast(
'Request failed'
);

}

}


// =====================================================
// SCHEDULE SAVE
// =====================================================

async function saveSchedule() {

const enabled =
document.getElementById(
'scheduleToggle'
).checked;


const start =
document.getElementById(
'scheduleStart'
).value;


const end =
document.getElementById(
'scheduleEnd'
).value;


if(!start || !end) {

showToast(
'Select start and end time'
);

return;

}


try {

await fetch(
'/api/schedule?' +
new URLSearchParams({

enabled:
enabled,

start:
start,

end:
end

})
);


showToast(
'Schedule saved'
);


refreshData();

} catch(e) {

showToast(
'Schedule save failed'
);

}

}


// =====================================================
// WIFI
// =====================================================

async function saveWiFi() {

const ssid =
document.getElementById(
'ssid'
).value.trim();


const password =
document.getElementById(
'password'
).value;


if(!ssid) {

showToast(
'SSID required'
);

return;

}


try {

await fetch(
'/api/wifi?' +
new URLSearchParams({

ssid,
password

})
);


document.getElementById(
'password'
).value = '';


showToast(
'Connecting to Wi-Fi'
);


} catch(e) {

showToast(
'Connection request failed'
);

}

}


// =====================================================
// THEME LOAD
// =====================================================

try {

document.getElementById(
'themeSelect'
).value =
localStorage.getItem(
'scarecrow-theme'
) || 'dark';

} catch(e) {}


changeTheme();


// =====================================================
// START REFRESH
// =====================================================

setInterval(
refreshData,
500
);


refreshData();

</script>

</body>

</html>

)HTML";


// ============================================================
// JSON ESCAPE
// ============================================================

String escapeJson(
  const String& input
) {

  String output;

  output.reserve(
    input.length() + 10
  );


  for (
    size_t i = 0;
    i < input.length();
    i++
  ) {

    char c =
      input.charAt(i);


    if (c == '"') {

      output += "\\\"";

    }

    else if (c == '\\') {

      output += "\\\\";

    }

    else {

      output += c;
    }
  }


  return output;
}


// ============================================================
// API: STATE
// ============================================================

void handleState() {

  unsigned long now =
    millis();


  char statusCopy[
    sizeof(aiStatus)
  ];


  char labelCopy[
    sizeof(aiLabel)
  ];


  char timeCopy[
    sizeof(aiLastTime)
  ];


  int confidenceCopy;

  int historyCopy[15];


  portENTER_CRITICAL(
    &aiMux
  );


  strlcpy(
    statusCopy,
    aiStatus,
    sizeof(statusCopy)
  );


  strlcpy(
    labelCopy,
    aiLabel,
    sizeof(labelCopy)
  );


  strlcpy(
    timeCopy,
    aiLastTime,
    sizeof(timeCopy)
  );


  confidenceCopy =
    aiConfidence;


  memcpy(
    historyCopy,
    aiConfHistory,
    sizeof(historyCopy)
  );


  portEXIT_CRITICAL(
    &aiMux
  );


  String json = "{";


  json +=
    "\"enabled\":" +
    String(
      systemEnabled
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"aiEnabled\":" +
    String(
      aiMicEnabled
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"aiStatus\":\"" +
    escapeJson(
      String(statusCopy)
    ) +
    "\",";


  json +=
    "\"aiLabel\":\"" +
    escapeJson(
      String(labelCopy)
    ) +
    "\",";


  json +=
    "\"aiConf\":" +
    String(
      confidenceCopy
    ) +
    ",";


  json +=
    "\"aiLast\":\"" +
    String(timeCopy) +
    "\",";


  // Confidence history

  json +=
    "\"confHistory\":[";


  for (
    int i = 0;
    i < 15;
    i++
  ) {

    json +=
      String(
        historyCopy[i]
      );


    if (i < 14) {
      json += ",";
    }
  }


  json += "],";


  // Microphone

  json +=
    "\"micVolume\":" +
    String(
      currentMicVolume
    ) +
    ",";


  // DFPlayer

  json +=
    "\"dfReady\":" +
    String(
      dfReady
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"playing\":" +
    String(
      playing
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"currentPlayCount\":" +
    String(
      currentPlayCount
    ) +
    ",";


  // Sensors

  json +=
    "\"radarInstant\":" +
    String(
      digitalRead(
        RADAR_PIN
      ) == HIGH
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"pirInstant\":" +
    String(
      digitalRead(
        PIR_PIN
      ) == HIGH
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"radarWindow\":" +
    String(
      radarSeen &&
      now - radarSeenMs <=
      OVERLAP_WINDOW
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"pirWindow\":" +
    String(
      pirSeen &&
      now - pirSeenMs <=
      OVERLAP_WINDOW
      ? "true"
      : "false"
    ) +
    ",";


  // Detection count

  json +=
    "\"count\":" +
    String(
      detectionCount
    ) +
    ",";


  // Audio settings

  json +=
    "\"volume\":" +
    String(
      volumeLevel
    ) +
    ",";


  json +=
    "\"soundChoice\":" +
    String(
      soundChoice
    ) +
    ",";


  json +=
    "\"duration\":" +
    String(
      playDuration
    ) +
    ",";


  json +=
    "\"repeatCount\":" +
    String(
      repeatCount
    ) +
    ",";


  // Wi-Fi

  json +=
    "\"wifiConnected\":" +
    String(
      WiFi.status() ==
      WL_CONNECTED
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"wifiSSID\":\"" +
    escapeJson(
      WiFi.SSID()
    ) +
    "\",";


  json +=
    "\"savedSSID\":\"" +
    escapeJson(
      routerSSID
    ) +
    "\",";


  // Real time

  json +=
    "\"time\":\"" +
    currentTimeString() +
    "\",";


  json +=
    "\"timeValid\":" +
    String(
      clockIsValid()
      ? "true"
      : "false"
    ) +
    ",";


  // ========================================================
  // SCHEDULE
  // ========================================================

  json +=
    "\"scheduleEnabled\":" +
    String(
      scheduleEnabled
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"scheduleStart\":\"" +
    formatScheduleTime(
      scheduleStartHour,
      scheduleStartMinute
    ) +
    "\",";


  json +=
    "\"scheduleEnd\":\"" +
    formatScheduleTime(
      scheduleEndHour,
      scheduleEndMinute
    ) +
    "\",";


  json +=
    "\"scheduleActive\":" +
    String(
      isScheduleActive()
      ? "true"
      : "false"
    ) +
    ",";


  json +=
    "\"scheduleStatus\":\"" +
    scheduleStatusText() +
    "\"";


  json += "}";


  server.send(
    200,
    "application/json",
    json
  );
}


// ============================================================
// API: AUDIO SETTINGS
// ============================================================

void handleSettings() {

  if (
    server.hasArg(
      "soundChoice"
    )
  ) {

    soundChoice =
      constrain(
        server.arg(
          "soundChoice"
        ).toInt(),
        0,
        3
      );
  }


  if (
    server.hasArg(
      "volume"
    )
  ) {

    volumeLevel =
      constrain(
        server.arg(
          "volume"
        ).toInt(),
        0,
        30
      );


    if (dfReady) {

      player.volume(
        volumeLevel
      );
    }
  }


  if (
    server.hasArg(
      "duration"
    )
  ) {

    playDuration =
      constrain(
        server.arg(
          "duration"
        ).toInt(),
        5,
        60
      );
  }


  if (
    server.hasArg(
      "repeatCount"
    )
  ) {

    repeatCount =
      constrain(
        server.arg(
          "repeatCount"
        ).toInt(),
        1,
        10
      );
  }


  saveSettings();


  server.send(
    200,
    "text/plain",
    "Saved"
  );
}


// ============================================================
// API: TOGGLE
// ============================================================

void handleToggle() {

  String type =
    server.arg(
      "type"
    );


  bool targetState =
    server.arg(
      "state"
    ) == "true";


  if (
    type == "system"
  ) {

    systemEnabled =
      targetState;


    if (
      !systemEnabled &&
      playing
    ) {

      stopSound();
    }

  }


  else if (
    type == "ai"
  ) {

    aiMicEnabled =
      targetState;


    if (!aiMicEnabled) {

      crowDetected =
        false;


      setAiStatus(
        "AI DISABLED"
      );

    } else {

      setAiStatus(
        "LISTENING"
      );
    }
  }


  saveSettings();


  server.send(
    200,
    "text/plain",
    "Toggled"
  );
}


// ============================================================
// API: TEST SOUND
// ============================================================

void handleTestSound() {

  if (playing) {

    server.send(
      200,
      "text/plain",
      "Speaker is already playing"
    );

    return;
  }


  playSelectedTrack();


  server.send(
    200,
    "text/plain",
    playing
    ?
    "Playing..."
    :
    "DFPlayer Error"
  );
}


// ============================================================
// API: SCHEDULE
// ============================================================

void handleSchedule() {

  if (
    server.hasArg(
      "enabled"
    )
  ) {

    scheduleEnabled =
      server.arg(
        "enabled"
      ) == "true";
  }


  // -------------------------
  // Start
  // -------------------------

  if (
    server.hasArg(
      "start"
    )
  ) {

    String start =
      server.arg(
        "start"
      );


    int separator =
      start.indexOf(':');


    if (separator > 0) {

      scheduleStartHour =
        start.substring(
          0,
          separator
        ).toInt();


      scheduleStartMinute =
        start.substring(
          separator + 1
        ).toInt();
    }
  }


  // -------------------------
  // End
  // -------------------------

  if (
    server.hasArg(
      "end"
    )
  ) {

    String end =
      server.arg(
        "end"
      );


    int separator =
      end.indexOf(':');


    if (separator > 0) {

      scheduleEndHour =
        end.substring(
          0,
          separator
        ).toInt();


      scheduleEndMinute =
        end.substring(
          separator + 1
        ).toInt();
    }
  }


  // Safety limits

  scheduleStartHour =
    constrain(
      scheduleStartHour,
      0,
      23
    );


  scheduleStartMinute =
    constrain(
      scheduleStartMinute,
      0,
      59
    );


  scheduleEndHour =
    constrain(
      scheduleEndHour,
      0,
      23
    );


  scheduleEndMinute =
    constrain(
      scheduleEndMinute,
      0,
      59
    );


  saveSettings();


  Serial.println(
    "SCHEDULE UPDATED"
  );


  Serial.printf(
    "Enabled: %s\n",
    scheduleEnabled
    ? "YES"
    : "NO"
  );


  Serial.printf(
    "Start: %02d:%02d\n",
    scheduleStartHour,
    scheduleStartMinute
  );


  Serial.printf(
    "End: %02d:%02d\n",
    scheduleEndHour,
    scheduleEndMinute
  );


  server.send(
    200,
    "text/plain",
    "Schedule saved"
  );
}


// ============================================================
// API: WIFI SAVE
// ============================================================

void handleWiFiSave() {

  routerSSID =
    server.arg(
      "ssid"
    );


  routerPassword =
    server.arg(
      "password"
    );


  prefs.putString(
    "routerSSID",
    routerSSID
  );


  prefs.putString(
    "routerPass",
    routerPassword
  );


  WiFi.mode(
    WIFI_AP_STA
  );


  WiFi.setSleep(
    false
  );


  WiFi.begin(
    routerSSID.c_str(),
    routerPassword.c_str()
  );


  ntpConfigured =
    false;


  lastNtpAttemptMs =
    millis();


  server.send(
    200,
    "text/plain",
    "Connecting..."
  );
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(
    115200
  );


  delay(500);


  // ========================================================
  // PINS
  // ========================================================

  pinMode(
    RADAR_PIN,
    INPUT
  );


  pinMode(
    PIR_PIN,
    INPUT
  );


  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );


  pinMode(
    LED_PIN,
    OUTPUT
  );


  digitalWrite(
    LED_PIN,
    LOW
  );


  randomSeed(
    (uint32_t)esp_random()
  );


  // ========================================================
  // PREFERENCES
  // ========================================================

  prefs.begin(
    "scarecrow",
    false
  );


  loadSettings();


  // ========================================================
  // WIFI AP + ROUTER
  // ========================================================

  WiFi.mode(
    WIFI_AP_STA
  );


  WiFi.setSleep(
    false
  );


  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );


  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "SCARECROW V5 STARTING"
  );

  Serial.println(
    "================================"
  );


  Serial.print(
    "Dashboard AP: http://"
  );


  Serial.println(
    WiFi.softAPIP()
  );


  if (
    routerSSID.length() > 0
  ) {

    Serial.print(
      "Connecting to saved Wi-Fi: "
    );


    Serial.println(
      routerSSID
    );


    WiFi.begin(
      routerSSID.c_str(),
      routerPassword.c_str()
    );
  }


  // ========================================================
  // DfPLAYER
  // ========================================================

  dfSerial.begin(
    9600,
    SERIAL_8N1,
    DF_RX_PIN,
    DF_TX_PIN
  );


  delay(1000);


  if (
    player.begin(
      dfSerial,
      true,
      true
    )
    ||
    player.begin(
      dfSerial,
      false,
      false
    )
  ) {

    dfReady = true;


    player.volume(
      volumeLevel
    );


    Serial.println(
      "DFPlayer ready."
    );

  } else {

    dfReady = false;


    Serial.println(
      "DFPlayer not detected."
    );
  }


  // ========================================================
  // EDGE IMPULSE
  // ========================================================

  Serial.println(
    "Initializing Edge Impulse model..."
  );


  run_classifier_init();


  print_results =
    -(EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW);


  microphoneReady =
    microphoneInferenceStart(
      EI_CLASSIFIER_SLICE_SIZE
    );


  if (microphoneReady) {

    setAiStatus(
      "LISTENING"
    );


    Serial.println(
      "Edge Impulse microphone capture started."
    );


    Serial.printf(
      "Model frequency: %d Hz\n",
      EI_CLASSIFIER_FREQUENCY
    );


    Serial.printf(
      "Slice size: %d\n",
      EI_CLASSIFIER_SLICE_SIZE
    );


    Serial.printf(
      "AI trigger threshold: %d%%\n",
      CROW_CONFIDENCE_MIN
    );

  } else {

    setAiStatus(
      "MIC ERROR"
    );


    Serial.println(
      "Microphone/Edge Impulse startup failed."
    );
  }


  // ========================================================
  // WEB SERVER
  // ========================================================

  server.on(
    "/",
    HTTP_GET,
    []() {

      server.send_P(
        200,
        "text/html",
        webpage
      );

    }
  );


  server.on(
    "/api/state",
    HTTP_GET,
    handleState
  );


  server.on(
    "/api/toggle",
    HTTP_GET,
    handleToggle
  );


  server.on(
    "/api/settings",
    HTTP_GET,
    handleSettings
  );


  server.on(
    "/api/schedule",
    HTTP_GET,
    handleSchedule
  );


  server.on(
    "/api/wifi",
    HTTP_GET,
    handleWiFiSave
  );


  server.on(
    "/api/test_sound",
    HTTP_GET,
    handleTestSound
  );


  server.begin();


  Serial.println(
    "Web server started."
  );


  Serial.println(
    "Setup complete."
  );
}


// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  // ========================================================
  // WEB SERVER
  // ========================================================

  server.handleClient();


  // ========================================================
  // CONNECT TO NTP WHEN ROUTER WIFI IS AVAILABLE
  // ========================================================

  if (
    WiFi.status() ==
    WL_CONNECTED
  ) {

    if (
      !ntpConfigured ||
      millis() -
      lastNtpAttemptMs >
      3600000UL
    ) {

      configTzTime(

        "IST-5:30",

        "pool.ntp.org",

        "time.google.com"

      );


      ntpConfigured =
        true;


      lastNtpAttemptMs =
        millis();


      Serial.println(
        "NTP time synchronization requested."
      );
    }
  }


  // ========================================================
  // TIME
  // ========================================================

  unsigned long now =
    millis();


  // ========================================================
  // LED
  // ========================================================

  digitalWrite(
    LED_PIN,
    systemEnabled
    ? HIGH
    : LOW
  );


  // ========================================================
  // SERIAL MICROPHONE VOLUME
  // ========================================================

  if (
    now -
    lastSerialVolMs >=
    150
  ) {

    Serial.printf(
      "Volume:%d | Time:%s | Schedule:%s\n",
      currentMicVolume,
      currentTimeString().c_str(),
      scheduleStatusText().c_str()
    );


    lastSerialVolMs =
      now;
  }


  // ========================================================
  // EDGE IMPULSE
  // ========================================================

  if (microphoneReady) {

    runEdgeImpulseInference();
  }


  // ========================================================
  // PHYSICAL BUTTON
  // ========================================================

  bool buttonState =
    digitalRead(
      BUTTON_PIN
    );


  if (
    buttonState == LOW &&
    previousButtonState == HIGH &&
    now -
    lastButtonMs > 300
  ) {

    systemEnabled =
      !systemEnabled;


    lastButtonMs =
      now;


    if (
      !systemEnabled &&
      playing
    ) {

      stopSound();
    }


    Serial.printf(
      "SYSTEM %s\n",
      systemEnabled
      ?
      "ENABLED"
      :
      "DISABLED"
    );


    saveSettings();
  }


  previousButtonState =
    buttonState;


  // ========================================================
  // PLAYBACK MANAGEMENT
  // ========================================================

  if (playing) {

    bool timeUp =
      now -
      playStartedMs >=
      (unsigned long)
      playDuration *
      1000UL;


    bool hardwareFinished =
      false;


    if (
      dfReady &&
      player.available()
    ) {

      if (
        player.readType() ==
        DFPlayerPlayFinished
      ) {

        hardwareFinished =
          true;
      }
    }


    if (
      timeUp ||
      hardwareFinished
    ) {

      if (
        currentPlayCount <
        repeatCount
      ) {

        currentPlayCount++;


        player.play(
          currentPlayingTrack
        );


        playStartedMs =
          millis();


        Serial.printf(
          "REPEATING TRACK %d/%d\n",
          currentPlayCount,
          repeatCount
        );

      } else {

        stopSound();
      }
    }


    delay(5);

    return;
  }


  // ========================================================
  // SENSOR LOGIC
  //
  // KEPT THE SAME
  // ========================================================

  bool currentRadar =
    digitalRead(
      RADAR_PIN
    ) == HIGH;


  bool currentPir =
    digitalRead(
      PIR_PIN
    ) == HIGH;


  if (currentRadar) {

    radarSeen =
      true;


    radarSeenMs =
      now;
  }


  if (currentPir) {

    pirSeen =
      true;


    pirSeenMs =
      now;
  }


  if (
    radarSeen &&
    now -
    radarSeenMs >
    OVERLAP_WINDOW
  ) {

    radarSeen =
      false;
  }


  if (
    pirSeen &&
    now -
    pirSeenMs >
    OVERLAP_WINDOW
  ) {

    pirSeen =
      false;
  }


  // ========================================================
  // TRIGGER DETECTION
  //
  // Radar/PIR logic is unchanged.
  //
  // Schedule is added as a gate.
  //
  // AI crow detection requires 95%.
  // ========================================================

  if (
    radarSeen ||
    pirSeen ||
    crowDetected
  ) {


    // ======================================================
    // SYSTEM ENABLED
    // ======================================================

    if (!systemEnabled) {

      crowDetected =
        false;
    }


    // ======================================================
    // SCHEDULE GATE
    // ======================================================

    else if (
      !isScheduleActive()
    ) {

      // Detection happened but
      // schedule does not permit playback.

      Serial.println(
        "Detection ignored: OUTSIDE SCHEDULE"
      );


      crowDetected =
        false;
    }


    // ======================================================
    // COOLDOWN
    // ======================================================

    else if (
      now -
      lastTriggerEndMs >=
      (unsigned long)
      cooldownSeconds *
      1000UL
    ) {

      detectionCount++;


      Serial.println(
        "================================"
      );


      Serial.println(
        "TRIGGER ACTIVATED"
      );


      Serial.printf(
        "Schedule: %s\n",
        scheduleStatusText().c_str()
      );


      Serial.printf(
        "Time: %s\n",
        currentTimeString().c_str()
      );


      if (crowDetected) {

        Serial.println(
          "AI: CROW >= 95%"
        );
      }


      Serial.println(
        "Starting Audio!"
      );


      Serial.println(
        "================================"
      );


      playSelectedTrack();

    }


    else {

      crowDetected =
        false;
    }
  }


  delay(5);
}