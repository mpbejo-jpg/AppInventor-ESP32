# Chapter 19 – Video Doorphone

Files for Chapter 19 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`Videodoorphone`](arduino/Videodoorphone/) | Video doorphone with audio/video streaming to the app |

**Board**: Freenove ESP32-S3 WROOM Camera (board: **ESP32S3 Dev Module**, PSRAM enabled)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| ArduinoJson (version 7 or later) | Benoit Blanchon | Library Manager |
| WebSockets | Markus Sattler | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`Videodoorphone.aia`](app-inventor/Videodoorphone.aia) — extensions included in the project: AudioStreamExt, Pushy, WebSocketExt

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`Videodoorphone.fzz`](fritzing/Videodoorphone.fzz) — image: [`Videodoorphone_bb.png`](fritzing/Videodoorphone_bb.png)

## Extra files

- `github_render` — Node.js relay server deployed on Render
- Images used by the app: `CloseCall.png`, `OpenDoor.png`, `Settings.png`, `videodoorbell.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
