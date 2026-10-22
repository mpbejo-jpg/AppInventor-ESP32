# Chapter 13 – Water Quality (ThingSpeak)

Files for Chapter 13 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`WaterQuality`](arduino/WaterQuality/) | TDS, turbidity and temperature sent to ThingSpeak |
| [`WaterQualityCalibration`](arduino/WaterQualityCalibration/) | Guided calibration of the TDS and turbidity sensors (Serial Monitor) |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| OneWire | Paul Stoffregen | Library Manager |
| DallasTemperature | Miles Burton | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`WaterQuality.aia`](app-inventor/WaterQuality.aia)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`WaterQuality.fzz`](fritzing/WaterQuality.fzz) — image: [`WaterQuality_bb.png`](fritzing/WaterQuality_bb.png)

## Extra files

- Images used by the app: `charts.png`, `parameters.png`, `settings.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
