#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BleKeyboardClient.h>
#include "HidKeymap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// =====================================================
// AR CLOUDX V3.2 - integrated prototype
// ESP32 DevKit V1 + 128x64 SSD1306 OLED
// Libraries: Adafruit GFX, Adafruit SSD1306,
// ArduinoJson 7, NimBLE-Arduino, ESP32-BLE-Keyboard-Client
// =====================================================

#define OLED_SDA 21
#define OLED_SCL 22
#define JOY_X 35
#define JOY_Y 34
#define TOUCH_PIN 2
#define IR_LED_PIN 4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define LOW_LIMIT 1200
#define HIGH_LIMIT 2800

// Existing AR CLOUDX Cloudflare Worker endpoint
const char* AI_URL = "https://ar-cloudx.namrata4a21.workers.dev/chat";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BleKeyboardClient hid;
QueueHandle_t keyQueue = nullptr;

enum ScreenState {
HOME, APP_SCREEN, SETTINGS_SCREEN, WIFI_LIST_SCREEN,
WIFI_PASSWORD_SCREEN, WIFI_STATUS_SCREEN, BLUETOOTH_SCREEN,
AI_SCREEN, IR_SCREEN, NOTEPAD_SCREEN, ABOUT_SCREEN
};
ScreenState currentScreen = HOME;

const char* appNames[] = {"AI MODE", "IR REMOTE", "SETTINGS", "NOTEPAD", "ABOUT OS"};
const int APP_COUNT = 5;
int selectedApp = 0;
int settingsSelection = 0;
int selectedNetwork = 0;

String networks[15];
int networkCount = -1;
String selectedSSID = "";
char wifiPassword[65] = "";
int passwordLength = 0;

bool previousTouch = false;
bool keyboardConnected = false;
uint8_t currentModifiers = 0;
bool capsLockActive = false;

unsigned long lastInputTime = 0;
unsigned long lastTouchTime = 0;
const unsigned long INPUT_DELAY = 180;
const unsigned long TOUCH_DELAY = 250;

char noteText[1200] = "";
int noteLength = 0;
int noteScrollLine = 0;
int noteTotalLines = 1;

String aiInput = "";
String aiReply = "";
String aiStatus = "Connect Wi-Fi first";
String chatUser[4];
String chatReply[4];
int chatCount = 0;
int aiScrollLine = 0;
bool aiBusy = false;

// ---------------- BLE keyboard callbacks ----------------
void on_key_pressed(bool is_modifier, uint8_t key) {
if (is_modifier) {
currentModifiers |= key;
return;
}

if (key == HID_KEY_CAPSLOCK) {
capsLockActive = !capsLockActive;
return;
}

char c = 0;
if (key == HID_KEY_ENTER) c = '\r';
else if (key == HID_KEY_BACKSPACE) c = '\b';
else if (key == HID_KEY_SPACE) c = ' ';
else {
bool shift = (currentModifiers & 0x22) != 0;
c = hidToAscii(key, shift, capsLockActive);
}

if (c && keyQueue) xQueueSend(keyQueue, &c, 0);
}

void on_key_released(bool is_modifier, uint8_t key) {
if (is_modifier) currentModifiers &= ~key;
}

// ---------------- Display helpers ----------------
void drawHeader(const char* title) {
display.clearDisplay();
display.setTextColor(SSD1306_WHITE);
display.setTextSize(1);
display.setCursor(0, 0);
display.print(title);
display.drawLine(0, 11, 127, 11, SSD1306_WHITE);
}

void drawFooter(const char* text) {
display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
display.setCursor(2, 57);
display.print(text);
}

void drawCentered(const char* text, int y) {
int16_t x1, y1;
uint16_t w, h;
display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
display.setCursor((128 - w) / 2, y);
display.print(text);
}

