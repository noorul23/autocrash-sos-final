// Autocrash SOS OG code 
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <time.h>

// ===== WiFi =====
#define WIFI_SSID "XXXX"  // Change your ssid
#define WIFI_PASSWORD "YYYY" // Change your password

// ===== Firebase =====
#define API_KEY "xxxxxxxxxxxxxxxxxxxxxx"  // change your API Key
#define FIRESTORE_PROJECT_ID "yyyyyyyy"   // Change your firebase project_id
#define DATABASE_URL "zzzzzzzzzzzzzzz/"   // Change your URL
 
// Firebase Objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
FirebaseJson json;

// ===== GPS & SIM800L =====
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
HardwareSerial sim800l(1);

// ===== Pins =====
#define GPS_RX 16
#define GPS_TX 17
#define SIM800L_RX 4
#define SIM800L_TX 5

#define BUZZER_PIN 22
#define BUTTON_PIN 23          // Push button: INPUT_PULLUP (pressed = LOW)
#define MOTOR_PIN 14           // Air pump driver (use proper transistor/driver)
#define SWITCH_PIN 27          // YL-99 OUT pin (active HIGH on impact)
#define LOOP_PIN   33          // Circuit break loop to GND, uses INPUT_PULLUP

// pumb, Circuit break loop, and YL-99 are just to simulate the airbag deplayment 

// ===== Timings =====
const unsigned long PUMP_DURATION_MS = 6000UL;   // 6 s pump
const unsigned long CANCEL_WINDOW_MS = 10000UL;  // 10 s to cancel
const unsigned long DEBOUNCE_MS      = 50UL;     // 50 ms debounce

// ===== State machine =====
enum SystemState {
  IDLE,
  PUMPING,
  ALERT_PENDING
};
SystemState state = IDLE;

// Timers
unsigned long pumpStartTime  = 0;
unsigned long alertStartTime = 0;

// Debounce storage
bool lastCollisionStable = false;
unsigned long lastCollisionChangeMs = 0;

bool lastButtonStable = false;
unsigned long lastButtonChangeMs = 0;

bool lastLoopStable = false;
unsigned long lastLoopChangeMs = 0;

// GPS cache
float lastLatitude = 0.0f;
float lastLongitude = 0.0f;

// ===== Forward declarations =====
void readGPS();
void sendATCommand(const String& command, int delayTime);
void sendSMSWithLocation(float latitude, float longitude);
void logAccident(bool loopBroken, float latitude, float longitude);
void updateRealtimeDatabase(bool loopBroken, float latitude, float longitude);

// ===== Simple raw reads (before debounce) =====
inline bool readCollisionRaw() { // YL-99 active HIGH
  return digitalRead(SWITCH_PIN) == HIGH;
}
inline bool readButtonRaw() {    // INPUT_PULLUP: pressed LOW
  return digitalRead(BUTTON_PIN) == LOW;
}
inline bool readLoopBrokenRaw() { // INPUT_PULLUP: intact LOW, broken HIGH
  return digitalRead(LOOP_PIN) == HIGH;
}

// ===== Debounce helper =====
bool debounce(bool raw, bool &lastStable, unsigned long &lastChangeTs) {
  unsigned long now = millis();
  if (raw != lastStable) {
    if (now - lastChangeTs >= DEBOUNCE_MS) {
      lastStable = raw;
      lastChangeTs = now;
      return true;   // A stable change occurred
    }
    // else wait until stable
  } else {
    // keep timestamp fresh to avoid long gaps on stable signals
    lastChangeTs = now;
  }
  return false;
}

// ===== SIM800L helpers =====
void sendATCommand(const String& command, int delayTime) {
  sim800l.println(command);
  delay(delayTime);
  while (sim800l.available()) {
    Serial.println(sim800l.readString());
  }
}

void sendSMSWithLocation(float latitude, float longitude) {
  String gmapLink = "https://maps.google.com/?q=" + String(latitude, 6) + "," + String(longitude, 6);
  String message  = "Accident Detected! Location: " + gmapLink;

  sendATCommand("AT+CMGF=1", 1000);
  sendATCommand("ATD+911234567890;", 10000); // Call  // change your receiver mobile number with country code
  delay(1000);
  sendATCommand("ATH", 1000);               // Hangup
  sendATCommand("AT+CMGS=\"+911234567890\"", 1000); // SMS // change your receiver mobile number with country code
  sim800l.print(message);
  sim800l.write(26); // CTRL+Z
  delay(3000);
}

// ===== Firebase: Firestore log =====
void logAccident(bool loopBroken, float latitude, float longitude) {
  String accidentDocPath = "accidents/" + String(random(100000, 999999));
  FirebaseJson json;

  json.set("fields/accident-status/booleanValue", loopBroken);
  json.set("fields/fire-status/booleanValue", false);
  json.set("fields/ambulance-status/stringValue", "Not allocated");
  json.set("fields/latitude/doubleValue", latitude);
  json.set("fields/longitude/doubleValue", longitude);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to get NTP time");
    json.set("fields/timestamp/stringValue", "NTP time unavailable");
  } else {
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
    json.set("fields/timestamp/stringValue", String(timestamp));
  }

  Serial.print("Firestore log... ");
  if (Firebase.Firestore.createDocument(&fbdo, FIRESTORE_PROJECT_ID, "", accidentDocPath.c_str(), json.raw())) {
    Serial.println("OK");
  } else {
    Serial.print("Failed: ");
    Serial.println(fbdo.errorReason());
  }
}

