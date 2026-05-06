#include <SoftwareSerial.h>
#include <Adafruit_Fingerprint.h>
#include <LiquidCrystal.h>

// ---------------- LCD ----------------
LiquidCrystal lcd(8, 9, 10, 11, 12, 13);

// ---------------- SERIAL PORTS ----------------
// RFID reader: TX from RFID -> D2
SoftwareSerial rfidSerial(2, 7);   // RX, TX (TX unused for RFID, pin 7 dummy)

// Fingerprint sensor
SoftwareSerial fingerSerial(3, 4); // RX, TX
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&fingerSerial);

// GSM module
SoftwareSerial gsmSerial(5, 6);    // RX, TX

// ---------------- OUTPUT PINS ----------------
const int RELAY_PIN  = A0;
const int BUZZER_PIN = A1;

// ---------------- SETTINGS ----------------
String ownerPhone = "+91XXXXXXXXXX";   // <-- Change this
unsigned long ignitionOnTime = 10000;  // 10 seconds motor ON for demo

// Authorized RFID tags (10 hex characters typically from 125kHz reader)
String authorizedRFID[] = {
  "0A0035BC91",
  "1F00AB12CD",
  "2200D4E981"
};
const int RFID_COUNT = sizeof(authorizedRFID) / sizeof(authorizedRFID[0]);

// Authorized fingerprint IDs stored in fingerprint sensor memory
uint8_t authorizedFingerIDs[] = {1, 2, 3};
const int FINGER_COUNT = sizeof(authorizedFingerIDs) / sizeof(authorizedFingerIDs[0]);

// ---------------- GLOBALS ----------------
bool waitingForCard = true;

// ----------------------------------------------------
// Utility functions
// ----------------------------------------------------
void beepSuccess() {
  tone(BUZZER_PIN, 2000, 150);
  delay(200);
  tone(BUZZER_PIN, 2500, 150);
  delay(200);
  noTone(BUZZER_PIN);
}

void beepFail() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 800, 250);
    delay(300);
  }
  noTone(BUZZER_PIN);
}

void lcdMessage(const String &line1, const String &line2 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1.substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(line2.substring(0, 16));
}

bool isAuthorizedRFID(String tag) {
  tag.trim();
  tag.toUpperCase();

  for (int i = 0; i < RFID_COUNT; i++) {
    if (tag == authorizedRFID[i]) {
      return true;
    }
  }
  return false;
}

bool isAuthorizedFingerID(uint8_t id) {
  for (int i = 0; i < FINGER_COUNT; i++) {
    if (id == authorizedFingerIDs[i]) {
      return true;
    }
  }
  return false;
}

// ----------------------------------------------------
// GSM functions
// ----------------------------------------------------
void sendAT(String cmd, int waitMs = 1000) {
  gsmSerial.println(cmd);
  delay(waitMs);
  while (gsmSerial.available()) {
    Serial.write(gsmSerial.read());
  }
}

void sendSMS(String phone, String message) {
  lcdMessage("Sending SMS...", "");
  sendAT("AT", 1000);
  sendAT("AT+CMGF=1", 1000); // text mode

  gsmSerial.print("AT+CMGS=\"");
  gsmSerial.print(phone);
  gsmSerial.println("\"");
  delay(1000);

  gsmSerial.print(message);
  delay(300);

  gsmSerial.write(26); // Ctrl+Z
  delay(4000);

  while (gsmSerial.available()) {
    Serial.write(gsmSerial.read());
  }
}

// ----------------------------------------------------
// RFID read function for 125kHz modules like EM-18 / RDM6300
// Usually sends 12 bytes or frame with STX/ETX.
// We extract 10-character tag ID.
// ----------------------------------------------------
String readRFIDTag(unsigned long timeoutMs = 10000) {
  unsigned long start = millis();
  String raw = "";

  while (millis() - start < timeoutMs) {
    while (rfidSerial.available()) {
      char c = rfidSerial.read();

      // Keep printable hex-like content only
      if (isPrintable(c)) {
        raw += c;
      }

      // Some readers send a line / frame; if enough data is collected, parse
      if (raw.length() >= 10) {
        // Try last 10 chars as tag
        String candidate = raw.substring(raw.length() - 10);
        candidate.toUpperCase();

        bool valid = true;
        for (unsigned int i = 0; i < candidate.length(); i++) {
          if (!isHexadecimalDigit(candidate[i])) {
            valid = false;
            break;
          }
        }

        if (valid) {
          return candidate;
        }
      }
    }
  }
  return "";
}

