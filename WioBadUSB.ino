// ======================================
// Wio BadUSB — Wio Terminal Firmware
// by GH0ST3CH
// ======================================
// HID injection for Wio Terminal
// ===============================================================
// Prompt engineering was used in the development of this firmware
// ===============================================================

// 1. C++ Headers
#include <map>
#include <vector>
#include <algorithm>

// 2. Undefine min/max before Arduino/library headers to prevent macro conflicts
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// 3. Arduino and Wio Terminal libraries
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Seeed_FS.h>
#include "SD/Seeed_SD.h"
#include "Keyboard.h"

// 4. Re-define min/max for Arduino compatibility AFTER all headers
#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif
#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

// ======= Wio Terminal Controls =======
#define BTN_A WIO_KEY_A   // Right button
#define BTN_B WIO_KEY_B   // Middle button
#define BTN_C WIO_KEY_C   // Left button

#define JOY_UP    WIO_5S_UP
#define JOY_DOWN  WIO_5S_DOWN
#define JOY_LEFT  WIO_5S_LEFT
#define JOY_RIGHT WIO_5S_RIGHT
#define JOY_OK    WIO_5S_PRESS

// ======= Display =======
TFT_eSPI tft;
static const int W = 320;
static const int H = 240;

// ======= Input State =======
struct Keys {
  uint16_t now = 0, last = 0;
  void update() { last = now; now = 0; }
  bool pressed(uint16_t m) const { return (now & m) && !(last & m); }
};
static Keys keys;

static inline void readInputs() {
  keys.update();
  if (digitalRead(BTN_A) == LOW) keys.now |= 0x0001;
  if (digitalRead(BTN_B) == LOW) keys.now |= 0x0002;
  if (digitalRead(BTN_C) == LOW) keys.now |= 0x0004;
  if (digitalRead(JOY_UP)    == LOW) keys.now |= 0x0010;
  if (digitalRead(JOY_DOWN)  == LOW) keys.now |= 0x0020;
  if (digitalRead(JOY_LEFT)  == LOW) keys.now |= 0x0040;
  if (digitalRead(JOY_RIGHT) == LOW) keys.now |= 0x0080;
  if (digitalRead(JOY_OK)    == LOW) keys.now |= 0x0100;
}

// ======= UI Helpers =======
static void drawSkinHeader(String title, String status = "", uint16_t statusColor = TFT_CYAN) {
  tft.fillRect(0, 0, W, 45, 0x2104);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(38, 17);
  tft.print(title);

  if (status != "") {
    tft.fillRoundRect(200, 10, 110, 25, 4, statusColor);
    tft.setTextColor(TFT_BLACK);
    tft.setTextSize(1);
    int textWidth = status.length() * 6;
    tft.setCursor(200 + (110 - textWidth) / 2, 18);
    tft.print(status);
  }
}

static void drawSkinFooter(String text) {
  tft.fillRect(0, 215, 320, 25, 0x1082);
  tft.setTextColor(TFT_LIGHTGREY);
  tft.setTextSize(1);
  // Center footer text
  int textWidth = text.length() * 6;
  int x = (W - textWidth) / 2;
  if (x < 0) x = 0;
  tft.setCursor(x, 223);
  tft.print(text);
}

// ======= Boot Logo =======
static void drawBootLogo() {
  tft.fillScreen(TFT_BLACK);
  for (int i = 0; i < W; i += 15) tft.drawLine(i, 0, W - i, H, 0x2104);

  tft.setTextSize(4);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(48, 80);  tft.print("Wio BadUSB");

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(95, 165);
  tft.print("by GH0ST3CH");

  tft.drawRect(45, 190, 230, 10, TFT_WHITE);
  for (int i = 0; i < 226; i++) {
    tft.fillRect(47, 192, i, 6, TFT_CYAN);
    if (i % 40 == 0) delay(10);
  }
  delay(3000);
  tft.fillScreen(TFT_BLACK);
}

