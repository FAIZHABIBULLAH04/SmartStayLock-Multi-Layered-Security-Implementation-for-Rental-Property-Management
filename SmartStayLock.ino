#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Keypad.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_Fingerprint.h>
#include <EEPROM.h>
#include <time.h>   

// ================= WIFI =================
const char* ssids[] = {
  "YOUR_WIFI_SSID_1",
  "YOUR_WIFI_SSID_2",
  "YOUR_WIFI_SSID_3"
};
const char* passwords[] = {
  "YOUR_WIFI_PASSWORD_1",
  "YOUR_WIFI_PASSWORD_2",
  "YOUR_WIFI_PASSWORD_3"
};
const int wifiCount = 3;

// ================= TELEGRAM =================
String botToken = "YOUR_TELEGRAM_BOT_TOKEN";
String chatID   = "YOUR_TELEGRAM_CHAT_ID";

// ================= DATABASE =================
String serverName =
"http://YOUR_SERVER_IP/smartstaylock_api/save_log.php";

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= PINS =================
#define RELAY_PIN  23
#define BUZZER_PIN 4

// ================= FINGERPRINT =================
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ================= KEYPAD =================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'D','C','B','A'},
  {'#','9','6','3'},
  {'0','8','5','2'},
  {'*','7','4','1'}
};
byte rowPins[ROWS] = {27, 14, 12, 13};
byte colPins[COLS] = {32, 33, 25, 26};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ================= OTP =================
String OTP      = "";
bool   otpReady = false;

// ================= ACCESS WINDOW =================
String        tempCode         = "";
bool          accessReady      = false;
bool          codeVerified     = false;
unsigned long windowStartEpoch = 0;
unsigned long windowEndEpoch   = 0;
bool          windowSet        = false;
String        windowStartStr   = "";
String        windowEndStr     = "";

// ================= TELEGRAM STATE =================
enum TelegramState {
  STATE_IDLE,
  STATE_WAIT_NAME,
  STATE_WAIT_PHONE,
  STATE_WAIT_CHECKIN,
  STATE_WAIT_CHECKOUT
};
TelegramState telegramState = STATE_IDLE;

// ================= TENANT INFO =================
String tenantName  = "";
String tenantPhone = "";

// ================= TIME SYNC =================
unsigned long bootEpoch  = 0;
unsigned long bootMillis = 0;
bool          timesynced = false;
unsigned long checkinEpoch = 0;

// Retry sync periodically if it failed
unsigned long lastSyncAttempt = 0;
const unsigned long SYNC_RETRY_INTERVAL = 60000UL; // retry every 60s

// ================= BRUTE FORCE =================
int  otpWrongAttempts  = 0;
int  codeWrongAttempts = 0;
int  lockoutLevel      = 0;

bool          systemLocked    = false;
unsigned long lockoutEndEpoch = 0;

const unsigned long ATTEMPT_DELAYS[] = { 0, 3000, 10000, 30000, 60000 };
const int ATTEMPT_DELAYS_COUNT = 5;

const int MAX_OTP_ATTEMPTS  = 5;
const int MAX_CODE_ATTEMPTS = 5;

const unsigned long LOCKOUT_DURATIONS_SEC[] = {
  30UL,
  300UL,
  1800UL,
  0UL
};
const int MAX_LOCKOUT_LEVEL = 3;

// EEPROM
#define EEPROM_SIZE          8
#define EEPROM_ADDR_LEVEL    0
#define EEPROM_ADDR_OTP_ATT  1
#define EEPROM_ADDR_CODE_ATT 2
#define EEPROM_MAGIC_ADDR    7
#define EEPROM_MAGIC_VAL     0xA5

// ================= ADMIN =================
bool adminMode = false;

// ================= TELEGRAM POLL =================
unsigned long lastTelegramCheck = 0;
static int    lastUpdateID      = -1;

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void          checkTelegram();
void          handleTelegramMessage(String text);
bool          parseDateTime(String input, unsigned long &epochOut, String &displayOut);
unsigned long currentEpoch();
bool          syncTime();
bool          syncTimeWorldTimeAPI();
bool          syncTimeTimeAPI();
bool          syncTimeNTP();
void          showMainMenu();
void          tenantMode();
void          accessLoop();
void          ownerMode();
void          adminMenu();
bool          verifyFingerprint();
void          generateOTP();
String        formatTimeLeft();
void          saveLog(String action, String status, String userName = "System", String role = "System");
bool          sendTelegram(String message);
String        getTelegramMessage();
void          unlockDoor();
void          triggerBuzzer();
void          displayText(String l1, String l2 = "", String l3 = "");
String        getInput(String prompt);
void          feedWatchdog();
void          handleWrongOTP();
void          handleWrongCode();
void          triggerLockout(String reason);
void          clearLockout(bool fullReset);
void          progressiveDelay(int attempts);
void          showLockoutScreen();
void          eepromSave();
void          eepromLoad();
bool          isPermanentlyLocked();
bool          isWindowExpired();
bool          isWindowOpen();
bool          isBeforeWindow();

// ================= SUPER ADMIN / ADMIN =================
String superAdminName   = "YOUR_ADMIN_NAME";
int superAdminFingerprint = 1;  // Fingerprint slot for Super Admin
int adminFingerprint1     = 2;  // Fingerprint slot for Admin 1
int adminFingerprint2     = 3;  // Fingerprint slot for Admin 2

// ============================================================
// MULTI-WIFI CONNECT
// ============================================================
bool connectWiFi() {

  WiFi.mode(WIFI_STA);

  for (int i = 0; i < wifiCount; i++) {

    Serial.println();
    Serial.println("Trying WiFi: " + String(ssids[i]));

    WiFi.disconnect(true, true); // <- IMPORTANT
    delay(1000);

    WiFi.begin(ssids[i], passwords[i]);

    int retry = 0;

    while (WiFi.status() != WL_CONNECTED && retry < 20) {
      delay(500);
      Serial.print(".");
      retry++;
    }

    if (WiFi.status() == WL_CONNECTED) {

      Serial.println();
      Serial.println("CONNECTED!");

      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      return true;
    }
  }

  Serial.println("NO WIFI FOUND");

  return false;
}
// ============================================================
// EEPROM
// ============================================================
void eepromSave() {
  EEPROM.write(EEPROM_ADDR_LEVEL,    (uint8_t)lockoutLevel);
  EEPROM.write(EEPROM_ADDR_OTP_ATT,  (uint8_t)min(otpWrongAttempts,  255));
  EEPROM.write(EEPROM_ADDR_CODE_ATT, (uint8_t)min(codeWrongAttempts, 255));
  EEPROM.write(EEPROM_MAGIC_ADDR,    EEPROM_MAGIC_VAL);
  EEPROM.commit();
}

