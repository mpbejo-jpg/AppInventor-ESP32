/*
  Chapter 10 - Tic Tac Toe (Part 2: Wi-Fi)
  Tic Tac Toe between the touch display (always X) and the App Inventor
  app (always O). The ESP32 connects to your router and exposes the web
  endpoints /state, /move and /reset.
  Set WIFI_SSID and WIFI_PASSWORD below before uploading.
  Board:     ESP32 Dev Module
  Hardware:  ILI9341 2.8" SPI touch display (pins set in TFT_eSPI
             User_Setup.h), backlight GPIO 15
  Libraries: TFT_eSPI
*/
#include <WiFi.h>
#include <WebServer.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);

// ---------------- Backlight control ----------------
// Manually driven (not via TFT_eSPI/TFT_BL) so it stays OFF
// during WiFi + display init, and turns ON only when ready.
const int BACKLIGHT_PIN = 15;

// ---------------- WiFi settings (Station mode, connects to your router) ----------------
const char* WIFI_SSID     = "YourSSID";
const char* WIFI_PASSWORD = "YourPassword";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000; // give up after 15 seconds

// ---------------- Touch calibration ----------------
// Values obtained from the TFT_eSPI "Touch_calibrate" example
uint16_t calData[5] = {  459, 3291, 510, 3056, 7 };

// ---------------- Grid geometry ----------------
const int GRID_SIZE = 240;
const int CELL_SIZE = GRID_SIZE / 3;
const int OFFSET_X  = (320 - GRID_SIZE) / 2;
const int OFFSET_Y  = 0;

// ---------------- Colors ----------------
#define COLOR_BG    TFT_BLACK
#define COLOR_LINE  TFT_WHITE
#define COLOR_X     TFT_BLUE
#define COLOR_O     TFT_RED
#define COLOR_TEXT  TFT_WHITE

// ---------------- Game state ----------------
// Fixed roles: the local touch display always plays X,
// the App Inventor app always plays O.
char board[3][3];
char currentPlayer;   // 'X' or 'O'
char gameStatus;      // 'P' playing, 'X' or 'O' win, 'D' draw
bool wasTouched;

void setup() {
  Serial.begin(115200);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, LOW); // keep screen dark during init

  // Start the WiFi connection attempt before the display init,
  // to avoid a brownout on the 3.3V rail while tft.init() runs
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  delay(500); // let the 3.3V rail stabilize after the WiFi radio turns on

  tft.init();
  tft.setRotation(1);
  tft.setTouch(calData);

  digitalWrite(BACKLIGHT_PIN, HIGH); // display is ready, turn the screen on now
  showConnectingScreen();

  bool connected = waitForWiFiConnection();

  if (connected) {
    setupWebServer();
    showConnectedScreen();
    delay(5000); // leave the IP address visible for five seconds
  } else {
    showWiFiErrorScreen();
    Serial.println("WiFi connection failed. Check SSID/password and retry.");
  }

  resetGame();
  drawGrid();

  Serial.println("Tic Tac Toe (WiFi) ready. X starts on the display.");
}

void loop() {
  server.handleClient();

  uint16_t x, y;
  bool touched = tft.getTouch(&x, &y);

  if (touched && !wasTouched) {
    handleTouch(x, y);
  }
  wasTouched = touched;
}

// ============================================================
// WiFi setup (Station mode, DHCP)
// ============================================================

// Waits for the connection to complete, printing progress dots.
// Returns true if connected before the timeout, false otherwise.
bool waitForWiFiConnection() {
  unsigned long startTime = millis();

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println();
      Serial.println("Connection timed out.");
      return false;
    }
    Serial.print(".");
    delay(300);
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  return true;
}

void setupWebServer() {
  server.on("/state", HTTP_GET, handleGetState);
  server.on("/move", HTTP_GET, handleGetMove);
  server.on("/reset", HTTP_GET, handleGetReset);
  server.begin();
  Serial.println("Web server started");
}

// ============================================================
// Web handlers
// ============================================================

// GET /state -> {"board":"X O  O X  ","turn":"X","status":"P"}
void handleGetState() {
  server.send(200, "application/json", buildStateJson());
}

// GET /move?cell=0-8&player=X|O
void handleGetMove() {
  if (!server.hasArg("cell") || !server.hasArg("player")) {
    server.send(400, "application/json", "{\"error\":\"missing parameters\"}");
    return;
  }

  int cell = server.arg("cell").toInt();
  String playerArg = server.arg("player");

  if (playerArg.length() != 1 || (playerArg.charAt(0) != 'X' && playerArg.charAt(0) != 'O')) {
    server.send(400, "application/json", "{\"error\":\"invalid player\"}");
    return;
  }

  char player = playerArg.charAt(0);

  if (cell < 0 || cell > 8) {
    server.send(400, "application/json", "{\"error\":\"invalid cell\"}");
    return;
  }

  int row = cell / 3;
  int col = cell % 3;

  bool success = makeMove(row, col, player);

  if (!success) {
    server.send(409, "application/json", buildStateJson()); // conflict: not your turn / cell taken / game over
    return;
  }

  Serial.print("Move from app: player ");
  Serial.print(player);
  Serial.print(" at cell ");
  Serial.println(cell);

  server.send(200, "application/json", buildStateJson());
}

// GET /reset
void handleGetReset() {
  Serial.println("Reset requested from app.");
  resetGame();
  drawGrid();
  server.send(200, "application/json", buildStateJson());
}

