# Chapter 10 – Tic Tac Toe

Files for Chapter 10 of the book *App Inventor & ESP32*.

## Arduino sketches

| Sketch | Description |
|---|---|
| [`TicTacToe`](arduino/TicTacToe/) | Part 1: Tic Tac Toe on the touch display, two players |
| [`TicTacToeWiFi`](arduino/TicTacToeWiFi/) | Part 2: Tic Tac Toe played against the app over Wi-Fi |

**Board**: ESP32 Dev Module (ESP32-WROOM-32)

## Arduino libraries

| Library | Author | How to install |
|---|---|---|
| TFT_eSPI | Bodmer | Library Manager (then configure `User_Setup.h`, see the chapter) |

The other libraries used (Wi-Fi, Bluetooth, web server, Preferences, etc.) come with the ESP32 board package and don't need to be installed.

## App Inventor

- [`TicTacToeWifi.aia`](app-inventor/TicTacToeWifi.aia)

To import it: on [ai2.appinventor.mit.edu](https://ai2.appinventor.mit.edu) choose **Projects → Import project (.aia) from my computer**.

## Circuit diagram

- [`TicTacToe.fzz`](fritzing/TicTacToe.fzz) — image: [`TicTacToe_bb.png`](fritzing/TicTacToe_bb.png)

## Notes

- Before uploading `TicTacToeWiFi`, enter your Wi-Fi network name and password in the `WIFI_SSID` and `WIFI_PASSWORD` constants at the top of the sketch.