void drawAppIcon(int index, int x, int y, bool selected) {
int radius = selected ? 17 : 13;
display.drawCircle(x, y, radius, SSD1306_WHITE);
if (selected) display.drawCircle(x, y, radius + 3, SSD1306_WHITE);

switch (index) {
case 0:
display.drawCircle(x, y, 5, SSD1306_WHITE);
display.drawLine(x - 8, y, x + 8, y, SSD1306_WHITE);
display.drawLine(x, y - 8, x, y + 8, SSD1306_WHITE);
break;
case 1:
display.drawCircle(x, y, 4, SSD1306_WHITE);
display.drawLine(x - 9, y - 8, x - 5, y - 4, SSD1306_WHITE);
display.drawLine(x + 9, y - 8, x + 5, y - 4, SSD1306_WHITE);
display.drawLine(x - 9, y + 8, x - 5, y + 4, SSD1306_WHITE);
display.drawLine(x + 9, y + 8, x + 5, y + 4, SSD1306_WHITE);
break;
case 2:
display.drawCircle(x, y, 7, SSD1306_WHITE);
display.drawCircle(x, y, 2, SSD1306_WHITE);
break;
case 3:
display.drawRoundRect(x - 7, y - 9, 14, 18, 2, SSD1306_WHITE);
display.drawLine(x - 4, y - 3, x + 4, y - 3, SSD1306_WHITE);
display.drawLine(x - 4, y + 1, x + 4, y + 1, SSD1306_WHITE);
break;
case 4:
display.drawCircle(x, y, 8, SSD1306_WHITE);
display.setCursor(x - 2, y - 5);
display.print("i");
break;
}
}

void drawHome() {
display.clearDisplay();
display.setTextColor(SSD1306_WHITE);
display.setTextSize(1);
display.setCursor(2, 2); display.print("AR CLOUDX");
display.setCursor(91, 2); display.print("V3.2");
display.drawLine(0, 12, 127, 12, SSD1306_WHITE);

int left = (selectedApp + APP_COUNT - 1) % APP_COUNT;
int right = (selectedApp + 1) % APP_COUNT;
drawAppIcon(left, 20, 34, false);
drawAppIcon(selectedApp, 64, 34, true);
drawAppIcon(right, 108, 34, false);
drawCentered(appNames[selectedApp], 55);
display.display();
}

void drawSettings() {
drawHeader("SETTINGS");
const char* items[] = {"Wi-Fi", "Bluetooth", "System Info"};
for (int i = 0; i < 3; i++) {
int y = 17 + i * 12;
if (i == settingsSelection) {
display.fillRoundRect(2, y - 1, 124, 11, 2, SSD1306_WHITE);
display.setTextColor(SSD1306_BLACK);
} else display.setTextColor(SSD1306_WHITE);
display.setCursor(7, y); display.print(items[i]);
}
display.setTextColor(SSD1306_WHITE);
drawFooter("Y: Move Tap: Select");
display.display();
}

// ---------------- Wi-Fi ----------------
void startWiFiScan() {
drawHeader("WI-FI SCAN");
display.setCursor(4, 25); display.print("Scanning networks...");
display.display();

WiFi.mode(WIFI_STA);
WiFi.disconnect();
WiFi.scanDelete();
networkCount = -1;
WiFi.scanNetworks(true, false);
currentScreen = WIFI_LIST_SCREEN;
}

void drawWiFiList() {
drawHeader("WI-FI NETWORKS");
if (networkCount == -1) {
display.setCursor(4, 25); display.print("Scanning...");
} else if (networkCount == 0) {
display.setCursor(4, 25); display.print("No networks found");
} else {
int first = selectedNetwork - 1;
if (first < 0) first = 0;
if (first > networkCount - 4) first = max(0, networkCount - 4);
for (int i = first; i < networkCount && i < first + 4; i++) {
int y = 15 + (i - first) * 10;
if (i == selectedNetwork) {
display.fillRect(1, y - 1, 126, 10, SSD1306_WHITE);
display.setTextColor(SSD1306_BLACK);
} else display.setTextColor(SSD1306_WHITE);
display.setCursor(4, y);
String n = networks[i];
if (n.length() > 19) n = n.substring(0, 19);
display.print(n);
}
}
display.setTextColor(SSD1306_WHITE);
drawFooter("Y: Move Tap: Select");
display.display();
}

void updateWiFiScan() {
if (networkCount != -1) return;
int result = WiFi.scanComplete();
if (result == WIFI_SCAN_RUNNING) return;
if (result < 0) networkCount = 0;
else {
networkCount = min(result, 15);
for (int i = 0; i < networkCount; i++) {
networks[i] = WiFi.SSID(i);
if (!networks[i].length()) networks[i] = "";
}
}
selectedNetwork = 0;
drawWiFiList();
}

void drawPasswordScreen() {
drawHeader("WI-FI PASSWORD");
display.setCursor(3, 16); display.print("Network:");
display.setCursor(3, 26); display.print(selectedSSID.substring(0, 20));
display.setCursor(3, 38); display.print("Password:");
display.setCursor(3, 47);
// Visible while typing, as requested
for (int i = 0; i < passwordLength && i < 20; i++) display.print(wifiPassword[i]);
display.setCursor(3, 57); display.print("Type + Enter");
display.display();
}

