/*
  Predictive Smart Waste Bin
  Board: ESP32
  Sensors: Ultrasonic sensor (TRIG/ECHO), HX711 load-cell amplifier
  Cloud: ThingSpeak
  Notifications: Telegram Bot API

  SECURITY:
  - Do not commit real Wi-Fi passwords, ThingSpeak write keys, or Telegram bot tokens.
  - Configure credentials in the Arduino IDE under:
      Tools > ... (or use a separate untracked secrets header).
  - This sketch uses placeholders below. Replace them locally before uploading to hardware.
  - If credentials were previously exposed, rotate/revoke them before reuse.

  IMPORTANT:
  - This code is for ESP32, not Arduino Uno, because it uses WiFi.h and HTTPClient.h.
  - Footfall is simulated with random() in the original logic; it is NOT real footfall sensing.
  - MQ-135 is not implemented in this sketch.
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "HX711.h"

// ================= SECURITY CONFIGURATION =================
// Replace these placeholders locally. Do not commit real values.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ThingSpeak Write API Key (keep secret)
const char* THINGSPEAK_WRITE_API_KEY = "YOUR_THINGSPEAK_WRITE_API_KEY";

// Telegram bot token and chat ID (keep token secret)
const char* TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN";
const char* TELEGRAM_CHAT_ID = "YOUR_TELEGRAM_CHAT_ID";

// ================= PIN CONFIGURATION (ESP32) =================
constexpr uint8_t TRIG_PIN = 5;
constexpr uint8_t ECHO_PIN = 18;
constexpr uint8_t HX711_DT_PIN = 4;
constexpr uint8_t HX711_SCK_PIN = 25;
constexpr uint8_t LED_PIN = 2;

// Set this to the measured inside height of your bin in centimetres.
constexpr float EMPTY_DISTANCE_CM = 25.0f;

// Original scale calibration factor; calibrate for your own load cell.
constexpr float SCALE_CALIBRATION_FACTOR = -800.0f;

// ThingSpeak free channels generally require at least 15 seconds between updates.
constexpr unsigned long THINGSPEAK_INTERVAL_MS = 15000UL;
constexpr unsigned long SENSOR_INTERVAL_MS = 1000UL;

HX711 scale;

float lastDistanceCm = EMPTY_DISTANCE_CM;
float lastFillPercent = 0.0f;
float fillHistory[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
uint8_t fillHistoryIndex = 0;
uint8_t fillHistoryCount = 0;

bool almostAlertSent = false;
bool fullAlertSent = false;
bool predictionAlertSent = false;

unsigned long lastThingSpeakSend = 0;
unsigned long lastSensorRead = 0;

// Encode text safely for use as a URL query parameter.
String urlEncode(const String& input) {
  String encoded;
  const char hex[] = "0123456789ABCDEF";

  for (size_t i = 0; i < input.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(input[i]);
    const bool safe =
      (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
      c == '.' || c == '~';

    if (safe) {
      encoded += static_cast<char>(c);
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }
  return encoded;
}

float getDistanceCm() {
  float sum = 0.0f;
  uint8_t validReadings = 0;

  for (uint8_t i = 0; i < 5; ++i) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // Timeout prevents pulseIn from blocking for too long.
    const unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000UL);
    if (duration > 0) {
      const float distance = (duration * 0.0343f) / 2.0f;
      if (distance >= 2.0f && distance <= 400.0f) {
        sum += distance;
        ++validReadings;
      }
    }
    delay(10);
  }

  if (validReadings == 0) {
    return -1.0f;
  }
  return sum / validReadings;
}

float getFillLevelPercent(float distanceCm) {
  if (distanceCm <= 3.0f) {
    return 100.0f;
  }

  float fill = ((EMPTY_DISTANCE_CM - distanceCm) / EMPTY_DISTANCE_CM) * 100.0f;
  if (fill < 0.0f) fill = 0.0f;
  if (fill > 100.0f) fill = 100.0f;
  return fill;
}

float getWeightGrams() {
  if (!scale.is_ready()) {
    Serial.println("HX711 not ready; weight unavailable.");
    return -1.0f;
  }

  float weight = scale.get_units(5);
  if (weight < 0.0f) weight = -weight;
  if (weight < 2.0f) weight = 0.0f;
  return weight;
}

void sendTelegram(const String& message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; Telegram alert not sent.");
    return;
  }

  if (String(TELEGRAM_BOT_TOKEN) == "YOUR_TELEGRAM_BOT_TOKEN" ||
      String(TELEGRAM_CHAT_ID) == "YOUR_TELEGRAM_CHAT_ID") {
    Serial.println("Telegram credentials are placeholders; skipping Telegram.");
    return;
  }

  WiFiClientSecure client;
  // For a quick student prototype. For production, validate the server certificate
  // using setCACert() instead of setInsecure().
  client.setInsecure();

  HTTPClient http;
  const String url = String("https://api.telegram.org/bot") +
                     TELEGRAM_BOT_TOKEN + "/sendMessage";

  if (!http.begin(client, url)) {
    Serial.println("Could not start Telegram HTTPS request.");
    return;
  }

  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body = "chat_id=" + urlEncode(String(TELEGRAM_CHAT_ID)) +
                      "&text=" + urlEncode(message);
  const int httpCode = http.POST(body);

  Serial.print("Telegram HTTP code: ");
  Serial.println(httpCode);
  http.end();
}

void sendToThingSpeak(float fillPercent, float weightGrams,
                      float timeToFullMinutes, int footfallValue) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; ThingSpeak update skipped.");
    return;
  }

  if (String(THINGSPEAK_WRITE_API_KEY) == "YOUR_THINGSPEAK_WRITE_API_KEY") {
    Serial.println("ThingSpeak key is a placeholder; skipping cloud update.");
    return;
  }

  HTTPClient http;
  String url = "https://api.thingspeak.com/update?api_key=";
  url += THINGSPEAK_WRITE_API_KEY;
  url += "&field1=" + String(fillPercent, 1);
  if (weightGrams >= 0.0f) {
    url += "&field2=" + String(weightGrams, 1);
  }
  url += "&field3=" + String(timeToFullMinutes, 1);
  url += "&field4=" + String(footfallValue);

  if (!http.begin(url)) {
    Serial.println("Could not start ThingSpeak request.");
    return;
  }

  const int httpCode = http.GET();
  Serial.print("ThingSpeak HTTP code: ");
  Serial.println(httpCode);
  if (httpCode == 200) {
    const String response = http.getString();
    Serial.print("ThingSpeak entry response: ");
    Serial.println(response);
    if (response == "0") {
      Serial.println("ThingSpeak rejected update; check key and update interval.");
    }
  }
  http.end();
}

float estimateTimeToFullMinutes(float currentFillPercent) {
  // Use only actual stored readings and calculate average fill increase
  // per second. A prediction is made only when the trend is positive.
  if (fillHistoryCount < 2) {
    return 0.0f;
  }

  const uint8_t oldestIndex = fillHistoryIndex;
  const uint8_t newestIndex = (fillHistoryIndex + 4) % 5;
  const float oldest = fillHistory[oldestIndex];
  const float newest = fillHistory[newestIndex];

  const float elapsedSeconds =
    static_cast<float>(fillHistoryCount - 1) *
    (SENSOR_INTERVAL_MS / 1000.0f);

  if (elapsedSeconds <= 0.0f) {
    return 0.0f;
  }

  const float ratePerSecond = (newest - oldest) / elapsedSeconds;
  if (ratePerSecond <= 0.0f || currentFillPercent >= 100.0f) {
    return 0.0f;
  }

  const float secondsToFull = (100.0f - currentFillPercent) / ratePerSecond;
  return secondsToFull / 60.0f;
}

String getStatus(float fillPercent) {
  if (fillPercent < 30.0f) return "Empty";
  if (fillPercent < 70.0f) return "Normal";
  if (fillPercent < 90.0f) return "Filling Fast";
  return "Full";
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to Wi-Fi");
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 20000UL) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi connection timed out; device will continue offline.");
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  scale.begin(HX711_DT_PIN, HX711_SCK_PIN);
  scale.set_scale(SCALE_CALIBRATION_FACTOR);
  delay(500);

  if (scale.is_ready()) {
    scale.tare();
    Serial.println("HX711 ready and tared.");
  } else {
    Serial.println("Warning: HX711 not ready at startup.");
  }

  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    sendTelegram("Smart Bin System is online.");
  }

  Serial.println("Smart Waste Bin system ready.");
}

void loop() {
  if (millis() - lastSensorRead < SENSOR_INTERVAL_MS) {
    delay(10);
    return;
  }
  lastSensorRead = millis();

  // ================= DISTANCE AND FILL =================
  const float measuredDistance = getDistanceCm();
  if (measuredDistance >= 0.0f) {
    // Ignore sudden jumps larger than 10 cm, matching the original intent.
    if (lastDistanceCm <= 0.0f ||
        fabs(measuredDistance - lastDistanceCm) <= 10.0f) {
      lastDistanceCm = measuredDistance;
    }
  } else {
    Serial.println("Ultrasonic reading invalid; using last valid distance.");
  }

  float fillPercent = getFillLevelPercent(lastDistanceCm);
  fillPercent = (0.7f * fillPercent) + (0.3f * lastFillPercent);
  lastFillPercent = fillPercent;

  fillHistory[fillHistoryIndex] = fillPercent;
  fillHistoryIndex = (fillHistoryIndex + 1) % 5;
  if (fillHistoryCount < 5) ++fillHistoryCount;

  // ================= WEIGHT =================
  const float weightGrams = getWeightGrams();

  // ================= PREDICTION =================
  const float timeToFullMinutes = estimateTimeToFullMinutes(fillPercent);

  // ================= STATUS =================
  const String status = getStatus(fillPercent);

  // The original code generated a random footfall value. Keep that behavior
  // explicit for compatibility, but it is simulated and not sensor-measured.
  const int simulatedFootfall = random(1, 10);

  // ================= ALERTS =================
  if (fillPercent > 50.0f && !almostAlertSent) {
    digitalWrite(LED_PIN, HIGH);
    sendTelegram("Smart Bin is over 50% full. Please monitor.");
    almostAlertSent = true;
  }

  if (fillPercent > 70.0f && !fullAlertSent) {
    digitalWrite(LED_PIN, HIGH);
    sendTelegram("Smart Bin is over 70% full. Please empty it soon.");
    fullAlertSent = true;
  }

  if (timeToFullMinutes > 0.0f &&
      timeToFullMinutes < 10.0f &&
      !predictionAlertSent) {
    sendTelegram("Smart Bin is predicted to be full in less than 10 minutes.");
    predictionAlertSent = true;
  }

  if (fillPercent < 20.0f) {
    digitalWrite(LED_PIN, LOW);
    fullAlertSent = false;
    almostAlertSent = false;
    predictionAlertSent = false;
  }

  // ================= THINGSPEAK =================
  if (lastThingSpeakSend == 0 ||
      millis() - lastThingSpeakSend >= THINGSPEAK_INTERVAL_MS) {
    sendToThingSpeak(fillPercent, weightGrams, timeToFullMinutes,
                     simulatedFootfall);
    lastThingSpeakSend = millis();
  }

  // ================= SERIAL MONITOR =================
  Serial.print("Distance: ");
  Serial.print(lastDistanceCm, 1);
  Serial.print(" cm | Fill: ");
  Serial.print(fillPercent, 1);
  Serial.print("% | Weight: ");
  if (weightGrams >= 0.0f) {
    Serial.print(weightGrams, 1);
    Serial.print(" g");
  } else {
    Serial.print("unavailable");
  }
  Serial.print(" | Status: ");
  Serial.print(status);
  Serial.print(" | Time to full: ");
  Serial.print(timeToFullMinutes, 1);
  Serial.print(" min | Simulated footfall: ");
  Serial.println(simulatedFootfall);
}
