# Chapter 7 – FM Radio

Files for Chapter 7 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`FMRadio`](arduino/FMRadio/) | Part 1: TEA5767 FM radio with display and buttons |
| [`FMRadioBLE`](arduino/FMRadioBLE/) | Part 2: radio controlled from the app over BLE |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| U8g2 | oliver | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`FMRadioBLE.aia`](app-inventor/FMRadioBLE.aia) — extensions included in the project: [BluetoothLE (MIT)](https://iot.appinventor.mit.edu/iot/reference/bluetoothle/bluetoothleintro)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`FMRadio.fzz`](fritzing/FMRadio.fzz) — image: [`FMRadio_bb.png`](fritzing/FMRadio_bb.png)

## Extra files

- Images used by the app: `BluetoothDisable.png`, `BluetoothEnable.png`, `bin.png`, `left.png`, `mute.png`, `mylist.png`, `right.png`, `save.png`, `voice.png`. Icons by [Icons8](https://icons8.com).