void drawWiFiStatus() {
drawHeader("WI-FI STATUS");
display.setCursor(4, 19); display.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Connection failed");
if (WiFi.status() == WL_CONNECTED) {
display.setCursor(4, 33); display.println("IP Address:");
display.setCursor(4, 44); display.println(WiFi.localIP());
} else {
display.setCursor(4, 35); display.println("Check password");
display.println("and try again");
}
drawFooter("Tap: Back");
display.display();
}

void connectWiFi() {
drawHeader("CONNECTING");
display.setCursor(4, 24); display.println("Connecting...");
display.setCursor(4, 36); display.println(selectedSSID.substring(0, 20));
display.display();

WiFi.mode(WIFI_STA);
WiFi.begin(selectedSSID.c_str(), wifiPassword);
unsigned long start = millis();
while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
delay(200);
hid.loop();
}
currentScreen = WIFI_STATUS_SCREEN;
drawWiFiStatus();
}

// ---------------- Notepad: wrapped + scrollable ----------------
void countNoteLines() {
noteTotalLines = 1;
int col = 0;
for (int i = 0; i < noteLength; i++) {
if (noteText[i] == '\n') { noteTotalLines++; col = 0; }
else if (++col >= 21) { noteTotalLines++; col = 0; }
}
}

void drawNotepad() {
drawHeader("NOTEPAD");
countNoteLines();

const int visibleLines = 5;
int maxScroll = max(0, noteTotalLines - visibleLines);
noteScrollLine = constrain(noteScrollLine, 0, maxScroll);

// Render wrapped lines into a small viewport. The status banner is removed.
String line = "";
int logicalLine = 0;
int col = 0;
int screenLine = 0;
int y = 14;

auto printLineIfVisible = [&](const String& s) {
if (logicalLine >= noteScrollLine && logicalLine < noteScrollLine + visibleLines) {
display.setCursor(2, y);
display.print(s);
y += 8;
screenLine++;
}
logicalLine++;
};

for (int i = 0; i < noteLength; i++) {
char c = noteText[i];
if (c == '\n') {
printLineIfVisible(line);
line = ""; col = 0;
} else {
line += c;
col++;
if (col >= 21) {
printLineIfVisible(line);
line = ""; col = 0;
}
}
}
if (line.length() || noteLength == 0) printLineIfVisible(line);

display.drawLine(0, 54, 127, 54, SSD1306_WHITE);
display.setCursor(2, 57);
display.print("Type | Y: Scroll");
display.display();
}

// ---------------- AI MODE ----------------
void drawAI() {
drawHeader("AR CLOUDX AI");
display.setCursor(2, 14);
if (WiFi.status() != WL_CONNECTED) {
display.println("Connect Wi-Fi in");
display.println("Settings first.");
} else if (aiBusy) {
display.println("Thinking...");
} else {
display.println(aiStatus.substring(0, 21));
display.setCursor(2, 25);
String prompt = "You: " + aiInput;
if (prompt.length() > 21) prompt = prompt.substring(prompt.length() - 21);
display.println(prompt);
display.setCursor(2, 36);
String answer = aiReply;
int start = max(0, (int)answer.length() - 42);
display.println(answer.substring(start, start + 21));
if (answer.length() > start + 21) {
display.setCursor(2, 45);
display.println(answer.substring(start + 21, start + 42));
}
}
drawFooter("Type + Enter: Send");
display.display();
}

void sendAIMessage() {
if (aiInput.length() == 0 || aiBusy) return;
if (WiFi.status() != WL_CONNECTED) {
aiStatus = "Connect Wi-Fi first";
drawAI();
return;
}

String sent = aiInput;
aiBusy = true;
aiStatus = "Sending...";
drawAI();

WiFiClientSecure client;
client.setInsecure(); // Prototype only; add certificate verification for production.
HTTPClient http;
http.setTimeout(20000);

if (!http.begin(client, AI_URL)) {
aiBusy = false; aiStatus = "HTTP setup failed"; drawAI(); return;
}
http.addHeader("Content-Type", "application/json");

JsonDocument doc;
doc["message"] = sent;
JsonArray history = doc["history"].to();
for (int i = 0; i < chatCount; i++) {
JsonObject u = history.add();
u["role"] = "user"; u["content"] = chatUser[i];
JsonObject a = history.add();
a["role"] = "assistant"; a["content"] = chatReply[i];
}

String body;
serializeJson(doc, body);
int status = http.POST(body);
String response = status > 0 ? http.getString() : "";
http.end();

aiBusy = false;
if (status >= 200 && status < 300) {
JsonDocument result;
DeserializationError err = deserializeJson(result, response);
if (!err && result["reply"].is<const char*>()) {
aiReply = result["reply"].as();
if (chatCount == 4) {
for (int i = 1; i < 4; i++) { chatUser[i-1] = chatUser[i]; chatReply[i-1] = chatReply[i]; }
chatCount = 3;
}
chatUser[chatCount] = sent;
chatReply[chatCount] = aiReply;
chatCount++;
aiStatus = "AR CLOUDX replied";
} else aiStatus = "Bad server response";
} else {
aiStatus = "HTTP error " + String(status);
}
aiInput = "";
drawAI();
}

