#include <Arduino.h>
#include <NimBLEDevice.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <LittleFS.h>

// ====== HARDVER BEÁLLÍTÁS ======
#define SENSOR_PIN 10   // hall sensor for cadence
#define STEP_PIN 3  // stepper driver step pin
#define DIR_PIN 2  // stepper driver direction pin
#define ZERO_PIN 9 // stepper zeroing switch

struct Config {
  float nullPointRotation; // mechanical endpoint as a fraction of a full rotation (1.0)
  float rotationRange; // maximum mechanical rotation range in full rotations (e.g. 2.5 for 250% grade)
  float cadenceFactor;  //estimated cadence in rpm = cadenceFactor / interval_ms
  float speedFactor;   //estimated km/h = cadence * speedFactor
};

Config config= {1.0f, 3.0f, 40000.0f, 0.4f}; // default config values, will be overridden by saved config if available


int stepperStepsPerRevolution = 200*27; // number of steps for a full revolution of the stepper motor, including any gearing (e.g. 200 steps/rev * 27:1 gear ratio)

int currentStepPosition = 0;
int gradeToGo = 0;

volatile bool rehomeRequested = false;
volatile bool isHoming = false;

// ====== BLE ======
NimBLECharacteristic* indoorBikeDataChar = nullptr;
NimBLECharacteristic* ftmsStatus = nullptr;

// ====== variants ======
float targetGrade = 0.0f;
float currentGrade = 0.0f;
unsigned long lastPulseTime = 0;
unsigned long lastInterval = 0;
unsigned long lastUpdate = 0;
unsigned long lastStepperUpdate = 0;
char prevmsg[128];

bool currentDir = LOW;

// ====== STEPPER ======
long targetStepPosition = 0;
unsigned long lastStepMicros = 0;
const unsigned long STEP_INTERVAL_US = 900;  


void handleSave();


// web 

WebServer server(80);
Preferences prefs;

void handleRehome() {
  if (targetGrade != 0 || isHoming) {
    server.send(409, "application/json", "{\"ok\":false}");
    return;
  }

  rehomeRequested = true;
  server.send(200, "application/json", "{\"ok\":true}");
}

const char* WIFI_SSID = "SSID"; // WiFi SSID
const char* WIFI_PASS = "pwd"; // WiFi password

int getStepRange() {
  int szogTartomany = 360 * config.rotationRange;
  return szogTartomany * stepperStepsPerRevolution / 360;
}

void startFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("[FS] Mount failed");
  } else {
    Serial.println("[FS] LittleFS mounted");
  }
}

void handleRoot() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "index.html missing");
    return;
  }

  server.streamFile(f, "text/html");
  f.close();
}

void handleGetConfig() {
  String json = "{";
  json += "\"nullRot\":" + String(config.nullPointRotation) + ",";
  json += "\"rotRange\":" + String(config.rotationRange) + ",";
  json += "\"cadFact\":" + String(config.cadenceFactor) + ",";
  json += "\"spdFact\":" + String(config.speedFactor);
  json += "}";

  server.send(200, "application/json", json);
}


void handleStatus() {
  int stepRange = getStepRange();
  float percent = stepRange > 0
    ? (100.0f * currentStepPosition / stepRange)
    : 0.0f;

  String json = "{";
  json += "\"stepperPercent\":" + String(percent, 1) + ",";
  json += "\"stepperSteps\":" + String(currentStepPosition) + ",";
  json += "\"grade\":" + String(currentGrade, 1);
  json += "}";

  server.send(200, "application/json", json);
}


void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[WIFI] Connecting");
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin("neo2")) {
      Serial.println("[mDNS] http://ftms.local");
    }
  } else {
    Serial.println("\n[WIFI] FAILED – running without web UI");
  }
}