// =====================================================================
// KEY LOOKUP TABLE
// Maps Rubber Ducky token strings → Arduino Keyboard key codes.
// Covers every key the Arduino Keyboard library exposes.
// Single printable chars are handled separately (see bu_lookupKey).
// =====================================================================

// Sentinel for "not found / not a special key"
#define KEY_NOT_FOUND 0x00

static uint8_t bu_lookupKey(const String& token) {
  // ---- Single-character printable tokens ----
  if (token.length() == 1) {
    char c = token[0];
    // Printable ASCII that Keyboard.press() accepts directly
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        (c == ' ' || c == '!' || c == '"' || c == '#' || c == '$' ||
         c == '%' || c == '&' || c == '\'' || c == '(' || c == ')' ||
         c == '*' || c == '+' || c == ',' || c == '-' || c == '.' ||
         c == '/' || c == ':' || c == ';' || c == '<' || c == '=' ||
         c == '>' || c == '?' || c == '@' || c == '[' || c == '\\' ||
         c == ']' || c == '^' || c == '_' || c == '`' || c == '{' ||
         c == '|' || c == '}' || c == '~')) {
      return (uint8_t)c;
    }
  }

  // ---- Named key tokens (upper-cased before lookup) ----
  String t = token;
  t.toUpperCase();

  // Modifiers
  if (t == "CTRL"   || t == "CONTROL") return KEY_LEFT_CTRL;
  if (t == "RCTRL"  || t == "RIGHTCTRL" || t == "RIGHTCONTROL") return KEY_RIGHT_CTRL;
  if (t == "SHIFT")                    return KEY_LEFT_SHIFT;
  if (t == "RSHIFT" || t == "RIGHTSHIFT") return KEY_RIGHT_SHIFT;
  if (t == "ALT")                      return KEY_LEFT_ALT;
  if (t == "RALT"   || t == "RIGHTALT" || t == "ALTGR") return KEY_RIGHT_ALT;
  if (t == "GUI"    || t == "WINDOWS"  || t == "COMMAND" || t == "META") return KEY_LEFT_GUI;
  if (t == "RGUI"   || t == "RIGHTGUI" || t == "RIGHTWINDOWS") return KEY_RIGHT_GUI;

  // Navigation & editing
  if (t == "ENTER"  || t == "RETURN")  return KEY_RETURN;
  if (t == "TAB")                      return KEY_TAB;
  if (t == "SPACE")                    return ' ';
  if (t == "BACKSPACE" || t == "BKSP") return KEY_BACKSPACE;
  if (t == "DELETE" || t == "DEL")     return KEY_DELETE;
  if (t == "INSERT" || t == "INS")     return KEY_INSERT;
  if (t == "HOME")                     return KEY_HOME;
  if (t == "END")                      return KEY_END;
  if (t == "PAGEUP"   || t == "PAGE_UP"   || t == "PGUP") return KEY_PAGE_UP;
  if (t == "PAGEDOWN" || t == "PAGE_DOWN" || t == "PGDN") return KEY_PAGE_DOWN;
  if (t == "ESCAPE"   || t == "ESC")   return KEY_ESC;

  // Arrow keys
  if (t == "UP"    || t == "UPARROW")    return KEY_UP_ARROW;
  if (t == "DOWN"  || t == "DOWNARROW")  return KEY_DOWN_ARROW;
  if (t == "LEFT"  || t == "LEFTARROW")  return KEY_LEFT_ARROW;
  if (t == "RIGHT" || t == "RIGHTARROW") return KEY_RIGHT_ARROW;

  // Lock keys
  if (t == "CAPSLOCK"   || t == "CAPS_LOCK")   return KEY_CAPS_LOCK;
  if (t == "NUMLOCK"    || t == "NUM_LOCK")     return KEY_NUM_LOCK;
  if (t == "SCROLLLOCK" || t == "SCROLL_LOCK")  return KEY_SCROLL_LOCK;

  // System keys
  if (t == "PRINTSCREEN" || t == "PRINT_SCREEN" || t == "PRTSC") return KEY_PRINT_SCREEN;
  if (t == "PAUSE" || t == "BREAK")              return KEY_PAUSE;
  if (t == "MENU"  || t == "APP" || t == "CONTEXT_MENU") return KEY_MENU;

  // Function keys F1–F24
  if (t == "F1")  return KEY_F1;
  if (t == "F2")  return KEY_F2;
  if (t == "F3")  return KEY_F3;
  if (t == "F4")  return KEY_F4;
  if (t == "F5")  return KEY_F5;
  if (t == "F6")  return KEY_F6;
  if (t == "F7")  return KEY_F7;
  if (t == "F8")  return KEY_F8;
  if (t == "F9")  return KEY_F9;
  if (t == "F10") return KEY_F10;
  if (t == "F11") return KEY_F11;
  if (t == "F12") return KEY_F12;
  if (t == "F13") return KEY_F13;
  if (t == "F14") return KEY_F14;
  if (t == "F15") return KEY_F15;
  if (t == "F16") return KEY_F16;
  if (t == "F17") return KEY_F17;
  if (t == "F18") return KEY_F18;
  if (t == "F19") return KEY_F19;
  if (t == "F20") return KEY_F20;
  if (t == "F21") return KEY_F21;
  if (t == "F22") return KEY_F22;
  if (t == "F23") return KEY_F23;
  if (t == "F24") return KEY_F24;

  // Numpad keys
  if (t == "NUM0"    || t == "KP0")      return KEY_KP_0;
  if (t == "NUM1"    || t == "KP1")      return KEY_KP_1;
  if (t == "NUM2"    || t == "KP2")      return KEY_KP_2;
  if (t == "NUM3"    || t == "KP3")      return KEY_KP_3;
  if (t == "NUM4"    || t == "KP4")      return KEY_KP_4;
  if (t == "NUM5"    || t == "KP5")      return KEY_KP_5;
  if (t == "NUM6"    || t == "KP6")      return KEY_KP_6;
  if (t == "NUM7"    || t == "KP7")      return KEY_KP_7;
  if (t == "NUM8"    || t == "KP8")      return KEY_KP_8;
  if (t == "NUM9"    || t == "KP9")      return KEY_KP_9;
  if (t == "NUMENTER"|| t == "KP_ENTER") return KEY_KP_ENTER;
  if (t == "NUMPLUS" || t == "KP_PLUS")  return KEY_KP_PLUS;
  if (t == "NUMMINUS"|| t == "KP_MINUS") return KEY_KP_MINUS;
  if (t == "NUMDIV"  || t == "KP_DIV")   return KEY_KP_SLASH;
  if (t == "NUMDOT"  || t == "KP_DOT")   return KEY_KP_DOT;

  return KEY_NOT_FOUND;
}

