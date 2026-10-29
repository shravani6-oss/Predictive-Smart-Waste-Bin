# Predictive Smart Waste Bin

An ESP32-based IoT prototype that monitors a waste bin's estimated fill level, reads weight data from a load cell through an HX711 amplifier, sends telemetry to ThingSpeak, and sends Telegram notifications when fill-level thresholds are crossed.

## Features

- **Ultrasonic fill-level estimation:** Reads distance from an ultrasonic sensor and converts it to an estimated bin fill percentage.
- **Smoothed readings:** Applies a simple smoothing calculation to reduce sudden changes in fill-level readings.
- **Weight measurement:** Reads load-cell data through an HX711 amplifier.
- **Cloud monitoring:** Sends fill percentage, available weight, estimated time-to-full, and a simulated footfall value to ThingSpeak.
- **Telegram notifications:** Sends alerts when the estimated fill level passes configured thresholds and when the basic algorithm predicts the bin may become full soon.
- **Status classification:** Labels the bin as `Empty`, `Normal`, `Filling Fast`, or `Full`.
- **Serial monitoring:** Prints distance, fill level, weight, status, and prediction information to the Serial Monitor.

## Hardware

- ESP32 development board
- Ultrasonic distance sensor compatible with your wiring and voltage levels
- Load cell
- HX711 load-cell amplifier module
- LED (optional; the sketch uses the ESP32 GPIO 2 by default)
- Breadboard, jumper wires, and suitable power supply

> **Board note:** This sketch uses `WiFi.h` and `HTTPClient.h` for ESP32. It is not an Arduino Uno sketch without substantial changes.

## Pin configuration

The default GPIO assignments in `SmartWasteBin.ino` are:

| Component signal | ESP32 GPIO |
|---|---:|
| Ultrasonic TRIG | 5 |
| Ultrasonic ECHO | 18 |
| HX711 DT / DOUT | 4 |
| HX711 SCK / CLK | 25 |
| LED | 2 |

Check your exact sensor modules and board before wiring. In particular, ensure the ultrasonic sensor's ECHO output does not exceed the ESP32's 3.3 V input limit; use an appropriate voltage divider or level shifter if required.

## Software and libraries

- Arduino IDE
- ESP32 board support package for Arduino
- `HX711` Arduino library
- A Wi-Fi network with internet access
- A ThingSpeak account and channel with fields configured
- A Telegram bot and chat ID, if Telegram notifications are desired

The sketch uses ESP32's `WiFi.h`, `HTTPClient.h`, and `WiFiClientSecure.h` libraries.

## Configuration

1. Open `SmartWasteBin.ino` in Arduino IDE.
2. In the configuration section, replace the placeholders locally:
   - `YOUR_WIFI_SSID`
   - `YOUR_WIFI_PASSWORD`
   - `YOUR_THINGSPEAK_WRITE_API_KEY`
   - `YOUR_TELEGRAM_BOT_TOKEN`
   - `YOUR_TELEGRAM_CHAT_ID`
3. **Never commit real passwords, API keys, or bot tokens to a public repository.** Keep local credentials out of Git and rotate any credentials that were previously exposed.
4. Set `EMPTY_DISTANCE_CM` to the measured distance from the ultrasonic sensor to the bottom of the empty bin.
5. Calibrate `SCALE_CALIBRATION_FACTOR` for your specific load cell and HX711 setup. The default value is only a starting point.
6. Select the correct ESP32 board and port in Arduino IDE, then verify and upload.

If you do not want to configure Telegram or ThingSpeak yet, leave their values as placeholders; the sketch skips those integrations when placeholder values are detected.

## ThingSpeak channel fields

The sketch sends these values:

| Field | Data |
|---|---|
| Field 1 | Estimated fill percentage |
| Field 2 | Weight in grams, when available |
| Field 3 | Estimated time to full in minutes |
| Field 4 | Simulated footfall value |

Create matching fields in your ThingSpeak channel. The sketch attempts an update every 15 seconds.

## Telegram alerts

The sketch attempts to send messages when:
- Estimated fill level rises above 50%.
- Estimated fill level rises above 70%.
- The basic time-to-full estimate is positive and below 10 minutes.
- The fill level falls below 20%, which resets the alert flags for a later cycle.

The LED is switched on for threshold alerts and switched off again when the fill level falls below 20%.

## How the prediction works

The sketch keeps a short history of recent fill-level readings and estimates the rate of fill increase. If the recent trend is positive, it calculates an approximate time until the estimated fill level reaches 100%.

This is a **simple rule-based estimate**, not a trained machine-learning model. Its accuracy depends on sensor quality, the bin's geometry, sampling interval, and actual waste-disposal patterns.

## Important limitations

- **Footfall is simulated:** The current sketch uses a random value for footfall. It does not measure actual people or foot traffic.
- **MQ-135 is not implemented:** This sketch does not read an MQ-135 air-quality/gas sensor.
- **No machine-learning model:** The time-to-full value is calculated from recent readings using a basic trend estimate.
- **Sensor calibration is required:** Set the empty-bin distance and calibrate the load cell before relying on the reported values.
- **Network dependence:** ThingSpeak and Telegram updates require Wi-Fi and internet connectivity.
- **TLS note:** The Telegram helper uses `client.setInsecure()` for prototype simplicity, which does not validate the server certificate. For production use, configure certificate validation with a trusted CA certificate.
- **Hardware testing:** Verify the wiring and test all sensor readings on your own hardware before using the system in a real waste-management setting.

## Repository structure

```text
Predictive-Smart-Waste-Bin/
├── SmartWasteBin.ino
└── README.md
```

You can optionally add project photos, a wiring diagram, or screenshots of your ThingSpeak dashboard.

## Future improvements

- Add a real footfall sensor and remove the simulated footfall value.
- Integrate and calibrate an MQ-135 sensor if air-quality monitoring is required.
- Improve the time-to-full estimation using longer-term historical data.
- Add sensor error reporting and more robust network reconnection.
- Store secrets outside the source code and use certificate validation for HTTPS.

## License

Add a license if you want to specify how others may use, modify, and distribute this project.
