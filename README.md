# App Inventor \& ESP32 – Book files

This repository contains all the files for the chapters of the book *App Inventor \& ESP32*: Arduino sketches, App Inventor projects, Fritzing diagrams and extra files.

## Chapters

|No.|Title|Folder|
|-|-|-|
|1|Hello World|[`Chapter01\_HelloWorld`](Chapter01_HelloWorld/)|
|2|Photocell|[`Chapter02\_PhotoCell`](Chapter02_PhotoCell/)|
|3|Traffic Light|[`Chapter03\_TrafficLight`](Chapter03_TrafficLight/)|
|4|RGB LED|[`Chapter04\_LedRGB`](Chapter04_LedRGB/)|
|5|Servo Motor|[`Chapter05\_Servo`](Chapter05_Servo/)|
|6|OLED Display and Temperature|[`Chapter06\_OledTemp`](Chapter06_OledTemp/)|
|7|FM Radio|[`Chapter07\_FMRadio`](Chapter07_FMRadio/)|
|8|Parking Sensor|[`Chapter08\_ParkingSensor`](Chapter08_ParkingSensor/)|
|9|Web Server and Relay|[`Chapter09\_WebServerRelay`](Chapter09_WebServerRelay/)|
|10|Tic Tac Toe|[`Chapter10\_TicTacToe`](Chapter10_TicTacToe/)|
|11|Weather Clock|[`Chapter11\_WeatherClock`](Chapter11_WeatherClock/)|
|12|Air Quality (SDS011 + Google Sheets)|[`Chapter12\_AirQuality`](Chapter12_AirQuality/)|
|13|Water Quality (ThingSpeak)|[`Chapter13\_WaterQuality`](Chapter13_WaterQuality/)|
|14|Thermostat with ESP-NOW|[`Chapter14\_ThermostatESPNow`](Chapter14_ThermostatESPNow/)|
|15|RFID Access Control with MQTT|[`Chapter15\_RFIDMQTT`](Chapter15_RFIDMQTT/)|
|16|RFID Access Control with ESP-NOW|[`Chapter16\_RFIDESPNow`](Chapter16_RFIDESPNow/)|
|17|Heart Rate and SpO2 (MAX30102)|[`Chapter17\_HeartRateSpO2`](Chapter17_HeartRateSpO2/)|
|18|Motion-Triggered Camera with Firebase|[`Chapter18\_RadarCamFirebase`](Chapter18_RadarCamFirebase/)|
|19|Video Doorphone|[`Chapter19\_Videodoorphone`](Chapter19_Videodoorphone/)|
|20|Voice Commands with TinyML|[`Chapter20\_TinyMLVoiceRelay`](Chapter20_TinyMLVoiceRelay/)|

## Chapter structure

```
ChapterNN\_Name/
├── README.md        libraries, board and notes for the chapter
├── arduino/         sketches (each sketch in its own folder)
├── app-inventor/    .aia project
├── fritzing/        .fzz diagram and .png image
└── extra/           images, HTML files, scripts
```

The `extensions/` folder contains the App Inventor extensions (`.aix`) used in the book.

## How to download

* **Latest version**: green **Code** button → **Download ZIP**.
* **Version matching your edition of the book**: **Releases** section (for example, *V1.0* for the first edition) → **Source code (zip)**.

## How to open the files

* **Sketches (`.ino`)**: open the sketch folder with the Arduino IDE. Each `.ino` file is in a folder with the same name, as the IDE requires.
* **App Inventor projects (`.aia`)**: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.
* **Extensions (`.aix`)**: they are usually already included in the `.aia`. If you need one separately, in the Designer use **Extension → Import extension**.
* **Diagrams (`.fzz`)**: open the file with Fritzing. If you don't have Fritzing, look at the `.png` image in the same folder.

## Tested versions

|Software|Version|
|-|-|
|Arduino IDE|2.3.10|
|ESP32 board package (Espressif)|3.3.10|
|Fritzing|1.07|

## License

© 2026 Marco Perrone
Code (sketches, scripts, App Inventor projects and the author's extensions): MIT License. You can use, modify and reuse it freely, including in your own projects, as long as you keep the copyright notice.
Everything else (Fritzing diagrams, images, documentation): Creative Commons BY-NC 4.0. You can share and adapt it for non-commercial purposes, crediting the author.
Third-party content (Icons8 icons, Edge Impulse SDK, third-party extensions) keeps its own license: see the LICENSE file.