// =====================================================================
// Parse a space-delimited token list and press all keys simultaneously,
// then release all. This gives unlimited chord support.
// Returns false if no valid keys were found.
// =====================================================================
static bool bu_pressChord(const String& tokenLine) {
  // Split by spaces
  std::vector<uint8_t> codes;
  String remaining = tokenLine;
  remaining.trim();

  while (remaining.length() > 0) {
    int sp = remaining.indexOf(' ');
    String token;
    if (sp == -1) {
      token = remaining;
      remaining = "";
    } else {
      token = remaining.substring(0, sp);
      remaining = remaining.substring(sp + 1);
      remaining.trim();
    }
    token.trim();
    if (token.length() == 0) continue;

    uint8_t code = bu_lookupKey(token);
    if (code != KEY_NOT_FOUND) {
      codes.push_back(code);
    }
  }

  if (codes.empty()) return false;

  for (uint8_t k : codes) Keyboard.press(k);
  delay(50);
  Keyboard.releaseAll();
  return true;
}

// ============
// BADUSB STATE
// ============
static bool   bu_sd = false;
static String bu_dir = "/";
static String bu_f[15];
static bool   bu_isDir[15];
static int    bu_c = 0, bu_s = 0;
static int    bu_scroll = 0;

static String bu_status = "READY";
static String bu_msg = "";
static bool   bu_msgIsError = false;