void eepromLoad() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VAL) {
    lockoutLevel = 0; otpWrongAttempts = 0; codeWrongAttempts = 0;
    eepromSave();
    return;
  }
  lockoutLevel      = (int)EEPROM.read(EEPROM_ADDR_LEVEL);
  otpWrongAttempts  = (int)EEPROM.read(EEPROM_ADDR_OTP_ATT);
  codeWrongAttempts = (int)EEPROM.read(EEPROM_ADDR_CODE_ATT);
  Serial.println("EEPROM: level=" + String(lockoutLevel)
    + " otp=" + String(otpWrongAttempts)
    + " code=" + String(codeWrongAttempts));
}

// ============================================================
// TIME SYNC — METHOD 1: worldtimeapi.org
// ============================================================
bool syncTimeWorldTimeAPI() {
  Serial.println("Trying worldtimeapi.org...");
  WiFiClient client;
  HTTPClient http;
  http.begin(client, "http://worldtimeapi.org/api/timezone/Asia/Kuala_Lumpur");
  http.setTimeout(6000);
  int code = http.GET();
  if (code != 200) {
    Serial.println("worldtimeapi failed: " + String(code));
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, body)) return false;

  unsigned long epoch = doc["unixtime"].as<unsigned long>();
  if (epoch < 1000000000UL) return false;

  bootEpoch  = epoch;
  bootMillis = millis();
  timesynced = true;
  Serial.println("worldtimeapi OK. Epoch=" + String(bootEpoch));
  return true;
}

// ============================================================
// TIME SYNC — METHOD 2: timeapi.io
// ============================================================
bool syncTimeTimeAPI() {
  Serial.println("Trying timeapi.io...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.begin(client, "https://timeapi.io/api/time/current/zone?timeZone=Asia%2FKuala_Lumpur");
  https.setTimeout(6000);
  int code = https.GET();
  if (code != 200) {
    Serial.println("timeapi.io failed: " + String(code));
    https.end();
    return false;
  }
  String body = https.getString();
  https.end();

  
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, body)) return false;

  unsigned long epoch = doc["unixTime"].as<unsigned long>();
  if (epoch < 1000000000UL) return false;

  bootEpoch  = epoch;
  bootMillis = millis();
  timesynced = true;
  Serial.println("timeapi.io OK. Epoch=" + String(bootEpoch));
  return true;
}

// ============================================================
// TIME SYNC — METHOD 3: NTP (most reliable, built into ESP32)
// ============================================================
bool syncTimeNTP() {
  Serial.println("Trying NTP...");

  
  // ================= WIFI CONNECT =================

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  // ================= MALAYSIA NTP TIME =================

configTime(8 * 3600, 0, "pool.ntp.org");

Serial.println("Waiting for NTP time...");

time_t now = time(nullptr);

while (now < 100000) {

  delay(500);

  Serial.print(".");

  now = time(nullptr);
}

Serial.println("\nTime synchronized!");

// DEBUG CURRENT TIME
struct tm timeinfo;

if (getLocalTime(&timeinfo)) {

  Serial.println(&timeinfo, "%d/%m/%Y %H:%M:%S");
}

  unsigned long start = millis();
  while (!getLocalTime(&timeinfo)) {
    if (millis() - start > 8000) {
      Serial.println("NTP timeout");
      return false;
    }
    delay(200);
    feedWatchdog();
  }

  // Convert to unix epoch (UTC)

  time(&now);

  if (now < 1000000000L) return false;

  // Store as UTC epoch
  bootEpoch = (unsigned long)now; 
  bootMillis = millis();
  timesynced = true;
  Serial.println("NTP OK. Epoch=" + String(bootEpoch));
  return true;
}

// ============================================================
// TIME SYNC — tries all 3 methods in order
// ============================================================
bool syncTime() {
  if (WiFi.status() != WL_CONNECTED) return false;

  displayText("SYNCING TIME...", "Method 1/3");
  if (syncTimeWorldTimeAPI()) return true;

  displayText("SYNCING TIME...", "Method 2/3");
  if (syncTimeTimeAPI()) return true;

  displayText("SYNCING TIME...", "Method 3/3 NTP");
  if (syncTimeNTP()) return true;

  Serial.println("All time sync methods failed.");
  return false;
}

// ============================================================
// CURRENT EPOCH
// ============================================================
unsigned long currentEpoch() {
  if (!timesynced) return 0;
  return bootEpoch + (millis() - bootMillis) / 1000UL;
}

// ============================================================
// WINDOW HELPERS
// ============================================================
bool isWindowExpired() {
  if (!windowSet || !timesynced) return false;
  return currentEpoch() > windowEndEpoch;
}

bool isWindowOpen() {
  if (!windowSet || !timesynced) return false;
  unsigned long now = currentEpoch();
  return (now >= windowStartEpoch && now <= windowEndEpoch);
}

bool isBeforeWindow() {
  if (!windowSet || !timesynced) return false;
  return currentEpoch() < windowStartEpoch;
}

