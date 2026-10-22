// ============================================================
// Chapter 10 - Tic Tac Toe on ESP32 + TFT 2.8" SPI touch display
// Local version: two players share the same touch screen
// Library: TFT_eSPI (configure pins in User_Setup.h, not here)
// ============================================================

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// ---------------- Touch calibration ----------------
// Run the "Touch_calibrate" example included in the TFT_eSPI
// library once, then replace these 5 values with the ones it prints.
uint16_t calData[5] = { 459, 3291, 510, 3056, 7};

// ---------------- Grid geometry ----------------
// Square 3x3 grid, 240x240, centered horizontally on a 320x240 screen
const int GRID_SIZE   = 240;
const int CELL_SIZE   = GRID_SIZE / 3;   // 80 px
const int OFFSET_X    = (320 - GRID_SIZE) / 2; // 40 px
const int OFFSET_Y    = 0;

// ---------------- Colors ----------------
#define COLOR_BG     TFT_BLACK
#define COLOR_LINE   TFT_WHITE
#define COLOR_X      TFT_BLUE
#define COLOR_O      TFT_RED
#define COLOR_TEXT   TFT_WHITE

// ---------------- Game state ----------------
char board[3][3];       // ' ' empty, 'X' or 'O'
char currentPlayer;     // 'X' or 'O'
bool gameOver;
bool wasTouched;        // used to detect a fresh touch (edge), avoid repeated taps

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);   // landscape, 320x240 - adjust if your wiring differs
  tft.setTouch(calData);

  resetGame();
  drawGrid();

  Serial.println("Tic Tac Toe ready. X starts.");
}

void loop() {
  uint16_t x, y;
  bool touched = tft.getTouch(&x, &y);

  // Only react on the moment the finger touches down, not while held
  if (touched && !wasTouched) {
    handleTouch(x, y);
  }
  wasTouched = touched;
}

// ---------------- Game logic ----------------

void resetGame() {
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      board[r][c] = ' ';
    }
  }
  currentPlayer = 'X';
  gameOver = false;
}

void handleTouch(int x, int y) {
  if (gameOver) {
    // Any touch after a win/draw clears the board and starts again
    Serial.println("Restarting game.");
    resetGame();
    drawGrid();
    return;
  }

  // Ignore touches outside the grid area
  if (x < OFFSET_X || x >= OFFSET_X + GRID_SIZE ||
      y < OFFSET_Y || y >= OFFSET_Y + GRID_SIZE) {
    return;
  }

  int col = (x - OFFSET_X) / CELL_SIZE;
  int row = (y - OFFSET_Y) / CELL_SIZE;

  if (board[row][col] != ' ') {
    // Cell already occupied, do nothing
    return;
  }

  board[row][col] = currentPlayer;

  if (currentPlayer == 'X') {
    drawX(row, col);
  } else {
    drawO(row, col);
  }

  Serial.print("Player ");
  Serial.print(currentPlayer);
  Serial.print(" played at row ");
  Serial.print(row);
  Serial.print(", col ");
  Serial.println(col);

  char winner = checkWinner();

  if (winner != ' ') {
    gameOver = true;
    showResult(winner);
  } else {
    currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
  }
}

// Returns 'X', 'O', 'D' (draw), or ' ' (game continues)
char checkWinner() {
  // Rows and columns
  for (int i = 0; i < 3; i++) {
    if (board[i][0] != ' ' && board[i][0] == board[i][1] && board[i][1] == board[i][2]) {
      return board[i][0];
    }
    if (board[0][i] != ' ' && board[0][i] == board[1][i] && board[1][i] == board[2][i]) {
      return board[0][i];
    }
  }

  // Diagonals
  if (board[0][0] != ' ' && board[0][0] == board[1][1] && board[1][1] == board[2][2]) {
    return board[0][0];
  }
  if (board[0][2] != ' ' && board[0][2] == board[1][1] && board[1][1] == board[2][0]) {
    return board[0][2];
  }

  // Draw: no empty cells left
  bool full = true;
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 3; c++) {
      if (board[r][c] == ' ') full = false;
    }
  }
  if (full) return 'D';

  return ' ';
}

// ---------------- Drawing ----------------

void drawGrid() {
  tft.fillScreen(COLOR_BG);

  // Two vertical lines
  for (int i = 1; i < 3; i++) {
    int x = OFFSET_X + i * CELL_SIZE;
    tft.drawLine(x, OFFSET_Y, x, OFFSET_Y + GRID_SIZE, COLOR_LINE);
    tft.drawLine(x + 1, OFFSET_Y, x + 1, OFFSET_Y + GRID_SIZE, COLOR_LINE); // thicker line
  }

  // Two horizontal lines
  for (int i = 1; i < 3; i++) {
    int y = OFFSET_Y + i * CELL_SIZE;
    tft.drawLine(OFFSET_X, y, OFFSET_X + GRID_SIZE, y, COLOR_LINE);
    tft.drawLine(OFFSET_X, y + 1, OFFSET_X + GRID_SIZE, y + 1, COLOR_LINE); // thicker line
  }

  // Outer border
  tft.drawRect(OFFSET_X, OFFSET_Y, GRID_SIZE, GRID_SIZE, COLOR_LINE);
}

void drawX(int row, int col) {
  int pad = 15; // margin inside the cell
  int x1 = OFFSET_X + col * CELL_SIZE + pad;
  int y1 = OFFSET_Y + row * CELL_SIZE + pad;
  int x2 = OFFSET_X + (col + 1) * CELL_SIZE - pad;
  int y2 = OFFSET_Y + (row + 1) * CELL_SIZE - pad;

  // Draw each diagonal twice, offset by 1px, to make the stroke thicker
  tft.drawLine(x1, y1, x2, y2, COLOR_X);
  tft.drawLine(x1 + 1, y1, x2 + 1, y2, COLOR_X);
  tft.drawLine(x1, y2, x2, y1, COLOR_X);
  tft.drawLine(x1 + 1, y2, x2 + 1, y1, COLOR_X);
}

void drawO(int row, int col) {
  int cx = OFFSET_X + col * CELL_SIZE + CELL_SIZE / 2;
  int cy = OFFSET_Y + row * CELL_SIZE + CELL_SIZE / 2;
  int radius = CELL_SIZE / 2 - 15;

  // Draw two concentric circles to make the stroke thicker
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

  int textWidth = message.length() * 18; // approx width for text size 3
  int x = (320 - textWidth) / 2;
  int y = 240 / 2 - 12;

  tft.setCursor(x, y);
  tft.print(message);

  Serial.println(message);
  Serial.println("Touch the screen to play again.");
}