static void bu_setStatus(const String& st, const String& msg = "") {
  bu_status = st;
  bu_msg = msg;
  bu_msgIsError = (st == "ERROR");
}

// Word-wrapping for size=1 text
static void bu_printWrapped(int x, int y, int maxW, const String& s, int maxLines) {
  const int approxChars = max(10, maxW / 6);
  int lineCount = 0;
  int i = 0;
  while (i < (int)s.length() && lineCount < maxLines) {
    int end = min(i + approxChars, (int)s.length());
    int space = s.lastIndexOf(' ', end);
    if (space <= i) space = end;
    String line = s.substring(i, space);
    line.trim();
    tft.setCursor(x, y + lineCount * 10);
    tft.print(line);
    i = space;
    while (i < (int)s.length() && s[i] == ' ') i++;
    lineCount++;
  }
}

static String bu_parentDir(const String& path) {
  if (path == "/" || path.length() == 0) return "/";
  String p = path;
  if (p.endsWith("/") && p.length() > 1) p.remove(p.length() - 1);
  int last = p.lastIndexOf('/');
  if (last <= 0) return "/";
  p = p.substring(0, last);
  if (p.length() == 0) p = "/";
  return p;
}

static bool bu_loadDir(const String& path, String* errOut = nullptr) {
  bu_c = 0;
  File r = SD.open(path.c_str());
  if (!r) {
    if (errOut) *errOut = "Can\x27t open folder: " + path;
    return false;
  }
  if (path != "/") {
    bu_f[bu_c] = "Back";
    bu_isDir[bu_c] = true;
    bu_c++;
  }
  while (bu_c < 15) {
    File e = r.openNextFile();
    if (!e) break;
    String name = String(e.name());
    int pfx = name.indexOf("://");
    if (pfx != -1) name = name.substring(pfx + 3);
    if (name.startsWith("/")) name = name.substring(1);
    int ls = name.lastIndexOf("/");
    if (ls != -1) name = name.substring(ls + 1);
    if (name == "." || name == "Back") { e.close(); continue; }

    if (e.isDirectory()) {
      bu_f[bu_c] = name;
      bu_isDir[bu_c] = true;
      bu_c++;
    } else if (name.endsWith(".txt")) {
      bu_f[bu_c] = name;
      bu_isDir[bu_c] = false;
      bu_c++;
    }
    e.close();
  }
  r.close();
  return true;
}

static void bu_draw(const String& stOverride = "", const String& msgOverride = "") {
  if (stOverride.length()) bu_setStatus(stOverride, msgOverride);

  uint16_t col = (bu_status == "READY")   ? TFT_CYAN
               : (bu_status == "RUNNING") ? TFT_YELLOW
               : (bu_status == "ERROR")   ? TFT_RED
               : TFT_GREEN;

  drawSkinHeader("Wio BadUSB", bu_status, col);
  tft.fillRect(0, 45, W, 170, TFT_BLACK);

  tft.setTextSize(2); tft.setTextColor(TFT_WHITE);
  tft.setCursor(15, 60); tft.print("SELECT PAYLOAD:");

  int visible = 5;
  int start   = bu_scroll;
  int end     = min(start + visible, bu_c);

  for (int row = 0; row < (end - start); row++) {
    int i = start + row;
    int y = 90 + row * 22;
    if (i == bu_s) {
      tft.fillRoundRect(10, y - 4, 300, 20, 4, 0x4208);
      tft.setTextColor(TFT_YELLOW);
    } else {
      tft.setTextColor(TFT_WHITE);
    }
    tft.setTextSize(1); tft.setCursor(25, y);
    if (bu_isDir[i]) tft.print("> ");
    else             tft.print("");
    tft.print(bu_f[i]);
  }

  if (bu_msg.length()) {
    tft.setTextSize(1);
    tft.setTextColor(bu_msgIsError ? TFT_RED : TFT_LIGHTGREY);
    tft.fillRect(10, 198, W - 20, 16, TFT_BLACK);
    bu_printWrapped(12, 200, W - 24, bu_msg, 1);
  }

  drawSkinFooter("U/D:Navigate up/down   OK:Open/Run   LB:Back");
}