// ============================================================
// PARSE DATE/TIME  DD/MM/YYYY HH:MM (MYT → UTC epoch)
// ============================================================
bool parseDateTime(String input, unsigned long &epochOut, String &displayOut) {
  input.trim();
  if (input.length() < 16) return false;

  int dd   = input.substring(0,  2).toInt();
  int mm   = input.substring(3,  5).toInt();
  int yyyy = input.substring(6, 10).toInt();
  int hh   = input.substring(11,13).toInt();
  int mi   = input.substring(14,16).toInt();

  if (dd < 1 || dd > 31)  return false;
  if (mm < 1 || mm > 12)  return false;
  if (yyyy < 2024)         return false;
  if (hh > 23 || mi > 59) return false;

  int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  bool ly = (yyyy%4==0 && (yyyy%100!=0 || yyyy%400==0));
  if (ly) daysInMonth[1] = 29;
  if (dd > daysInMonth[mm-1]) return false;

  unsigned long days = 0;
  for (int y = 1970; y < yyyy; y++) {
    bool lly = (y%4==0 && (y%100!=0 || y%400==0));
    days += lly ? 366 : 365;
  }
  for (int m = 1; m < mm; m++) days += daysInMonth[m-1];
  days += dd - 1;

  unsigned long epoch = days * 86400UL
                      + (unsigned long)hh * 3600UL
                      + (unsigned long)mi * 60UL
                      - 8UL * 3600UL;  // MYT to UTC
  epochOut = epoch;

  char buf[20];
  snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d", dd, mm, hh, mi);
  displayOut = String(buf);
  return true;
}

// ============================================================
// FORMAT TIME LEFT
// ============================================================
String formatTimeLeft() {
  if (!windowSet || !timesynced) return "No window";
  unsigned long now = currentEpoch();

  if (now < windowStartEpoch) {
    unsigned long s = windowStartEpoch - now;
    return "Opens in " + String(s / 3600) + "h " + String((s % 3600) / 60) + "m";
  }
  if (now > windowEndEpoch) return "EXPIRED";

  unsigned long s = windowEndEpoch - now;
  return String(s / 3600) + "h " + String((s % 3600) / 60) + "m left";
}

// ============================================================
// BRUTE FORCE
// ============================================================
bool isPermanentlyLocked() { return (lockoutLevel >= MAX_LOCKOUT_LEVEL); }

void progressiveDelay(int attempts) {
  int idx = constrain(attempts - 1, 0, ATTEMPT_DELAYS_COUNT - 1);
  unsigned long waitMs = ATTEMPT_DELAYS[idx];
  if (waitMs == 0) return;

  unsigned long start = millis();
  while (millis() - start < waitMs) {
    feedWatchdog();
    unsigned long rem = (waitMs - (millis() - start)) / 1000 + 1;
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("WAIT BEFORE");
    display.setCursor(0, 12); display.println("NEXT ATTEMPT");
    display.setTextSize(2);
    display.setCursor(0, 28); display.print(rem); display.println("s");
    display.setTextSize(1);
    display.setCursor(0, 50); display.println("Too many wrong tries");
    display.display();
    delay(500);
  }
}

void handleWrongOTP() {
  otpWrongAttempts++;
  eepromSave();
  int left = max(0, MAX_OTP_ATTEMPTS - otpWrongAttempts);
  displayText("WRONG OTP", String(left) + " attempt(s) left");
  sendTelegram("Wrong OTP #" + String(otpWrongAttempts) + ". " + String(left) + " left.");
  saveLog("otp_wrong", "attempt_" + String(otpWrongAttempts));
  triggerBuzzer();
  if (otpWrongAttempts >= MAX_OTP_ATTEMPTS) { triggerLockout("otp_brute_force"); return; }
  progressiveDelay(otpWrongAttempts);
}

void handleWrongCode() {
  codeWrongAttempts++;
  eepromSave();
  int left = max(0, MAX_CODE_ATTEMPTS - codeWrongAttempts);
  displayText("WRONG CODE", String(left) + " attempt(s) left");
  sendTelegram("Wrong code #" + String(codeWrongAttempts) + ". " + String(left) + " left.");
  saveLog("code_wrong", "attempt_" + String(codeWrongAttempts));
  triggerBuzzer();
  if (codeWrongAttempts >= MAX_CODE_ATTEMPTS) { triggerLockout("code_brute_force"); return; }
  progressiveDelay(codeWrongAttempts);
}

void triggerLockout(String reason) {
  lockoutLevel++;
  if (lockoutLevel > MAX_LOCKOUT_LEVEL) lockoutLevel = MAX_LOCKOUT_LEVEL;

  systemLocked      = true;
  otpWrongAttempts  = 0;
  codeWrongAttempts = 0;

  if (lockoutLevel >= MAX_LOCKOUT_LEVEL) {
    lockoutEndEpoch = 0;
    eepromSave();
    displayText("!! PERMANENT !!", "LOCKOUT", "Owner: /clearlockout");
    sendTelegram(
      "SECURITY ALERT: PERMANENT LOCKOUT\n"
      "Reason: " + reason + "\n"
      "Send /clearlockout to restore."
    );
    saveLog("permanent_lockout", reason);
    for (int i = 0; i < 5; i++) {
      digitalWrite(BUZZER_PIN, HIGH); delay(400);
      digitalWrite(BUZZER_PIN, LOW);  delay(200);
    }
  } else {
    unsigned long durSec = LOCKOUT_DURATIONS_SEC[lockoutLevel - 1];
    lockoutEndEpoch = timesynced ? (currentEpoch() + durSec) : 0;
    eepromSave();

    String durStr = (durSec < 60) ? String(durSec) + "s"
                  : (durSec < 3600) ? String(durSec/60) + " min"
                  : String(durSec/3600) + "h";

    displayText("LOCKED", durStr, "Level " + String(lockoutLevel) + "/3");
    sendTelegram(
      "ALERT: Device locked\n"
      "Reason: " + reason + "\n"
      "Duration: " + durStr + "\n"
      "Level: " + String(lockoutLevel) + "/3\n"
      "/clearlockout to unlock early."
    );
    saveLog("lockout_triggered", reason + "_lvl" + String(lockoutLevel));
    triggerBuzzer();
    delay(2000);
  }
}

void clearLockout(bool fullReset) {
  systemLocked      = false;
  lockoutEndEpoch   = 0;
  otpWrongAttempts  = 0;
  codeWrongAttempts = 0;
  if (fullReset) {
    lockoutLevel = 0;
    sendTelegram("Lockout fully cleared. Level reset to 0.");
    saveLog("lockout_cleared", "owner_full_reset");
  } else {
    sendTelegram(
      "Lockout auto-cleared.\n"
      "Level remains " + String(lockoutLevel) + "/3.\n"
      + String(MAX_LOCKOUT_LEVEL - lockoutLevel) + " more before permanent."
    );
    saveLog("lockout_cleared", "auto_timeout");
  }
  eepromSave();
}

