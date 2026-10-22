# Chapter 5 – Servo Motor

Files for Chapter 5 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`Servo`](arduino/Servo/) | Part 1: servo motor driven by the buttons |
| [`ServoBLE`](arduino/ServoBLE/) | Part 2: servo motor driven from the app over BLE |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| ESP32Servo | Kevin Harrington, John K. Bennett | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`ServoBLE.aia`](app-inventor/ServoBLE.aia) — extensions included in the project: [BluetoothLE (MIT)](https://iot.appinventor.mit.edu/iot/reference/bluetoothle/bluetoothleintro)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`ServoBLE.fzz`](fritzing/ServoBLE.fzz) — image: [`ServoBLE_bb.png`](fritzing/ServoBLE_bb.png)

## Extra files

- Images used by the app: `BluetoothDisable.png`, `BluetoothEnable.png`. Icons by [Icons8](https://icons8.com).
