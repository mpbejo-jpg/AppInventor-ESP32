# Chapter 17 – Heart Rate and SpO2 (MAX30102)

Files for Chapter 17 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`PulseOximeterBle`](arduino/PulseOximeterBle/) | Part 2: heart rate, SpO2 and waveform sent to the app over BLE |
| [`PulseOximeterOled`](arduino/PulseOximeterOled/) | Part 1: MAX30102 heart rate and SpO2 on the OLED display |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| U8g2 | oliver | Library Manager |
| SparkFun MAX3010x Pulse and Proximity Sensor Library | SparkFun Electronics | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`PulseOximeter.aia`](app-inventor/PulseOximeter.aia) — extensions included in the project: [BluetoothLE (MIT)](https://iot.appinventor.mit.edu/iot/reference/bluetoothle/bluetoothleintro)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`PulseOximeter.fzz`](fritzing/PulseOximeter.fzz) — image: [`PulseOximeter_bb.png`](fritzing/PulseOximeter_bb.png)

## Extra files

- `waveform.html` — HTML page with the scrolling chart shown in the app's WebViewer

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