void showLockoutScreen() {
  if (isPermanentlyLocked()) {
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("!! PERMANENT !!");
    display.setCursor(0, 12); display.println("LOCKOUT ACTIVE");
    display.setCursor(0, 24); display.println("Owner must send");
    display.setCursor(0, 34); display.println("/clearlockout");
    display.setCursor(0, 46); display.println("via Telegram");
    display.display();
    delay(1000);
    return;
  }

  if (timesynced && lockoutEndEpoch > 0 && currentEpoch() >= lockoutEndEpoch) {
    clearLockout(false);
    return;
  }

  unsigned long remaining = 0;
  if (timesynced && lockoutEndEpoch > 0) remaining = lockoutEndEpoch - currentEpoch();

  display.clearDisplay();
  display.setCursor(0, 0);  display.println("!! LOCKED !!");
  display.setCursor(0, 12); display.print("Level: "); display.print(lockoutLevel); display.println("/3");
  display.setCursor(0, 22);
  if      (remaining >= 3600) { display.print(remaining/3600); display.print("h "); display.print((remaining%3600)/60); display.println("m"); }
  else if (remaining >= 60)   { display.print(remaining/60); display.println("m left"); }
  else if (remaining > 0)     { display.print(remaining); display.println("s left"); }
  else                        { display.println("Ask owner"); }
  display.setCursor(0, 34); display.println("/clearlockout");
  display.display();
  delay(1000);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  eepromLoad();

  pinMode(RELAY_PIN, OUTPUT);  digitalWrite(RELAY_PIN, LOW);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED"); while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  if (lockoutLevel >= MAX_LOCKOUT_LEVEL) {
    systemLocked    = true;
    lockoutEndEpoch = 0;
    displayText("PERMANENT", "LOCKOUT", "/clearlockout");
    delay(2000);
  }

  displayText("CONNECTING", "WIFI...");
  connectWiFi();
  int retry = 0; 

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi: " + WiFi.localIP().toString());
    displayText("WIFI OK", WiFi.localIP().toString());
    delay(800);

    
    if (syncTime()) {
      char tbuf[30];
      unsigned long e = currentEpoch();
      unsigned long myt = e + 28800UL;
      int th = (myt % 86400) / 3600;
      int tm = (myt % 3600) / 60;
      snprintf(tbuf, sizeof(tbuf), "Time: %02d:%02d MYT", th, tm);
      displayText("TIME SYNCED", String(tbuf));
      Serial.println("Sync success. " + String(tbuf));

      if (systemLocked && !isPermanentlyLocked() && lockoutEndEpoch > 0) {
        if (currentEpoch() >= lockoutEndEpoch) {
          clearLockout(false);
          displayText("LOCKOUT EXPIRED", "While offline");
          delay(1500);
        }
      }
    } else {
      displayText("TIME SYNC FAIL", "Will retry...");
      Serial.println("All sync methods failed. Will retry in loop.");
      delay(2000);
    }

  } else {
    Serial.println("\nWiFi FAILED");
    displayText("WIFI FAILED", "No internet");
    delay(2000);
  }
  delay(800);

  mySerial.begin(57600, SERIAL_8N1, 18, 19);
  finger.begin(57600);
  delay(500);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint OK");
  } else {
    displayText("FINGER ERROR", "Check sensor");
    while (1) { delay(1000); }
  }

  randomSeed(esp_random());

  if (systemLocked && isPermanentlyLocked()) {
    sendTelegram("SmartStayLock rebooted.\nWARNING: PERMANENT LOCKOUT.\nSend /clearlockout.");
  } else {
    sendTelegram(
      "SmartStayLock online.\n"
      "Time sync: " + String(timesynced ? "OK" : "FAILED") + "\n"
      "/newtenant - Register tenant\n"
      "/status    - System status\n"
      "/help      - All commands"
    );
  }

  saveLog("system_start", timesynced ? "success" : "no_time_sync");
  displayText("SYSTEM READY");
  delay(2000);
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  feedWatchdog();

  if (!timesynced && WiFi.status() == WL_CONNECTED) {
    if (millis() - lastSyncAttempt > SYNC_RETRY_INTERVAL) {
      lastSyncAttempt = millis();
      Serial.println("Retrying time sync...");
      display.clearDisplay();
      display.setCursor(0, 0); display.println("SYNCING TIME...");
      display.display();
      if (syncTime()) {
        sendTelegram("Time sync successful (retry).");
      }
    }
  }

  if (millis() - lastTelegramCheck > 3000) {
    checkTelegram();
    lastTelegramCheck = millis();
  }

  if (windowSet && timesynced && isWindowExpired()) {
    bool wasActive = accessReady || codeVerified || otpReady;
    tempCode           = "";
    OTP                = "";
    otpReady           = false;
    accessReady        = false;
    codeVerified       = false;
    windowSet          = false;
    windowStartEpoch   = 0;
    windowEndEpoch     = 0;
    codeWrongAttempts  = 0;
    eepromSave();
    if (wasActive) {
      displayText("CHECKOUT TIME", "Access ended");
      sendTelegram("Tenant access window ended. Guest checked out.");
      saveLog("access_expired", "window_end");
      delay(2000);
    }
  }

  if (systemLocked) {
    showLockoutScreen();
    return;
  }

  showMainMenu();

  char key = keypad.getKey();
  if (key) {
    Serial.print("KEY: "); Serial.println(key);
    if      (key == '7') tenantMode();
    else if (key == '8') ownerMode();
  }
  delay(100);

}

void feedWatchdog() { yield(); }

// ============================================================
// TELEGRAM
// ============================================================
void checkTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;
  String text = getTelegramMessage();
  if (text == "") return;
  text.trim();
  handleTelegramMessage(text);
}