// ---------------- IR: carrier test only until codes known ----------------
void setupIRCarrier() {
// ESP32 Arduino core 3.x LEDC API. Generates a 38 kHz carrier on GPIO4.
ledcAttach(IR_LED_PIN, 38000, 8);
ledcWrite(IR_LED_PIN, 0);
}

void sendIRCarrierTest() {
// This is only a short carrier burst, NOT a complete remote-control command.
ledcWrite(IR_LED_PIN, 128);
delay(100);
ledcWrite(IR_LED_PIN, 0);
}

void drawIR() {
drawHeader("IR REMOTE");
display.setCursor(3, 18); display.println("IR LED: GPIO 4");
display.setCursor(3, 29); display.println("38 kHz carrier test");
display.setCursor(3, 40); display.println("Tap: Send test burst");
display.setCursor(3, 49); display.println("Codes needed for remote");
display.display();
}

// ---------------- Navigation ----------------
void goHome() {
currentScreen = HOME;
drawHome();
}

void openSelectedApp() {
switch (selectedApp) {
case 0: currentScreen = AI_SCREEN; drawAI(); break;
case 1: currentScreen = IR_SCREEN; drawIR(); break;
case 2: currentScreen = SETTINGS_SCREEN; settingsSelection = 0; drawSettings(); break;
case 3: currentScreen = NOTEPAD_SCREEN; drawNotepad(); break;
case 4: currentScreen = ABOUT_SCREEN; drawHeader("ABOUT OS");
display.setCursor(3, 18); display.println("AR CLOUDX V3.2");
display.println("ESP32 + OLED");
display.println("128 x 64 display");
display.display(); break;
}
}

void openSettingsItem() {
if (settingsSelection == 0) startWiFiScan();
else if (settingsSelection == 1) {
currentScreen = BLUETOOTH_SCREEN;
drawHeader("BLUETOOTH");
display.setCursor(4, 20); display.println("BLE keyboard");
display.setCursor(4, 34); display.println(keyboardConnected ? "CONNECTED" : "SEARCHING...");
display.display();
} else {
currentScreen = ABOUT_SCREEN;
drawHeader("SYSTEM INFO");
display.setCursor(3, 18); display.println("ESP32 DevKit V1");
display.println("OLED: 128x64");
display.println("Wi-Fi: 2.4 GHz");
display.display();
}
}

void handleTouch() {
bool touch = digitalRead(TOUCH_PIN);
if (touch && !previousTouch && millis() - lastTouchTime > TOUCH_DELAY) {
lastTouchTime = millis();
switch (currentScreen) {
case HOME: openSelectedApp(); break;
case SETTINGS_SCREEN: openSettingsItem(); break;
case WIFI_LIST_SCREEN:
if (networkCount > 0) {
selectedSSID = networks[selectedNetwork];
passwordLength = 0; wifiPassword[0] = '\0';
currentScreen = WIFI_PASSWORD_SCREEN; drawPasswordScreen();
}
break;
case WIFI_PASSWORD_SCREEN: connectWiFi(); break;
case WIFI_STATUS_SCREEN:
case BLUETOOTH_SCREEN: currentScreen = SETTINGS_SCREEN; drawSettings(); break;
case IR_SCREEN: sendIRCarrierTest(); drawIR(); break;
default: goHome(); break;
}
}
previousTouch = touch;
}

