# Chapter 14 – Thermostat with ESP-NOW

Files for Chapter 14 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`ThermostatESPNow`](arduino/ThermostatESPNow/) | Thermostat with touch display, relays and ESP-NOW receiver |
| [`WindowSensorESPNow`](arduino/WindowSensorESPNow/) | Window sensor talking to the thermostat over ESP-NOW |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| TFT_eSPI | Bodmer | Library Manager (then configure `User_Setup.h`, see the chapter) |
| OneWire | Paul Stoffregen | Library Manager |
| DallasTemperature | Miles Burton | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`ThermostatWiFi.aia`](app-inventor/ThermostatWiFi.aia)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`ThermostatESPNow.fzz`](fritzing/ThermostatESPNow.fzz) — image: [`ThermostatESPNow_bb.png`](fritzing/ThermostatESPNow_bb.png)
- [`WindowSensorESPNow.fzz`](fritzing/WindowSensorESPNow.fzz) — image: [`WindowSensorESPNow_bb.png`](fritzing/WindowSensorESPNow_bb.png)

## Extra files

- Images used by the app: `fanMax.png`, `fanMin.png`, `minus.png`, `plus.png`, `settings.png`, `snow.png`, `sun.png`, `thermostat.png`, `window.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