void handleTelegramMessage(String text) {
  Serial.println("TG: " + text);

  if (text == "/status") {
    String msg = "SmartStayLock Status\n";
    msg += "Time sync: " + String(timesynced ? "OK" : "FAILED") + "\n";
    if (timesynced) {
      unsigned long myt = currentEpoch() + 28800UL;
      int th = (myt % 86400) / 3600;
      int tm = (myt % 3600) / 60;
      char tbuf[20];
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d MYT", th, tm);
      msg += "Current time: " + String(tbuf) + "\n";
    }
    msg += "OTP: "          + String(otpReady    ? "READY" : "NONE") + "\n";
    msg += "Code: "         + String(codeVerified ? "SET"   : "NONE") + "\n";
    msg += "Door access: "  + String(accessReady  ? "YES"   : "NO")   + "\n";
    msg += "Window: "       + String(windowSet    ? "SET"   : "NO")   + "\n";
    if (windowSet) {
      msg += "Check-in:  " + windowStartStr + " MYT\n";
      msg += "Check-out: " + windowEndStr   + " MYT\n";
      msg += "Time left: " + formatTimeLeft() + "\n";
    }
    msg += "Locked: "        + String(systemLocked ? "YES" : "NO") + "\n";
    msg += "Lockout level: " + String(lockoutLevel) + "/3\n";
    msg += "OTP wrong: "    + String(otpWrongAttempts)  + "/" + String(MAX_OTP_ATTEMPTS)  + "\n";
    msg += "Code wrong: "   + String(codeWrongAttempts) + "/" + String(MAX_CODE_ATTEMPTS);
    sendTelegram(msg);
    return;
  }

  if (text == "/cancel") {
    telegramState = STATE_IDLE;
    tenantName    = "";
    tenantPhone   = "";
    sendTelegram("Cancelled.");
    return;
  }

  if (text == "/reset") {
    tempCode          = "";
    OTP               = "";
    otpReady          = false;
    accessReady       = false;
    codeVerified      = false;
    windowSet         = false;
    windowStartEpoch  = 0;
    windowEndEpoch    = 0;
    otpWrongAttempts  = 0;
    codeWrongAttempts = 0;
    telegramState     = STATE_IDLE;
    tenantName        = "";
    tenantPhone       = "";
    eepromSave();
    sendTelegram("Tenant access reset.");
    saveLog("admin_reset", "telegram", superAdminName, "SuperAdmin");
    displayText("RESET BY", "OWNER");
    delay(2000);
    return;
  }

  if (text == "/emergency") {
    sendTelegram("Emergency: opening door now.");
    saveLog("emergency_unlock", "telegram", superAdminName, "SuperAdmin");
    displayText("EMERGENCY", "OPEN REMOTE");
    unlockDoor();
    return;
  }

  if (text == "/clearlockout") {
    clearLockout(true);
    displayText("LOCKOUT", "CLEARED");
    delay(2000);
    return;
  }

  if (text == "/synctime") {
  
    sendTelegram("Attempting time sync...");
    if (syncTime()) {
      unsigned long myt = currentEpoch() + 28800UL;
      int th = (myt % 86400) / 3600;
      int tm = (myt % 3600) / 60;
      char tbuf[20];
      snprintf(tbuf, sizeof(tbuf), "%02d:%02d MYT", th, tm);
      sendTelegram("Time sync OK!\nCurrent time: " + String(tbuf));
    } else {
      sendTelegram("Time sync still failing. Check internet connection.");
    }
    return;
  }

  if (text == "/help") {
    sendTelegram(
      "Commands:\n"
      "/newtenant    - Register tenant\n"
      "/status       - Full status\n"
      "/reset        - Clear tenant access\n"
      "/emergency    - Open door now\n"
      "/clearlockout - Clear lockout\n"
      "/synctime     - Force time re-sync\n"
      "/cancel       - Cancel operation\n"
      "/help         - This list\n\n"
      "Lockout levels:\n"
      "1 = 30 sec\n"
      "2 = 5 min\n"
      "3 = PERMANENT"
    );
    return;
  }

  if (text == "/newtenant") {
    if (!timesynced) {
      sendTelegram("Time not synced. Attempting sync before continuing...");
      if (syncTime()) {
        sendTelegram("Time sync OK. Continuing with /newtenant.");
      } else {
        sendTelegram(
          "Time sync failed. Cannot register tenant without time sync.\n"
          "Try:\n"
          "1. Check device WiFi connection\n"
          "2. Send /synctime to retry\n"
          "3. Reboot the device"
        );
        return;
      }
    }

    telegramState = STATE_WAIT_NAME;
    tenantName    = "";
    tenantPhone   = "";
    sendTelegram(
      "New tenant setup.\n\n"
      "Step 1/4: TENANT NAME\n"
      "Enter full name of tenant.\n"
      "Example: Ahmad Syafiq\n\n"
      "/cancel to abort."
    );
    return;
  }


  if (telegramState == STATE_WAIT_NAME) {
    text.trim();
    if (text.length() < 2) {
      sendTelegram("Name too short. Please enter the tenant's full name.\nOr /cancel.");
      return;
    }
    tenantName    = text;
    telegramState = STATE_WAIT_PHONE;
    sendTelegram(
      "Name: " + tenantName + "\n\n"
      "Step 2/4: PHONE NUMBER\n"
      "Enter tenant phone number.\n"
      "Example: 0123456789\n\n"
      "/cancel to abort."
    );
    return;
  }


  if (telegramState == STATE_WAIT_PHONE) {
    text.trim();

    bool valid = (text.length() >= 7 && text.length() <= 15);
    for (int i = 0; i < (int)text.length() && valid; i++) {
      if (!isDigit(text[i]) && text[i] != '+' && text[i] != '-') valid = false;
    }
    if (!valid) {
      sendTelegram("Invalid phone number. Enter digits only (7-15 chars).\nExample: 0123456789\nOr /cancel.");
      return;
    }
    tenantPhone   = text;
    telegramState = STATE_WAIT_CHECKIN;
    sendTelegram(
      "Phone: " + tenantPhone + "\n\n"
      "Step 3/4: CHECK-IN date/time\n"
      "Format: DD/MM/YYYY HH:MM\n"
      "Example: 15/06/2026 15:00\n"
      "Malaysia time (MYT).\n\n"
      "/cancel to abort."
    );
    return;
  }

  if (telegramState == STATE_WAIT_CHECKIN) {
    unsigned long epoch = 0;
    String disp;
    if (!parseDateTime(text, epoch, disp)) {
      sendTelegram("Invalid format.\nUse: DD/MM/YYYY HH:MM\nExample: 15/06/2026 15:00\nOr /cancel.");
      return;
    }
    if (timesynced && epoch < currentEpoch() - 300) {
      sendTelegram("That time is in the past. Enter a future check-in time.");
      return;
    }
    checkinEpoch   = epoch;
    windowStartStr = disp;
    telegramState  = STATE_WAIT_CHECKOUT;
    sendTelegram(
      "Check-in: " + disp + " MYT\n\n"
      "Step 4/4: CHECK-OUT date/time\n"
      "Format: DD/MM/YYYY HH:MM\n"
      "Example: 16/06/2026 12:00\n\n"
      "/cancel to abort."
    );
    return;
  }

  if (telegramState == STATE_WAIT_CHECKOUT) {
    unsigned long epoch = 0;
    String disp;
    if (!parseDateTime(text, epoch, disp)) {
      sendTelegram("Invalid format.\nUse: DD/MM/YYYY HH:MM\nOr /cancel.");
      return;
    }
    if (epoch <= checkinEpoch) {
      sendTelegram("Check-out must be AFTER check-in (" + windowStartStr + "). Try again.");
      return;
    }
    if (epoch - checkinEpoch > 30UL * 86400UL) {
      sendTelegram("Max stay is 30 days.");
      return;
    }

    windowStartEpoch  = checkinEpoch;
    windowEndEpoch    = epoch;
    windowEndStr      = disp;
    windowSet         = true;

    tempCode          = "";
    accessReady       = false;
    codeVerified      = false;
    OTP               = "";
    otpWrongAttempts  = 0;
    codeWrongAttempts = 0;
    eepromSave();

    generateOTP();
    otpReady = true;

    unsigned long stayS = epoch - checkinEpoch;

    Serial.println("=== WINDOW SET ===");
    Serial.println("Tenant       : " + tenantName + " / " + tenantPhone);
    Serial.println("currentEpoch : " + String(currentEpoch()));
    Serial.println("windowStart  : " + String(windowStartEpoch));
    Serial.println("windowEnd    : " + String(windowEndEpoch));
    Serial.println("isOpen?      : " + String(isWindowOpen()    ? "YES" : "NO"));
    Serial.println("isBefore?    : " + String(isBeforeWindow()  ? "YES" : "NO"));
    Serial.println("isExpired?   : " + String(isWindowExpired() ? "YES" : "NO"));

    if (WiFi.status() == WL_CONNECTED) {
      auto urlEncode = [](String s) -> String {
        String out = "";
        for (int i = 0; i < (int)s.length(); i++) {
          char c = s[i];
          if (c == ' ') out += "%20";
          else if (c == '&') out += "%26";
          else if (c == '=') out += "%3D";
          else if (c == '+') out += "%2B";
          else out += c;
        }
        return out;
      };

      WiFiClient wifiClient3;
      HTTPClient http3;
      String tenantUrl = serverName
        + "?api_key=YOUR_API_KEY"
        + "&action=register_tenant"
        + "&tenant_name=" + urlEncode(tenantName)
        + "&phone="       + urlEncode(tenantPhone);
      http3.begin(wifiClient3, tenantUrl);
      http3.setTimeout(6000);
      int tCode = http3.GET();
      Serial.println("TENANT SAVE: " + String(tCode));
      http3.end();
    }

    String confirm =
      "Tenant registered!\n\n"
      "Name:  " + tenantName  + "\n"
      "Phone: " + tenantPhone + "\n\n"
      "OTP: " + OTP + " (single use)\n\n"
      "Check-in:  " + windowStartStr + " MYT\n"
      "Check-out: " + windowEndStr   + " MYT\n"
      "Duration:  " + String(stayS/3600) + "h " + String((stayS%3600)/60) + "m\n\n"
      "Brute force protection:\n"
      "5 wrong OTPs = lockout\n"
      "5 wrong codes = lockout\n"
      "3 lockouts = permanent lock";

    sendTelegram(confirm);
    saveLog("tenant_registered", "success", tenantName, "Tenant");
    telegramState = STATE_IDLE;
    displayText("TENANT READY", windowStartStr);
    delay(2000);
    return;
  }

  if (telegramState == STATE_IDLE) {
    sendTelegram("Unknown command. /help for list.");
  }
}

