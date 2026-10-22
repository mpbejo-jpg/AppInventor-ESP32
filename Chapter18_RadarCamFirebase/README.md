# Chapter 18 – Motion-Triggered Camera with Firebase

Files for Chapter 18 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`RadarCam`](arduino/RadarCam/) | Photo on motion detection, uploaded to Firebase Storage |

**Board**: Freenove ESP32-S3 WROOM Camera (board: **ESP32S3 Dev Module**, PSRAM enabled)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| ArduinoJson (version 7 or later) | Benoit Blanchon | Library Manager |
| Adafruit NeoPixel | Adafruit | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`RadarCam.aia`](app-inventor/RadarCam.aia) — extensions included in the project: FirebaseManager

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`RadarCam.fzz`](fritzing/RadarCam.fzz) — image: [`RadarCam_bb.png`](fritzing/RadarCam_bb.png)

## Extra files

- Images used by the app: `delete.png`, `parameters.png`, `pictures.png`, `settings.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