void startWeb() {
  server.on("/", handleRoot);
  server.on("/config", HTTP_GET, handleGetConfig);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/rehome", HTTP_POST, handleRehome);

  server.on("/favicon.ico", []() { server.send(204); });

  server.onNotFound([]() {
    Serial.print("[WEB] 404 ");
    Serial.println(server.uri());
    server.send(404, "text/plain", "Not found");
  });

  server.begin();
  Serial.println("[WEB] Web server started");
}




// ====== CONFIG ======

void loadConfig() {
  prefs.begin("bikecfg", true);

  config.nullPointRotation = prefs.getFloat("nullRot", 0.3f);
  config.rotationRange     = prefs.getFloat("rotRange", 2.5f);
  config.cadenceFactor     = prefs.getFloat("cadFact", 60000.0f);
  config.speedFactor       = prefs.getFloat("spdFact", 0.1f);

  prefs.end();
}

void saveConfig() {
  prefs.begin("bikecfg", false);

  prefs.putFloat("nullRot", config.nullPointRotation);
  prefs.putFloat("rotRange", config.rotationRange);
  prefs.putFloat("cadFact", config.cadenceFactor);
  prefs.putFloat("spdFact", config.speedFactor);

  prefs.end();
}

void handleSave() {
  config.nullPointRotation = constrain(server.arg("nullRot").toFloat(), 0.0f, 1.0f);
  config.rotationRange     = constrain(server.arg("rotRange").toFloat(), 0.5f, 3.0f);
  config.cadenceFactor     = constrain(server.arg("cadFact").toFloat(), 1000.0f, 100000.0f);
  config.speedFactor       = constrain(server.arg("spdFact").toFloat(), 0.01f, 1.0f);

  saveConfig();

  server.send(200, "application/json", "{\"ok\":true}");
}



// ====== INTERRUPT ======
void IRAM_ATTR onSensorTrigger() { 
  unsigned long now = millis(); 
  
    
  if (now - lastPulseTime < 250) return; // max ~240 rpm
 
  
  if (lastPulseTime != 0){
    lastInterval = now - lastPulseTime; 
  }

  lastPulseTime = now; 
} 

// ====== CADENCE AND SPEED ====== 
float calculateCadence() {
  static float smoothed = 0;
  float raw = 0;

  if (lastInterval != 0 && millis() - lastPulseTime < 3000) {
    raw = config.cadenceFactor / lastInterval;
    
    if (raw > 180) raw = smoothed; 
  }

  smoothed = smoothed * 0.8f + raw * 0.2f;
  return smoothed;
}


float calculateSpeed(float cadence) {
  return cadence * config.speedFactor;  
}

// ====== MOTOR SETUP======
void setupStepper() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
}

// ====== STEPPER ZEROING ======

void stepperZero() {
  
  digitalWrite(DIR_PIN, HIGH);  // up

  Serial.println("Zeroing stepper...");

  while (digitalRead(ZERO_PIN)!=LOW)
  {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(700);      
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(700);
  }

  Serial.println("Zero position reached. Homing back a bit...");
  
  digitalWrite(DIR_PIN, LOW);

  int zeroPositionSteps = stepperStepsPerRevolution*config.nullPointRotation; // steps to move back from the zero switch to the mechanical endpoint  

  for(int x = 0; x < zeroPositionSteps; x++){
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(700);      
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(700);
  }

  currentStepPosition = 0;
  currentGrade = 0.0f;
  Serial.println("Homing done.");
  
}

void setTargetGrade(float grade) {
  grade = constrain(grade, 0.0f, 20.0f);
  targetGrade = grade;
  int szogTartomany = 360*config.rotationRange; // ratio of the mechanical range to a full rotation
  int stepRange = szogTartomany*stepperStepsPerRevolution/360; // number of steps to the maximum grade


  targetStepPosition = map(
    (int)(grade * 10),     // grade multiplied by 10 to allow one decimal place
    0, 200,
    0, stepRange
  );
}