// ============================================================
// MAIN MENU
// ============================================================
void showMainMenu() {
  display.clearDisplay();
  display.setCursor(0, 0); display.println("SMARTSTAYLOCK");
  display.setCursor(0, 11);

  if (!timesynced) {
    display.println("NO TIME SYNC");
  } else if (accessReady && windowSet) {
    display.println(formatTimeLeft());
  } else if (codeVerified && windowSet && isBeforeWindow()) {
    display.println("WAITING CHECKIN");
    display.setCursor(0, 21); display.println(formatTimeLeft());
  } else if (otpReady && windowSet) {
    display.println("OTP READY");
  } else {
    display.println("STANDBY");
  }

  if (lockoutLevel > 0) {
    display.setCursor(0, 26);
    display.print("Alert lvl:"); display.println(lockoutLevel);
  }

  display.setCursor(0, 38); display.println("[7] TENANT");
  display.setCursor(0, 50); display.println("[8] OWNER");
  display.display();
}

// ============================================================
// TENANT MODE
// ============================================================
void tenantMode() {
  if (systemLocked) { showLockoutScreen(); return; }

  if (!timesynced) {
    displayText("TIME NOT SYNCED", "Send /synctime", "to owner");
    delay(2500);
    return;
  }

  if (accessReady && windowSet) { accessLoop(); return; }

  if (codeVerified && windowSet && isBeforeWindow()) {
    unsigned long secsUntil = windowStartEpoch - currentEpoch();
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("CODE READY");
    display.setCursor(0, 12); display.println("Not open yet.");
    display.setCursor(0, 24); display.print("Opens: "); display.println(windowStartStr);
    display.setCursor(0, 36); display.print("In: ");
    display.print(secsUntil / 3600); display.print("h ");
    display.print((secsUntil % 3600) / 60); display.println("m");
    display.setCursor(0, 50); display.println("Come back later.");
    display.display();
    delay(4000);
    return;
  }

  if (codeVerified && windowSet && isWindowOpen()) {
    accessReady = true;
    displayText("WINDOW OPEN!", "Enter your code");
    sendTelegram("Check-in window now open. Access activated.");
    saveLog("access_activated", "window_opened");
    delay(1500);
    accessLoop();
    return;
  }

  if (!otpReady) { displayText("NO OTP READY", "Contact owner"); delay(2500); return; }
  if (!windowSet) { displayText("NO WINDOW SET", "Contact owner"); delay(2500); return; }

  String enteredOTP = getInput("ENTER OTP:");
  Serial.println("OTP: [" + enteredOTP + "] vs [" + OTP + "]");

  if (enteredOTP != OTP) { handleWrongOTP(); return; }

  otpWrongAttempts = 0;
  OTP      = "";
  otpReady = false;
  eepromSave();

  displayText("OTP VERIFIED!", "Set your code:");
  sendTelegram("Tenant OTP accepted.");
  saveLog("otp_verify", "success", tenantName, "Tenant");
  delay(2000);

  String code1 = getInput("SET CODE (4+):");
  if (code1.length() < 4) {
    displayText("TOO SHORT", "Min 4 digits");
    triggerBuzzer(); delay(2500);
    sendTelegram("Code too short. Use /newtenant for new OTP.");
    return;
  }

  String code2 = getInput("CONFIRM CODE:");
  if (code1 != code2) {
    displayText("CODES DIFFER", "Ask owner for", "new OTP");
    triggerBuzzer(); delay(2500);
    sendTelegram("Code mismatch. Use /newtenant for new OTP.");
    return;
  }

  tempCode          = code1;
  codeVerified      = true;
  codeWrongAttempts = 0;
  eepromSave();

  if (isWindowOpen()) {
    accessReady = true;
    displayText("ACCESS ACTIVE!", formatTimeLeft());
    sendTelegram("Tenant code set. Access active.\nCheck-out: " + windowEndStr + " MYT\nTime left: " + formatTimeLeft());
    saveLog("access_created", "success", tenantName, "Tenant");
    delay(2000);
    accessLoop();

  } else if (isBeforeWindow()) {
    unsigned long secsUntil = windowStartEpoch - currentEpoch();
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("CODE SAVED!");
    display.setCursor(0, 12); display.println("Come back at:");
    display.setCursor(0, 24); display.println(windowStartStr);
    display.setCursor(0, 36); display.print("In: ");
    display.print(secsUntil / 3600); display.print("h ");
    display.print((secsUntil % 3600) / 60); display.println("m");
    display.display();
    sendTelegram("Tenant code saved. Window opens at " + windowStartStr + " MYT.");
    saveLog("access_created", "before_window");
    delay(5000);

  } else {
    displayText("WINDOW EXPIRED", "Contact owner");
    sendTelegram("Tenant used OTP after window expired. Use /newtenant.");
    saveLog("access_created", "window_expired");
    triggerBuzzer();
    codeVerified = false;
    tempCode     = "";
    delay(3000);
  }
}

