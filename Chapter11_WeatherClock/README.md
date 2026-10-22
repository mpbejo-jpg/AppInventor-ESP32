# Chapter 11 – Weather Clock

Files for Chapter 11 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`WeatherClockWiFi`](arduino/WeatherClockWiFi/) | Clock with DS3231 and OpenWeatherMap weather, configured from the app |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| RTClib | Adafruit | Library Manager |
| TFT_eSPI | Bodmer | Library Manager (then configure `User_Setup.h`, see the chapter) |
| ArduinoJson (version 7 or later) | Benoit Blanchon | Library Manager |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`WeatherClockWiFi.aia`](app-inventor/WeatherClockWiFi.aia) — extensions included in the project: OpenWeatherMap

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`WeatherClock.fzz`](fritzing/WeatherClock.fzz) — image: [`WeatherClock_bb.png`](fritzing/WeatherClock_bb.png)

## Extra files

- Images used by the app: `clock.png`, `clouds.png`, `wifi.png`. Icons by [Icons8](https://icons8.com).

## Notes

- Credentials (Wi-Fi network, API keys, cloud service accounts) are not stored in the files: you enter them from the app, as explained in the chapter.
