🔐 SmartStayLock

An ESP32-based smart door lock system for short-term rental properties. Tenants are registered via Telegram, receive a one-time OTP, and access the door through a keypad code. All activity is logged to a MySQL database.


📋 Features


Telegram Bot — Owner registers tenants remotely via /newtenant with a 4-step flow (Name → Phone → Check-in → Check-out)
OTP System — One-time password sent via Telegram, entered on keypad by tenant to set their personal door code
Access Window — Door only unlocks within the registered check-in / check-out time window
Fingerprint Authentication — Owner/Admin access via fingerprint sensor
Brute Force Protection — Progressive delays, lockout levels, and permanent lockout after repeated wrong attempts
Time Sync — 3-method time sync (WorldTimeAPI → TimeAPI.io → NTP) with auto-retry
MySQL Logging — All actions logged to database with user, role, and timestamp
OLED Display — Real-time status on 128×64 OLED screen
Multi-WiFi — Connects to up to 3 configured WiFi networks automatically



🛠️ Hardware Required

ComponentDetailsESP32Main microcontrollerFingerprint SensorUART, connected to pins 18 (RX) / 19 (TX)4×4 Matrix KeypadConnected to pins 27, 14, 12, 13, 32, 33, 25, 26OLED DisplaySSD1306 128×64, I2C address 0x3CRelay ModulePin 23 — controls door lockBuzzerPin 4


📁 Project Structure

smartstaylock_api/
├── db.php                    # Database connection
├── save_log.php              # Receives logs from ESP32
├── register_tenant.php       # Registers tenant manually
├── register_tenant_auto.php  # Registers tenant + logs in one call
├── get_admin.php             # Looks up admin by fingerprint ID

SmartStayLock.ino             # Main ESP32 Arduino sketch
README.md


⚙️ Setup Instructions

1. Database

Create a MySQL database named smartstaylock and run the following SQL:

sqlCREATE TABLE tenants (
    id INT AUTO_INCREMENT PRIMARY KEY,
    tenant_name VARCHAR(100),
    phone VARCHAR(20),
    checkin_date VARCHAR(20),
    checkout_date VARCHAR(20),
    status VARCHAR(20),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE access_logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    action VARCHAR(100),
    status VARCHAR(100),
    user_name VARCHAR(100),
    role VARCHAR(50),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE admins (
    id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100),
    fingerprint_id INT,
    role VARCHAR(50)
);

2. PHP API


Copy the smartstaylock_api/ folder to your XAMPP htdocs directory
Open db.php and fill in your database credentials:


php$conn = new mysqli(
    "YOUR_DB_HOST",
    "YOUR_DB_USERNAME",
    "YOUR_DB_PASSWORD",
    "YOUR_DB_NAME"
);


Open save_log.php and set your API key:


php$valid_key = "YOUR_API_KEY";


Test your server is working by visiting:


http://YOUR_SERVER_IP/smartstaylock_api/test.php

It should return WORKING.

3. Arduino Sketch


Install the following libraries via Arduino Library Manager:

ArduinoJson
Adafruit GFX
Adafruit SSD1306
Adafruit Fingerprint Sensor Library
Keypad



Open SmartStayLock.ino and fill in your credentials at the top of the file:


cpp// WiFi
const char* ssids[]     = { "YOUR_WIFI_SSID_1", ... };
const char* passwords[] = { "YOUR_WIFI_PASSWORD_1", ... };

// Telegram
String botToken = "YOUR_TELEGRAM_BOT_TOKEN";
String chatID   = "YOUR_TELEGRAM_CHAT_ID";

// Server
String serverName = "http://YOUR_SERVER_IP/smartstaylock_api/save_log.php";

// Admin
String superAdminName = "YOUR_ADMIN_NAME";


Select your ESP32 board and upload.



💬 Telegram Bot Commands

CommandDescription/newtenantRegister a new tenant (4-step flow)/statusView current system status/resetClear current tenant access/emergencyOpen door immediately/clearlockoutClear brute force lockout/synctimeForce time re-sync/cancelCancel current operation/helpShow all commands


🔒 Tenant Flow

Owner sends /newtenant
        │
        ▼
Step 1/4 — Enter Tenant Name
        │
        ▼
Step 2/4 — Enter Phone Number
        │
        ▼
Step 3/4 — Enter Check-In date/time (DD/MM/YYYY HH:MM)
        │
        ▼
Step 4/4 — Enter Check-Out date/time (DD/MM/YYYY HH:MM)
        │
        ▼
Bot sends OTP to owner → Owner gives OTP to tenant
        │
        ▼
Tenant enters OTP on keypad → Sets personal door code
        │
        ▼
Tenant uses code to unlock door within access window


🛡️ Brute Force Protection

EventConsequence5 wrong OTPsLockout triggered5 wrong door codesLockout triggeredLockout Level 130 secondsLockout Level 25 minutesLockout Level 330 minutes3 lockouts totalPermanent lockout — owner must send /clearlockout


⚠️ Important Notes


All times are in Malaysia Time (MYT / UTC+8)
The ESP32 and your server must be on the same local network for the API to work
Never commit your real credentials — keep db.php and the .ino file credentials private
Enroll fingerprints into the sensor before use (slot 1 = Super Admin, slot 2 & 3 = Admin)



👨‍💻 Author

Faiz — SmartStayLock Project