// ============================================================
// ACCESS LOOP
// ============================================================
void accessLoop() {
  accessReady = true;

  while (!systemLocked) {
    feedWatchdog();

    if (!windowSet || isWindowExpired()) {
      accessReady        = false;
      codeVerified       = false;
      tempCode           = "";
      windowSet          = false;
      windowStartEpoch   = 0;
      windowEndEpoch     = 0;
      codeWrongAttempts  = 0;
      eepromSave();
      display.clearDisplay();
      display.setCursor(0, 0);  display.println("CHECKOUT TIME");
      display.setCursor(0, 16); display.println("Stay has ended.");
      display.setCursor(0, 32); display.println("Door access closed.");
      display.display();
      sendTelegram("Tenant access ended. Checkout time reached.");
      saveLog("access_ended", "checkout");
      delay(4000);
      return;
    }

    unsigned long secsLeft = windowEndEpoch - currentEpoch();
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("ENTER CODE:");
    display.setCursor(0, 12);
    display.print(secsLeft / 3600); display.print("h ");
    display.print((secsLeft % 3600) / 60); display.println("m left");
    if (codeWrongAttempts > 0) {
      display.setCursor(0, 22);
      display.print("Attempts: "); display.print(codeWrongAttempts);
      display.print("/"); display.println(MAX_CODE_ATTEMPTS);
    }
    display.setCursor(0, 32); display.println("# confirm  * clear");
    display.display();

    String entered = getInput("ENTER CODE:");

    if (entered == tempCode) {
      codeWrongAttempts = 0;
      eepromSave();
      displayText("ACCESS GRANTED", "Welcome!");
      sendTelegram("Door opened by tenant: " + tenantName);
      saveLog("door_open", "success", tenantName, "Tenant");

      if (WiFi.status() == WL_CONNECTED) {
        WiFiClient wifiClient2;
        HTTPClient http2;
        String url2 = serverName + "?api_key=YOUR_API_KEY&action=update_opened&status=success"
                    + "&opened_by=John&opened_at=NOW";
        http2.begin(wifiClient2, url2);
        http2.setTimeout(4000);
        http2.GET();
        http2.end();
      }

      unlockDoor();
    } else {
      handleWrongCode();
      if (systemLocked) return;
    }
  }
}

// ============================================================
// OWNER / ADMIN
// ============================================================
void ownerMode() {
  displayText("SCAN FINGER", "(10 sec)");
  unsigned long t = millis();
  while (millis() - t < 10000) {
    feedWatchdog();
    if (verifyFingerprint()) {
      adminMode = true;
      displayText("ADMIN ACCESS", "GRANTED");
      sendTelegram("Owner accessed admin via fingerprint.");
      saveLog("admin_access", "success", superAdminName, "SuperAdmin");
      delay(1500);
      adminMenu();
      return;
    }
    delay(200);
  }
  displayText("NO FINGER", "DETECTED");
  saveLog("admin_access", "fp_timeout");
  delay(2000);
}