// ===== Firebase: Realtime DB =====
void updateRealtimeDatabase(bool loopBroken, float latitude, float longitude) {
  FirebaseJson json;
  json.set("/accident-status", loopBroken);
  json.set("/fire-status", false);
  json.set("/location/latitude", latitude);
  json.set("/location/longitude", longitude);
  json.set("/timestamp", String(millis()));

  Serial.print("RTDB update... ");
  if (Firebase.RTDB.setJSON(&fbdo, "/liveData", &json)) {
    Serial.println("OK");
  } else {
    Serial.print("Failed: ");
    Serial.println(fbdo.errorReason());
  }
}

// ===== GPS =====
void readGPS() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  // --- Network time (for Firestore logs) ---
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nConnected");
  configTime(19800, 0, "pool.ntp.org"); // IST

  // --- Firebase ---
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = "autocrash@gmail.com";  // change your mail(firebase)
  auth.user.password = "12345678";         // change your password(firebase)
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // --- Serial peripherals ---
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  sim800l.begin(9600, SERIAL_8N1, SIM800L_RX, SIM800L_TX);

  // --- IO directions ---
  pinMode(BUZZER_PIN, OUTPUT);          digitalWrite(BUZZER_PIN, LOW);
  pinMode(MOTOR_PIN, OUTPUT);           digitalWrite(MOTOR_PIN, LOW);

  pinMode(BUTTON_PIN, INPUT_PULLUP);    // pressed = LOW
  pinMode(SWITCH_PIN, INPUT);           // YL-99 OUT drives HIGH on impact
  pinMode(LOOP_PIN, INPUT_PULLUP);      // loop intact = LOW, broken = HIGH

  // Init debounce baselines
  lastCollisionStable   = readCollisionRaw();
  lastButtonStable      = readButtonRaw();
  lastLoopStable        = readLoopBrokenRaw();
  unsigned long now     = millis();
  lastCollisionChangeMs = now;
  lastButtonChangeMs    = now;
  lastLoopChangeMs      = now;

  state = IDLE;
}

// ===== Loop =====
void loop() {
  unsigned long now = millis();

  // GPS update
  readGPS();
  float latitude  = gps.location.isValid() ? gps.location.lat() : lastLatitude;
  float longitude = gps.location.isValid() ? gps.location.lng() : lastLongitude;

  // Read raw inputs
  bool collisionRaw  = readCollisionRaw();   // active on HIGH
  bool buttonRaw     = readButtonRaw();      // pressed LOW (INPUT_PULLUP)
  bool loopBrokenRaw = readLoopBrokenRaw();  // broken HIGH

  // Debounce to get stable signals + detect edges
  bool collisionChanged = debounce(collisionRaw,  lastCollisionStable, lastCollisionChangeMs);
  bool buttonChanged    = debounce(buttonRaw,     lastButtonStable,    lastButtonChangeMs);
  bool loopChanged      = debounce(loopBrokenRaw, lastLoopStable,      lastLoopChangeMs);

  bool collision  = lastCollisionStable;
  bool pressed    = lastButtonStable;
  bool loopBroken = lastLoopStable;

  // State machine
  switch (state) {

    case IDLE: {
      // Start pump only on a NEW impact (rising edge to HIGH)
      if (collision && collisionChanged) {
        Serial.println("Impact detected -> PUMPING");
        digitalWrite(MOTOR_PIN, HIGH);
        pumpStartTime = now;
        state = PUMPING;
      }
      // If loop is somehow already broken, start alert window immediately
      if (loopBroken && loopChanged) {
        Serial.println("Loop broken -> ALERT_PENDING");
        digitalWrite(BUZZER_PIN, HIGH);
        alertStartTime = now;
        state = ALERT_PENDING;
      }
      break;
    }

    case PUMPING: {
      // If loop breaks while pumping -> go alert immediately
      if (loopBroken && loopChanged) {
        Serial.println("Loop broken during PUMPING -> ALERT_PENDING");
        digitalWrite(MOTOR_PIN, LOW);
        digitalWrite(BUZZER_PIN, HIGH);
        alertStartTime = now;
        state = ALERT_PENDING;
        break;
      }
      // Stop pump after 6 s
      if (now - pumpStartTime >= PUMP_DURATION_MS) {
        Serial.println("Pump stopped (6 s).");
        digitalWrite(MOTOR_PIN, LOW);
        state = IDLE; // wait for loop break or another impact
      }
      break;
    }

    case ALERT_PENDING: {
      // Driver cancel within 10 s
      if (pressed && buttonChanged) {
        Serial.println("Alert cancelled by driver.");
        digitalWrite(BUZZER_PIN, LOW);
        state = IDLE;
        break;
      }
      // Timeout -> send alerts
      if (now - alertStartTime >= CANCEL_WINDOW_MS) {
        Serial.println("Sending call/SMS + Firebase log...");
        digitalWrite(BUZZER_PIN, LOW);

        sendSMSWithLocation(latitude, longitude);
        logAccident(true, latitude, longitude);
        updateRealtimeDatabase(true, latitude, longitude);

        state = IDLE;
      }
      break;
    }
  }

  // Save GPS for next loop
  lastLatitude = latitude;
  lastLongitude = longitude;

  // Keep loop responsive
  delay(5);
}
