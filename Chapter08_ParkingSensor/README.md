# Chapter 8 – Parking Sensor

Files for Chapter 8 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`ParkingSensor`](arduino/ParkingSensor/) | Part 1: VL53L1X distance sensor with RGB LED |
| [`ParkingSensorBLE`](arduino/ParkingSensorBLE/) | Part 2: distance sent to the app over BLE |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| SparkFun VL53L1X 4m Laser Distance Sensor | SparkFun Electronics | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`ParkingSensor.aia`](app-inventor/ParkingSensor.aia) — extensions included in the project: [BluetoothLE (MIT)](https://iot.appinventor.mit.edu/iot/reference/bluetoothle/bluetoothleintro)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`ParkingSensor.fzz`](fritzing/ParkingSensor.fzz) — image: [`ParkingSensor_bb.png`](fritzing/ParkingSensor_bb.png)

## Extra files

- `beep.wav` — sound used by the app
- Images used by the app: `BluetoothDisable.png`, `BluetoothEnable.png`. Icons by [Icons8](https://icons8.com).
