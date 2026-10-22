# Chapter 20 – Voice Commands with TinyML

Files for Chapter 20 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`TinyML_VoiceRelay`](arduino/TinyML_VoiceRelay/) | Voice commands (Edge Impulse) plus control from the app over MQTT |
| [`TinyML_VoiceRelay_Local`](arduino/TinyML_VoiceRelay_Local/) | Voice commands with no network: recognition runs on the ESP32 |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| VoiceRelay_inferencing (Edge Impulse model) | — | **Sketch → Include Library → Add .ZIP Library** and select the zip in `extra/` |
| PubSubClient | Nick O'Leary | Library Manager |
| U8g2 | oliver | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`AiSpeechRecognition.aia`](app-inventor/AiSpeechRecognition.aia) — extensions included in the project: [UrsAI2PahoMQTT](http://UllisRoboterSeite.de/android-AI2-PahoMQTT.html)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`TinyML_VoiceRelay.fzz`](fritzing/TinyML_VoiceRelay.fzz) — image: [`TinyML_VoiceRelay_bb.png`](fritzing/TinyML_VoiceRelay_bb.png)

## Extra files

- `ei-voicerelay-arduino-1.0.1-impulse-#1.zip` — Arduino library of the Edge Impulse model (`VoiceRelay_inferencing`), to install as a .ZIP library
- Images used by the app: `edit.png`, `microphone.png`, `settings.png`, `speech-bubble.png`. Icons by [Icons8](https://icons8.com).

## Notes

- **Training audio samples**: not included in the repository because of their size. The chapter explains how to obtain them from the original dataset. You don't need them to use the project as is: the trained model is in the `VoiceRelay_inferencing` library in `extra/`.
- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