void stepperTask() {
  if (currentStepPosition == targetStepPosition) return;

  unsigned long now = micros();
  if (now - lastStepMicros < STEP_INTERVAL_US) return;
  lastStepMicros = now;

  bool dir = targetStepPosition > currentStepPosition;
  digitalWrite(DIR_PIN, dir ? LOW : HIGH);

  // one step
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(3);
  digitalWrite(STEP_PIN, LOW);

  currentStepPosition += dir ? 1 : -1;

  int szogTartomany = 360*config.rotationRange; // ratio of the mechanical range to a full rotation
  int stepRange = szogTartomany*stepperStepsPerRevolution/360; // number of steps to the maximum grade

  currentGrade = map(
    currentStepPosition,
    0, stepRange,
    0, 200
  ) / 10.0f;
}


// ====== FTMS DATA Transfer ======
void sendIndoorBikeData(float speedKph, float cadenceRpm, uint16_t powerWatts) {
  if (!indoorBikeDataChar) return;

  const uint16_t flags = 0x01D5; // More Data + Speed + Cadence + Power + Elapsed + Distance
  uint16_t instSpeed   = (uint16_t)(speedKph * 100.0f);
  uint16_t instCadence = (uint16_t)(cadenceRpm * 2.0f);
  uint16_t instPower   = constrain(powerWatts, 0, 2000);

  static uint16_t elapsedTime = 0;
  static uint16_t distance = 0;

  elapsedTime += 4;
  distance += (uint16_t)(speedKph * 1000.0f / 3600.0f);

  uint8_t pkt[12];
  pkt[0]  = flags & 0xFF;
  pkt[1]  = (flags >> 8) & 0xFF;
  pkt[2]  = instCadence & 0xFF;
  pkt[3]  = (instCadence >> 8) & 0xFF;
  pkt[4]  = elapsedTime & 0xFF;
  pkt[5]  = (elapsedTime >> 8) & 0xFF;
  pkt[6]  = instSpeed & 0xFF;
  pkt[7]  = instPower & 0xFF;
  pkt[8]  = (instPower >> 8);
  pkt[10] = distance & 0xFF;
  pkt[11] = (distance >> 8);

  indoorBikeDataChar->setValue(pkt, sizeof(pkt));
  indoorBikeDataChar->notify();

  char msg[126];
  snprintf(msg, sizeof(msg),
           "[FTMS] TX: %.1f km/h  %.1f rpm  %u W  (flags=0x%04X)",
           speedKph, cadenceRpm, powerWatts, flags);

  if (strcmp(prevmsg, msg) != 0) {
    Serial.println(msg);
    strncpy(prevmsg, msg, sizeof(prevmsg));
    prevmsg[sizeof(prevmsg) - 1] = '\0';
  }
}


// ====== FTMS CONTROL POINT CALLBACK ======
class FtmsControlPointCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    if (value.empty()) return;
    uint8_t opcode = value[0];

    Serial.printf("\n[FTMS] Control Point received %d bytes:\n", value.length());
    for (uint8_t b : value) Serial.printf("  0x%02X", b);

    Serial.println();

    switch (opcode) {
      case 0x00:
        Serial.println("→ Request Control");
        break;

      case 0x01:
        Serial.println("→ Reset Control: returning to free ride");
        targetGrade = 0;
        break;

      case 0x05: {  // Set Target Power (ERG mode)
        if (value.size() >= 3) {
          uint16_t target = value[1] | (value[2] << 8);
          Serial.printf("→ Set Target Power: %u W\n", target);
          targetGrade = constrain((float)target / 30.0f, -15.0f, 15.0f);
          Serial.printf("[FTMS] Adjusting resistance for %.1f %% equivalent grade\n", targetGrade);
        } else {
          Serial.println("→ Set Target Power: invalid format");
        }
        break;
      }

      case 0x07:
        Serial.println("→ Set Resistance Level: reset to default");
        targetGrade = 0;
        break;

      case 0x11:
        if (value.size() >= 7) {
          int16_t gradeRaw = value[3] | (value[4] << 8);
          float newGrade = gradeRaw / 100.0f;
          Serial.printf("→ Grade %.2f %%\n", newGrade);
          setTargetGrade(newGrade);
        }
        break;


      default:
        Serial.printf("→ Unknown opcode 0x%02X\n", opcode);
        break;
    }

    // respond with "success" for the received opcode
    uint8_t resp[3] = {0x80, opcode, 0x01};
    characteristic->setValue(resp, sizeof(resp));
    characteristic->indicate();

    if (ftmsStatus) {
      uint8_t status[3] = {0x02, opcode, 0x01};
      ftmsStatus->setValue(status, sizeof(status));
      ftmsStatus->notify();
    }
  }
};

