# Chapter 16 – RFID Access Control with ESP-NOW

Files for Chapter 16 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`RfidDoorESPNow`](arduino/RfidDoorESPNow/) | RFID door node linked to the thermostat over ESP-NOW |
| [`ThermostatESPNowUpdate`](arduino/ThermostatESPNowUpdate/) | Updated thermostat: ESP-NOW to the nodes, MQTT to the app |
| [`WindowSensorESPNowUpdate`](arduino/WindowSensorESPNowUpdate/) | Window sensor updated to work alongside the RFID node |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| MFRC522 | GithubCommunity | Library Manager |
| TFT_eSPI | Bodmer | Library Manager (then configure `User_Setup.h`, see the chapter) |
| OneWire | Paul Stoffregen | Library Manager |
| DallasTemperature | Miles Burton | Library Manager |
| PubSubClient | Nick O'Leary | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`ThermostatMqtt.aia`](app-inventor/ThermostatMqtt.aia) — extensions included in the project: FirebaseManager, [UrsAI2PahoMQTT](http://UllisRoboterSeite.de/android-AI2-PahoMQTT.html), Mifare

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`RFIDESPNow.fzz`](fritzing/RFIDESPNow.fzz) — image: [`RFIDESPNow_bb.png`](fritzing/RFIDESPNow_bb.png)
- [`ThermostatESPNow.fzz`](fritzing/ThermostatESPNow.fzz) — image: [`ThermostatESPNow_bb.png`](fritzing/ThermostatESPNow_bb.png)
- [`WindowSensorESPNow.fzz`](fritzing/WindowSensorESPNow.fzz) — image: [`WindowSensorESPNow_bb.png`](fritzing/WindowSensorESPNow_bb.png)

## Extra files

- Images used by the app: `fanMax.png`, `fanMin.png`, `minus.png`, `plus.png`, `rfid.png`, `settings.png`, `snow.png`, `sun.png`, `thermostat.png`, `window.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