void adminMenu() {
  while (adminMode) {
    feedWatchdog();
    display.clearDisplay();
    display.setCursor(0, 0);  display.println("== ADMIN ==");
    display.setCursor(0, 10); display.println("1 EMERGENCY OPEN");
    display.setCursor(0, 19); display.println("2 FULL RESET");
    display.setCursor(0, 28); display.println("3 CLEAR LOCKOUT");
    display.setCursor(0, 37); display.println("4 STATUS");
    display.setCursor(0, 46); display.println("7 EXIT");
    display.display();

    char key = keypad.getKey();
    if (!key) { delay(100); continue; }

    if (key == '1') {
      displayText("EMERGENCY", "OPENING...");
      sendTelegram("Owner: emergency open.");
      saveLog("emergency_unlock", "keypad", superAdminName, "SuperAdmin");
      unlockDoor();
    }
    else if (key == '2') {
      tempCode = ""; OTP = "";
      otpReady = false; accessReady = false;
      codeVerified = false; windowSet = false;
      windowStartEpoch = 0; windowEndEpoch = 0;
      otpWrongAttempts = 0; codeWrongAttempts = 0;
      telegramState = STATE_IDLE;
      eepromSave();
      displayText("FULL RESET", "Done");
      sendTelegram("Full reset via keypad.");
      saveLog("admin_reset", "keypad", superAdminName, "SuperAdmin");
      delay(2000);
    }
    else if (key == '3') {
      clearLockout(true);
      displayText("LOCKOUT", "CLEARED");
      delay(2000);
    }
    else if (key == '4') {
      display.clearDisplay();
      display.setCursor(0, 0);  display.println("== STATUS ==");
      display.setCursor(0, 9);  display.print("Time: "); display.println(timesynced ? "OK" : "FAIL");
      display.setCursor(0, 18); display.print("OTP: "); display.println(otpReady ? "READY" : "NONE");
      display.setCursor(0, 27); display.print("Code: "); display.println(codeVerified ? (accessReady ? "ACTIVE" : "SAVED") : "NONE");
      display.setCursor(0, 36); display.print("Lock lvl: "); display.print(lockoutLevel); display.println("/3");
      display.setCursor(0, 45); display.print("OTP err: "); display.println(otpWrongAttempts);
      display.setCursor(0, 54); display.print("Code err: "); display.println(codeWrongAttempts);
      display.display();
      delay(6000);
    }
    else if (key == '7') {
      adminMode = false;
      displayText("ADMIN EXIT");
      sendTelegram("Owner logged out.");
      saveLog("admin_exit", "success", superAdminName, "SuperAdmin");
      delay(1000);
      return;
    }
  }
}

// ============================================================
// FINGERPRINT / UTILITIES
// ============================================================
bool verifyFingerprint() {
  uint8_t p = finger.getImage();    if (p != FINGERPRINT_OK) return false;
  p = finger.image2Tz();            if (p != FINGERPRINT_OK) return false;
  p = finger.fingerFastSearch();    if (p != FINGERPRINT_OK) return false;
  return (finger.fingerID >= 1);
}

void generateOTP() {
  OTP = String(random(100000, 999999));
  Serial.println("OTP: " + OTP);
}

void saveLog(String action, String status, String userName, String role) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("saveLog: WiFi not connected, skipping.");
    return;
  }


  auto urlEncode = [](String s) -> String {
    String out = "";
    for (int i = 0; i < (int)s.length(); i++) {
      char c = s[i];
      if (c == ' ') out += "%20";
      else if (c == '&') out += "%26";
      else if (c == '=') out += "%3D";
      else if (c == '+') out += "%2B";
      else out += c;
    }
    return out;
  };

  String url = serverName
    + "?api_key=YOUR_API_KEY"
    + "&action="    + urlEncode(action)
    + "&status="    + urlEncode(status)
    + "&user_name=" + urlEncode(userName)
    + "&role="      + urlEncode(role);

  WiFiClient wifiClient;
  HTTPClient http;
  http.begin(wifiClient, url);
  http.setTimeout(6000);
  int code = http.GET();
  Serial.println("LOG " + String(code) + ": " + action + " [" + userName + "/" + role + "]");
  http.end();
}

bool sendTelegram(String message) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  String url = "https://api.telegram.org/bot" + botToken +
               "/sendMessage?chat_id=" + chatID + "&text=" + message;
  url.replace(" ", "%20");
  url.replace("\n", "%0A");
  https.begin(client, url);
  https.setTimeout(7000);
  int code = https.GET();
  Serial.println("Telegram: " + String(code));
  https.end();
  return (code == 200);
}

String getTelegramMessage() {
  if (WiFi.status() != WL_CONNECTED) return "";
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  String url = "https://api.telegram.org/bot" + botToken +
               "/getUpdates?offset=" + String(lastUpdateID + 1) +
               "&timeout=1&limit=5";
  https.begin(client, url);
  https.setTimeout(7000);
  int code = https.GET();
  if (code != 200) { https.end(); return ""; }
  String body = https.getString();
  https.end();
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, body)) return "";
  int size = doc["result"].size();
  if (size == 0) return "";
  lastUpdateID = doc["result"][size - 1]["update_id"].as<int>();
  return doc["result"][size - 1]["message"]["text"].as<String>();
}

void unlockDoor() {
  digitalWrite(RELAY_PIN, HIGH); delay(5000); digitalWrite(RELAY_PIN, LOW);
  displayText("DOOR CLOSED"); delay(1000);
}

void triggerBuzzer() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH); delay(120);
    digitalWrite(BUZZER_PIN, LOW);  delay(80);
  }
}

void displayText(String l1, String l2, String l3) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println(l1);
  if (l2 != "") { display.setCursor(0, 16); display.println(l2); }
  if (l3 != "") { display.setCursor(0, 32); display.println(l3); }
  display.display();
}

String getInput(String prompt) {
  String input = "";
  display.clearDisplay();
  display.setCursor(0, 0);  display.println(prompt);
  display.setCursor(0, 12); display.println("# confirm  * clear");
  display.display();
  while (true) {
    feedWatchdog();
    char key = keypad.getKey();
    if (key) {
      if (key == '#') { delay(300); break; }
      if (key == '*') { input = ""; display.fillRect(0, 40, 128, 24, BLACK); display.display(); continue; }
      if (key >= '0' && key <= '9') {
        input += key;
        display.fillRect(0, 40, 128, 24, BLACK);
        display.setCursor(0, 40);
        for (int i = 0; i < (int)input.length(); i++) display.print("*");
        display.display();
      }
    }
    delay(50);
  }
  Serial.println("Input: " + input);
  return input;
}