void handleJoystick() {
if (millis() - lastInputTime < INPUT_DELAY) return;
int x = analogRead(JOY_X);
int y = analogRead(JOY_Y);
bool changed = false;

if (currentScreen == HOME) {
if (x > HIGH_LIMIT) { selectedApp = (selectedApp + APP_COUNT - 1) % APP_COUNT; changed = true; }
else if (x < LOW_LIMIT) { selectedApp = (selectedApp + 1) % APP_COUNT; changed = true; }
if (changed) drawHome();
} else if (currentScreen == SETTINGS_SCREEN) {
if (y > HIGH_LIMIT) { settingsSelection = (settingsSelection + 1) % 3; changed = true; }
else if (y < LOW_LIMIT) { settingsSelection = (settingsSelection + 2) % 3; changed = true; }
if (changed) drawSettings();
} else if (currentScreen == WIFI_LIST_SCREEN && networkCount > 0) {
if (y > HIGH_LIMIT) { selectedNetwork = (selectedNetwork + 1) % networkCount; changed = true; }
else if (y < LOW_LIMIT) { selectedNetwork = (selectedNetwork + networkCount - 1) % networkCount; changed = true; }
if (changed) drawWiFiList();
} else if (currentScreen == NOTEPAD_SCREEN) {
if (y > HIGH_LIMIT) { noteScrollLine++; changed = true; }
else if (y < LOW_LIMIT) { noteScrollLine--; changed = true; }
if (changed) drawNotepad();
}
if (changed) lastInputTime = millis();
}

// ---------------- Keyboard text entry ----------------
void processKeyboardInput() {
if (!keyQueue) return;
char c;
while (xQueueReceive(keyQueue, &c, 0) == pdTRUE) {
if (currentScreen == WIFI_PASSWORD_SCREEN) {
if (c == '\b' && passwordLength > 0) {
wifiPassword[--passwordLength] = '\0';
} else if (c == '\r') {
connectWiFi();
} else if (c >= 32 && c <= 126 && passwordLength < 63) {
wifiPassword[passwordLength++] = c;
wifiPassword[passwordLength] = '\0';
}
if (currentScreen == WIFI_PASSWORD_SCREEN) drawPasswordScreen();
} else if (currentScreen == NOTEPAD_SCREEN) {
if (c == '\b' && noteLength > 0) noteText[--noteLength] = '\0';
else if (c == '\r' && noteLength < (int)sizeof(noteText) - 2) {
noteText[noteLength++] = '\n'; noteText[noteLength] = '\0';
} else if (c >= 32 && c <= 126 && noteLength < (int)sizeof(noteText) - 2) {
noteText[noteLength++] = c; noteText[noteLength] = '\0';
}
countNoteLines();
noteScrollLine = max(0, noteTotalLines - 5); // auto-scroll to newest text
drawNotepad();
} else if (currentScreen == AI_SCREEN) {
if (c == '\b' && aiInput.length()) aiInput.remove(aiInput.length() - 1);
else if (c == '\r') sendAIMessage();
else if (c >= 32 && c <= 126 && aiInput.length() < 100) aiInput += c;
if (currentScreen == AI_SCREEN && !aiBusy) drawAI();
}
}
}

// ---------------- Setup / loop ----------------
void setup() {
Serial.begin(115200);
pinMode(TOUCH_PIN, INPUT);
pinMode(IR_LED_PIN, OUTPUT);
digitalWrite(IR_LED_PIN, LOW);
analogReadResolution(12);

Wire.begin(OLED_SDA, OLED_SCL);
if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
Serial.println("OLED ERROR");
while (true) delay(100);
}

display.clearDisplay();
display.setTextColor(SSD1306_WHITE);
for (int r = 2; r <= 25; r += 3) {
display.clearDisplay();
display.drawCircle(64, 30, r, SSD1306_WHITE);
display.setCursor(29, 55); display.print("AR CLOUDX");
display.display(); delay(35);
}
display.clearDisplay();
display.setCursor(22, 25); display.println("AR CLOUDX");
display.setCursor(35, 39); display.println("VERSION 3.2");
display.display(); delay(700);

setupIRCarrier();
keyQueue = xQueueCreate(64, sizeof(char));

hid.setDebug(false);
hid.setTargetDeviceName("Bluetooth Keyboard");
BLEKeyboard &keyboard = hid.get_keyboard();
keyboard.on_key_pressed(on_key_pressed);
keyboard.on_key_released(on_key_released);
hid.begin("AR CLOUDX");

WiFi.mode(WIFI_STA);
WiFi.disconnect();

drawHome();
Serial.println("AR CLOUDX V3.2 started");
}

void loop() {
hid.loop();

bool connected = hid.is_connected();
if (connected != keyboardConnected) {
keyboardConnected = connected;
if (currentScreen == BLUETOOTH_SCREEN) {
drawHeader("BLUETOOTH");
display.setCursor(4, 20); display.println("BLE keyboard");
display.setCursor(4, 34); display.println(keyboardConnected ? "CONNECTED" : "SEARCHING...");
display.display();
}
// Deliberately do not draw a keyboard status banner in Notepad.
}

processKeyboardInput();
if (currentScreen == WIFI_LIST_SCREEN) updateWiFiScan();
handleJoystick();
handleTouch();
delay(5);
}