// =====================================================================
// PAYLOAD RUNNER — Full Rubber Ducky syntax
//
// Supported commands:
//   REM <text>             — Comment, ignored
//   STRING <text>          — Type the text literally
//   STRINGLN <text>        — Type the text and press Enter
//   DELAY <ms>             — Wait ms milliseconds
//   DEFAULTDELAY <ms>      — Set inter-command delay (alias: DEFAULT_DELAY)
//   REPEAT <n>             — Repeat the previous command n times
//   <KEY> [KEY KEY ...]    — Press all listed keys simultaneously (chord)
//                            Any combination of named keys and single chars.
//
// Key names (case-insensitive):
//   Modifiers : CTRL/CONTROL, SHIFT, ALT, GUI/WINDOWS/COMMAND/META
//               RCTRL, RSHIFT, RALT/ALTGR, RGUI
//   Nav/Edit  : ENTER/RETURN, TAB, SPACE, BACKSPACE, DELETE, INSERT,
//               HOME, END, PAGEUP, PAGEDOWN, ESCAPE/ESC
//   Arrows    : UP, DOWN, LEFT, RIGHT
//               (also: UPARROW, DOWNARROW, LEFTARROW, RIGHTARROW)
//   Locks     : CAPSLOCK, NUMLOCK, SCROLLLOCK
//   System    : PRINTSCREEN, PAUSE/BREAK, MENU/APP
//   Function  : F1–F24
//   Numpad    : NUM0–NUM9, NUMENTER, NUMPLUS, NUMMINUS, NUMMULT, NUMDIV, NUMDOT
//               (also: KP0–KP9, KP_ENTER, KP_PLUS, KP_MINUS, KP_MULT, KP_DIV, KP_DOT)
//   Single chars: a-z A-Z 0-9 and all standard printable symbols
//
// Chord examples in a payload file:
//   CONTROL ALT DELETE
//   GUI r
//   GUI ALT m h
//   SHIFT F10
//   CONTROL SHIFT ESCAPE
//   ALT F4
// =====================================================================

static void bu_runFile(const String& fullPath) {
  File f = SD.open(fullPath.c_str());
  if (!f) { bu_draw("ERROR", "Can\x27t open file: " + fullPath); return; }

  int  lc          = 0;       // line counter (for display)
  int  defaultDelay = 0;      // inter-command delay in ms
  String lastCmd   = "";      // for REPEAT
  String lastArg   = "";

  // Lambda-style helper — execute a single parsed command.
  // We encapsulate in a local struct so we can call it from REPEAT too.
  // (C++11 lambdas work on the Wio/SAMD toolchain.)
  auto execCmd = [&](String cu, String a) {
    // cu is already upper-cased command token; a is the rest of the line

    if (cu == "STRING") {
      Keyboard.print(a);

    } else if (cu == "STRINGLN") {
      Keyboard.print(a);
      Keyboard.write(KEY_RETURN);

    } else if (cu == "DELAY") {
      delay((uint32_t)a.toInt());

    } else if (cu == "DEFAULTDELAY" || cu == "DEFAULT_DELAY") {
      defaultDelay = a.toInt();

    } else {
      // Chord: first token is cu, remaining tokens are in a.
      // Re-join them so bu_pressChord can split uniformly.
      String chord = cu;
      if (a.length() > 0) {
        chord += " ";
        chord += a;
      }
      if (!bu_pressChord(chord)) {
        bu_draw("ERROR", "Unknown key (line " + String(lc) + "): " + cu);
        bu_status = "RUNNING";
      }
    }
  };

  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l == "" || l.startsWith("REM") || l.startsWith("//")) continue;

    int    sp = l.indexOf(' ');
    String cu = (sp == -1) ? l       : l.substring(0, sp);
    String a  = (sp == -1) ? ""      : l.substring(sp + 1);
    a.trim();

    String cuUp = cu;
    cuUp.toUpperCase();

    bu_draw("RUNNING", "Line " + String(++lc) + ": " + cu);

    if (cuUp == "REPEAT") {
      int n = a.toInt();
      if (n < 1) n = 1;
      for (int r = 0; r < n; r++) {
        execCmd(lastCmd, lastArg);
        if (defaultDelay > 0) delay(defaultDelay);
      }
      // Do NOT update lastCmd/lastArg for REPEAT itself
      continue;
    }

    execCmd(cuUp, a);

    // Store for potential REPEAT
    lastCmd = cuUp;
    lastArg = a;

    if (defaultDelay > 0) delay(defaultDelay);
  }

  f.close();
  bu_draw("DONE");
  delay(1500);
  bu_draw("READY", "");
}