String buildStateJson() {
  String boardString = "";
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      boardString += board[r][c];
    }
  }

  String statusString;
  switch (gameStatus) {
    case 'X': statusString = "WIN_X"; break;
    case 'O': statusString = "WIN_O"; break;
    case 'D': statusString = "DRAW";  break;
    default:  statusString = "PLAYING"; break;
  }

  String json = "{";
  json += "\"board\":\"" + boardString + "\",";
  json += "\"turn\":\"" + String(currentPlayer) + "\",";
  json += "\"status\":\"" + statusString + "\"";
  json += "}";
  return json;
}

// ============================================================
// Game logic (shared by touch input and web input)
// ============================================================

void resetGame() {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      board[r][c] = ' ';
    }
  }
  currentPlayer = 'X';
  gameStatus = 'P';
}

// Returns true if the move was applied, false if rejected
bool makeMove(int row, int col, char player) {
  if (gameStatus != 'P') return false;          // game already over
  if (player != currentPlayer) return false;    // not this player's turn
  if (board[row][col] != ' ') return false;      // cell occupied

  board[row][col] = player;

  if (player == 'X') {
    drawX(row, col);
  } else {
    drawO(row, col);
  }

  char winner = checkWinner();

  if (winner != ' ') {
    gameStatus = winner; // 'X', 'O' or 'D'
    showResult(winner);
  } else {
    currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
  }

  return true;
}

char checkWinner() {
  for (int i = 0; i < 3; i++) {
    if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
      return board[i][0];
    }
    if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
      return board[0][i];
    }
  }

  if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
    return board[0][0];
  }
  if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
    return board[0][2];
  }

  bool full = true;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (board[r][c] == ' ') full = false;
    }
  }
  if (full) return 'D';

  return ' ';
}

// ============================================================
// Touch handling (local player, always X)
// ============================================================

void handleTouch(int x, int y) {
  if (gameStatus != 'P') {
    // Any touch after a win/draw clears the board and starts again
    Serial.println("Restarting game (local touch).");
    resetGame();
    drawGrid();
    return;
  }

  if (x < OFFSET_X || x >= OFFSET_X + GRID_SIZE ||
      y < OFFSET_Y || y >= OFFSET_Y + GRID_SIZE) {
    return;
  }

  int col = (x - OFFSET_X) / CELL_SIZE;
  int row = (y - OFFSET_Y) / CELL_SIZE;

  // The display can only play as X. If it is not X's turn,
  // the touch is simply ignored (it is the app's turn).
  makeMove(row, col, 'X');
}

// ============================================================
// WiFi status screens
// ============================================================

void showConnectingScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(40, 100);
  tft.print("Connecting to WiFi...");
  tft.setTextSize(1);
  tft.setCursor(40, 130);
  tft.print(WIFI_SSID);
}

void showConnectedScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(40, 90);
  tft.print("WiFi connected");
  tft.setTextSize(1);
  tft.setCursor(40, 120);
  tft.print("IP address:");
  tft.setTextSize(2);
  tft.setCursor(40, 140);
  tft.print(WiFi.localIP());
}

void showWiFiErrorScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(TFT_RED, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(30, 100);
  tft.print("WiFi connection");
  tft.setCursor(30, 125);
  tft.print("failed!");
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setCursor(30, 160);
  tft.print("Check SSID/password");
}

// ============================================================
// Drawing
// ============================================================

void drawGrid() {
  tft.fillScreen(COLOR_BG);

  for (int i = 1; i < 3; i++) {
    int x = OFFSET_X + i * CELL_SIZE;
    tft.drawLine(x, OFFSET_Y, x, OFFSET_Y + GRID_SIZE, COLOR_LINE);
    tft.drawLine(x + 1, OFFSET_Y, x + 1, OFFSET_Y + GRID_SIZE, COLOR_LINE);
  }

  for (int i = 1; i < 3; i++) {
    int y = OFFSET_Y + i * CELL_SIZE;
    tft.drawLine(OFFSET_X, y, OFFSET_X + GRID_SIZE, y, COLOR_LINE);
    tft.drawLine(OFFSET_X, y + 1, OFFSET_X + GRID_SIZE, y + 1, COLOR_LINE);
  }

  tft.drawRect(OFFSET_X, OFFSET_Y, GRID_SIZE, GRID_SIZE, COLOR_LINE);
}

void drawX(int row, int col) {
  int pad = 15;
  int x1 = OFFSET_X + col * CELL_SIZE + pad;
  int y1 = OFFSET_Y + row * CELL_SIZE + pad;
  int x2 = OFFSET_X + (col + 1) * CELL_SIZE - pad;
  int y2 = OFFSET_Y + (row + 1) * CELL_SIZE - pad;

  tft.drawLine(x1, y1, x2, y2, COLOR_X);
  tft.drawLine(x1 + 1, y1, x2 + 1, y2, COLOR_X);
  tft.drawLine(x1, y2, x2, y1, COLOR_X);
  tft.drawLine(x1 + 1, y2, x2 + 1, y1, COLOR_X);
}

void drawO(int row, int col) {
  int cx = OFFSET_X + col * CELL_SIZE + CELL_SIZE / 2;
  int cy = OFFSET_Y + row * CELL_SIZE + CELL_SIZE / 2;
  int radius = CELL_SIZE / 2 - 15;

  tft.drawCircle(cx, cy, radius, COLOR_O);
  tft.drawCircle(cx, cy, radius - 1, COLOR_O);
}

void showResult(char winner) {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextSize(3);

  String message;
  if (winner == 'D') {
    message = "DRAW!";
  } else {
    message = String(winner) + " WIN!";
  }

  int textWidth = message.length() * 18;
  int x = (320 - textWidth) / 2;
  int y = 240 / 2 - 12;

  tft.setCursor(x, y);
  tft.print(message);

  Serial.println(message);
  Serial.println("Touch the screen or call /reset to play again.");
}