# Chapter 6 – OLED Display and Temperature

Files for Chapter 6 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`OledTemp`](arduino/OledTemp/) | Part 1: DS18B20 temperature on the OLED display |
| [`OledTempBLE`](arduino/OledTempBLE/) | Part 2: temperature sent to the app over BLE |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| OneWire | Paul Stoffregen | Library Manager |
| DallasTemperature | Miles Burton | Library Manager |
| U8g2 | oliver | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`TemperatureBLE.aia`](app-inventor/TemperatureBLE.aia) — extensions included in the project: [BluetoothLE (MIT)](https://iot.appinventor.mit.edu/iot/reference/bluetoothle/bluetoothleintro)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`TemperatureBLE.fzz`](fritzing/TemperatureBLE.fzz) — image: [`TemperatureBLE_bb.png`](fritzing/TemperatureBLE_bb.png)

## Extra files

- Images used by the app: `BluetoothDisable.png`, `BluetoothEnable.png`. Icons by [Icons8](https://icons8.com).