// ====== BLE ======
void setupBLE() {
  NimBLEDevice::init("Neo2");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityInitKey(0);
  NimBLEDevice::setSecurityRespKey(0);
  NimBLEDevice::setSecurityPasskey(0);

  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* ftms = server->createService((uint16_t)0x1826);

  indoorBikeDataChar = ftms->createCharacteristic((uint16_t)0x2AD2, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* ftmsFeature = ftms->createCharacteristic((uint16_t)0x2ACC, NIMBLE_PROPERTY::READ);

  uint8_t feat[4] = {0x1F, 0x20, 0x1C, 0x00}; // controllable + resistance + simulation
  ftmsFeature->setValue(feat, sizeof(feat));

  NimBLECharacteristic* resRange = ftms->createCharacteristic((uint16_t)0x2AD5, NIMBLE_PROPERTY::READ);
  int16_t minR = -1000, maxR = 1000, stepR = 10;
  uint8_t rr[6];
  memcpy(rr, &minR, 2); memcpy(rr + 2, &maxR, 2); memcpy(rr + 4, &stepR, 2);
  resRange->setValue(rr, sizeof(rr));

  ftmsStatus = ftms->createCharacteristic((uint16_t)0x2AD6, NIMBLE_PROPERTY::NOTIFY);

  NimBLECharacteristic* controlPoint =
      ftms->createCharacteristic((uint16_t)0x2AD9, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::INDICATE);
  controlPoint->setCallbacks(new FtmsControlPointCallback());

  ftms->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID((uint16_t)0x1826);
  adv->setScanResponse(false);
  adv->start();

  Serial.println("[BLE] Advertising started, controllable FTMS ready!");
}


// ====== MAIN ======
void setup() {
  Serial.begin(115200);
  delay(1000);

  loadConfig();          // NVS → RAM
  connectWiFi();         // LAN connection for web UI
  startFS();             // file system for web UI
  startWeb();            // web server for configuration and status


  pinMode(SENSOR_PIN, INPUT_PULLUP);
  pinMode(ZERO_PIN, INPUT_PULLUP);
  setupStepper();   
  stepperZero();

  attachInterrupt(SENSOR_PIN, onSensorTrigger, FALLING);
  
  setupBLE();
  Serial.println("[INIT] Ready to connect to app!");


}


void loop() {


  server.handleClient();   // handle web requests
  if (rehomeRequested && !isHoming) {
    isHoming = true;
    rehomeRequested = false;

    Serial.println("[STEPPER] Rehome started");
    stepperZero();
    Serial.println("[STEPPER] Rehome done");

    isHoming = false;
  }



  unsigned long now = millis();


  // stepper control
  stepperTask();

  // FTMS data 1 Hz
  if (now - lastUpdate > 1000) {
    lastUpdate = now;

    float cadence = calculateCadence();
    float speed = calculateSpeed(cadence);
    uint16_t power = (uint16_t)(0.015 * cadence * cadence + 10);

    sendIndoorBikeData(speed, cadence, power);
  }

  

}
