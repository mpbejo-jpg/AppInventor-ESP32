# Chapter 15 – RFID Access Control with MQTT

Files for Chapter 15 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`RfidMqtt`](arduino/RfidMqtt/) | RFID reader receiving commands from the app over MQTT (CloudAMQP) |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| MFRC522 | GithubCommunity | Library Manager |
| PubSubClient | Nick O'Leary | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`RfidMqtt.aia`](app-inventor/RfidMqtt.aia) — extensions included in the project: [UrsAI2PahoMQTT](http://UllisRoboterSeite.de/android-AI2-PahoMQTT.html), Mifare

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`RFIDESPNow.fzz`](fritzing/RFIDESPNow.fzz) — image: [`RFIDESPNow_bb.png`](fritzing/RFIDESPNow_bb.png)

## Extra files

- Images used by the app: `rfid.png`, `settings.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