// =====================================================================
// APP ENTRY & LOOP
// =====================================================================
static void bu_enter() {
  Keyboard.begin();
  bu_setStatus("READY", "");
  if (SD.begin(SDCARD_SS_PIN, SDCARD_SPI)) {
    bu_sd  = true;
    bu_dir = "/";
    String err;
    if (!bu_loadDir(bu_dir, &err)) {
      bu_s = 0; bu_scroll = 0;
      bu_draw("ERROR", err);
      return;
    }
  } else {
    bu_sd = false;
  }
  bu_s = 0; bu_scroll = 0;
  if (!bu_sd) bu_draw("ERROR", "SD card not detected. Insert SD card and restart.");
  else        bu_draw("READY", "");
}

static void bu_loop() {
  int visible = 5;

  if (keys.pressed(0x0010)) {  // JOY_UP
    if (bu_s > 0) bu_s--;
    if (bu_s < bu_scroll) bu_scroll = bu_s;
    bu_draw();
  }
  if (keys.pressed(0x0020)) {  // JOY_DOWN
    if (bu_s < bu_c - 1) bu_s++;
    if (bu_s >= bu_scroll + visible) bu_scroll = bu_s - (visible - 1);
    bu_draw();
  }

  // BTN_C (Left button): back up one folder or exit to… nothing (we're standalone)
  if (keys.pressed(0x0004)) {
    if (!bu_sd) return;
    if (bu_dir != "/") {
      bu_dir = bu_parentDir(bu_dir);
      String err;
      if (!bu_loadDir(bu_dir, &err)) { bu_draw("ERROR", err); return; }
      bu_s = 0; bu_scroll = 0;
      bu_draw();
    }
    // At root, LB does nothing — there's nowhere to exit to in standalone mode
  }

  // JOY_OK: open directory or run payload
  if (keys.pressed(0x0100) && bu_sd && bu_c > 0) {
    String name = bu_f[bu_s];

    if (bu_isDir[bu_s]) {
      if (name == "Back") {
        bu_dir = bu_parentDir(bu_dir);
      } else {
        bu_dir = (bu_dir == "/") ? "/" + name : bu_dir + "/" + name;
      }
      String err;
      if (!bu_loadDir(bu_dir, &err)) { bu_draw("ERROR", err); return; }
      bu_s = 0; bu_scroll = 0;
      bu_draw();
    } else {
      String fullPath;
      if (bu_dir == "/") fullPath = "/" + name;
      else               fullPath = bu_dir + "/" + name;

      // Normalize any SdFat-style prefixes
      int pfx = fullPath.indexOf("://");
      if (pfx != -1) fullPath = fullPath.substring(pfx + 3);
      if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;

      bu_runFile(fullPath);
    }
  }
}

// =====================================================================
// ARDUINO ENTRY POINTS
// =====================================================================
void setup() {
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_C, INPUT_PULLUP);
  pinMode(JOY_UP,    INPUT_PULLUP);
  pinMode(JOY_DOWN,  INPUT_PULLUP);
  pinMode(JOY_LEFT,  INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_OK,    INPUT_PULLUP);

  Serial.begin(115200);
  tft.begin();
  tft.setRotation(3);

  drawBootLogo();
  bu_enter();
}

void loop() {
  readInputs();
  bu_loop();
}
