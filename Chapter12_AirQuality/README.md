# Chapter 12 – Air Quality (SDS011 + Google Sheets)

Files for Chapter 12 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`AirQuality`](arduino/AirQuality/) | SDS011 (PM2.5/PM10) logged to Google Sheets through Apps Script |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

All the libraries used come with the ESP32 board package: nothing to install.

## App Inventor

- [`AirQuality.aia`](app-inventor/AirQuality.aia)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`AirQuality.fzz`](fritzing/AirQuality.fzz) — image: [`AirQuality_bb.png`](fritzing/AirQuality_bb.png)

## Extra files

- `code.gs` — Apps Script code to paste into the Google Sheet (see the chapter)
- Images used by the app: `charts.png`, `parameters.png`, `settings.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