// ----------------------------------------------------
// Fingerprint match
// Returns fingerprint ID if matched, else -1
// ----------------------------------------------------
int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;

  p = finger.fingerSearch();
  if (p != FINGERPRINT_OK) return -1;

  return finger.fingerID;
}

// ----------------------------------------------------
// Enroll fingerprint helper from Serial Monitor
// type e1 or e2 or e3 in serial monitor if needed
// ----------------------------------------------------
uint8_t enrollFingerprint(uint8_t id) {
  int p = -1;

  Serial.print("Enrolling finger ID #");
  Serial.println(id);

  lcdMessage("Enroll Finger", "Place finger");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      default:
        Serial.println("Image error");
        return p;
    }
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) return p;

  lcdMessage("Remove finger", "");
  delay(2000);

  p = 0;
  lcdMessage("Place same fingr", "Again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken again");
        break;
      case FINGERPRINT_NOFINGER:
        break;
      default:
        Serial.println("Image error");
        return p;
    }
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) return p;

  p = finger.createModel();
  if (p != FINGERPRINT_OK) return p;

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    lcdMessage("Enroll Success", "ID: " + String(id));
  } else {
    lcdMessage("Enroll Failed", "");
  }
  delay(2000);
  return p;
}

// ----------------------------------------------------
// Ignition control
// ----------------------------------------------------
void startIgnition() {
  digitalWrite(RELAY_PIN, HIGH);
  lcdMessage("Access Granted", "Ignition ON");
  Serial.println("Ignition ON");
  beepSuccess();

  delay(ignitionOnTime);

  digitalWrite(RELAY_PIN, LOW);
  lcdMessage("Ignition OFF", "Scan next user");
  Serial.println("Ignition OFF");
  delay(1500);
}

void denyAccess(String reason) {
  digitalWrite(RELAY_PIN, LOW);
  lcdMessage("Access Denied", reason);
  Serial.println("Denied: " + reason);
  beepFail();

  sendSMS(ownerPhone, "ALERT: Unauthorized access attempt detected. Reason: " + reason);
  delay(2000);
}

// ----------------------------------------------------
// Setup
// ----------------------------------------------------
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Serial.begin(9600);
  rfidSerial.begin(9600);    // common for EM-18 / RDM6300
  fingerSerial.begin(57600); // common for many fingerprint sensors
  gsmSerial.begin(9600);

  lcd.begin(16, 2);
  lcdMessage("Vehicle Security", "System Booting");
  delay(2000);

  finger.begin(57600);
  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor found");
    lcdMessage("Fingerprint OK", "");
  } else {
    Serial.println("Fingerprint sensor NOT found");
    lcdMessage("Finger Sensor", "Not Found");
    while (1) {
      delay(1);
    }
  }

  // GSM startup
  lcdMessage("Initializing", "GSM Module");
  sendAT("AT", 1000);
  sendAT("AT+CMGF=1", 1000);

  lcdMessage("Scan RFID Card", "");
  Serial.println("System ready");
  Serial.println("Optional: type e1/e2/e3 in Serial Monitor to enroll fingerprint IDs 1/2/3");
}

// ----------------------------------------------------
// Loop
// ----------------------------------------------------
void loop() {
  // Optional enrollment from Serial Monitor
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();

    if (cmd == "e1") enrollFingerprint(1);
    else if (cmd == "e2") enrollFingerprint(2);
    else if (cmd == "e3") enrollFingerprint(3);

    lcdMessage("Scan RFID Card", "");
  }

  // Step 1: Read RFID
  lcdMessage("Show RFID Card", "");
  String tag = readRFIDTag(500);

  if (tag.length() > 0) {
    Serial.print("RFID Tag: ");
    Serial.println(tag);

    if (!isAuthorizedRFID(tag)) {
      denyAccess("Invalid RFID");
      lcdMessage("Show RFID Card", "");
      return;
    }

    lcdMessage("RFID Matched", "Place Finger");
    Serial.println("RFID authorized");

    // Step 2: Fingerprint verification
    unsigned long startWait = millis();
    bool fingerMatched = false;

    while (millis() - startWait < 10000) {
      int fingerID = getFingerprintID();
      if (fingerID != -1) {
        Serial.print("Finger matched ID: ");
        Serial.println(fingerID);

        if (isAuthorizedFingerID((uint8_t)fingerID)) {
          fingerMatched = true;
          lcdMessage("Finger Matched", "ID: " + String(fingerID));
          delay(1000);
          startIgnition();
        } else {
          denyAccess("Wrong Finger");
        }
        break;
      }
    }

    if (!fingerMatched) {
      denyAccess("Finger Timeout");
    }

    lcdMessage("Show RFID Card", "");
  }
}
