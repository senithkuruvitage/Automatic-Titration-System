#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <AccelStepper.h>
#include <Preferences.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>

/*
  ESP32 AUTOMATED ACID-BASE TITRATION SYSTEM - WEB DASHBOARD VERSION

  Hardware:
  - ESP32 DevKit V1
  - 20x4 I2C LCD
  - Rotary encoder + push switch
  - Analog pH sensor/module + probe
  - 2 x ULN2003 + 28BYJ-48 5V stepper motors
  - 2 x limit switches used only as reverse travel safety stops
  - START and STOP physical push buttons
  - Manual Acid/Base dosing remains available from the web dashboard
  - 50mL syringe manual control in mL: Acid/Base Forward & Reverse
  - Reverse jog uses fixed high speed and stops after selected mL
  - Encoder rotation changes 20x4 LCD pages; encoder SW is not used
  - Automatic Acidic / Basic / Neutral liquid identification
  - Final analyte concentration calculation (1:1 acid-base stoichiometry)
  - Green / Yellow / Red LEDs + active buzzer
  - DS18B20 digital temperature sensor

  Web dashboard:
  - Live pH, target, status, Acid/Base volume
  - Set target pH
  - Start / Stop automatic titration
  - Manual Acid / Base dose
  - Reset counters/fault
  - Save calibration and pump settings
  - Live temperature monitoring
  - Excel-compatible CSV log download with date/time, pH, temperature, inputs, volumes and final concentration

  IMPORTANT ELECTRICAL NOTES:
  1. GPIO34 pH input MUST NEVER exceed 3.3V.
  2. GPIO35, GPIO36, GPIO39 need EXTERNAL 10k pull-ups to 3.3V.
     limit switch wiring for COM+NO: COM -> GND, NO -> GPIO, plus GPIO -> 10k -> 3.3V.
  3. 28BYJ-48 motors must use an external regulated 5V supply. COMMON GND with ESP32.
  4. This final map is for the 30-pin ESP32 DevKit V1 / ESP-WROOM-32 shown by the user.
  5. TX0(GPIO1) and RX0(GPIO3) are intentionally left free for reliable USB programming.
  6. GPIO2/4/5/12/15 are boot-strapping pins. Do not hold START/STOP during reset,
     and wire indicator LEDs only as shown in the wiring guide.
  7. DS18B20 DATA uses GPIO27 with a 4.7k pull-up to 3.3V.
  8. Both motor dosing directions are reversed from the previous version.
 10. First test syringe movement with WATER, not acid/base.
*/

// ========================= WIFI =========================
// Enter your Wi-Fi details here. If connection fails, ESP32 creates its own AP.
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* AP_SSID = "Titration-System";
const char* AP_PASSWORD = "12345678";

WebServer server(80);
Preferences prefs;

// ========================= LCD =========================
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ========================= PIN MAP =========================
// Board: 30-pin ESP32 DevKit V1 / ESP-WROOM-32
// Header labels visible on this board include VP(GPIO36), VN(GPIO39),
// D34, D35, D32, D33, D25, D26, D27, D14, D12, D13,
// D23, D22, TX0(GPIO1), RX0(GPIO3), D21, D19, D18, D5,
// TX2(GPIO17), RX2(GPIO16), D4, D2, D15.
// Acid ULN2003
const uint8_t ACID_IN1 = 13;
const uint8_t ACID_IN2 = 14;
const uint8_t ACID_IN3 = 16;
const uint8_t ACID_IN4 = 17;

// Base ULN2003
const uint8_t BASE_IN1 = 18;
const uint8_t BASE_IN2 = 19;
const uint8_t BASE_IN3 = 23;
const uint8_t BASE_IN4 = 25;

// I2C LCD: SDA=21, SCL=22

// Encoder
const uint8_t ENC_CLK = 32;
const uint8_t ENC_DT  = 33;
const uint8_t ENC_SW  = 36;  // EXTERNAL 10k pull-up

// pH ADC
const uint8_t PH_PIN = 34;

// DS18B20 temperature sensor
const uint8_t TEMP_PIN = 27;
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensors(&oneWire);

// Reverse travel limit switches - COM + NO wiring
// COM -> GND
// NO  -> GPIO35 / GPIO39
// 10k resistor from each GPIO to 3.3V
// Result: released = HIGH, pressed = LOW.
const uint8_t ACID_LIMIT_PIN = 35; // EXTERNAL 10k pull-up REQUIRED
const uint8_t BASE_LIMIT_PIN = 39; // EXTERNAL 10k pull-up REQUIRED
const uint8_t LIMIT_ACTIVE_LEVEL = HIGH;

// Local push buttons
const uint8_t START_BTN = 4;
const uint8_t STOP_BTN  = 5;

// Indicators / buzzer
// GPIO2/12/15 are exposed on this 30-pin board and used only as outputs.
// Wire each LED from GPIO -> 1k resistor -> LED -> GND.
const uint8_t LED_GREEN  = 2;
const uint8_t LED_YELLOW = 15;
const uint8_t LED_RED    = 12;
const uint8_t BUZZER_PIN = 26;

// ========================= STEPPERS =========================
// AccelStepper HALF4WIRE order 1-3-2-4 suits common ULN2003 + 28BYJ-48 boards.
AccelStepper acidStepper(AccelStepper::HALF4WIRE, ACID_IN1, ACID_IN3, ACID_IN2, ACID_IN4);
AccelStepper baseStepper(AccelStepper::HALF4WIRE, BASE_IN1, BASE_IN3, BASE_IN2, BASE_IN4);

// ========================= SETTINGS =========================
float acidStepsPerMl = 400.0f; // 4000 steps = 10 mL
float baseStepsPerMl = 400.0f; // 4000 steps = 10 mL
const float SYRINGE_CAPACITY_ML = 50.0f;
float phCalVoltage = 2.50f;
float phAtCal = 7.00f;
float phPerVolt = -5.70f;
float phVoltageCorrection = 1.0f;

float targetPH = 7.00f;
float phTolerance = 0.03f;
float manualDoseMl = 0.10f;
float maxAcidMl = 50.0f;
float maxBaseMl = 50.0f;

// Titration calculation inputs
float titrantConcentration = 0.1000f; // mol dm^-3
float analyteVolumeMl = 25.00f;       // mL
float finalAnalyteConcentration = NAN;
float finalTitrantUsedMl = 0.0f;

// Manual motor speed control
float manualMotorSpeed = 350.0f;      // Forward/manual speed
const float REVERSE_HIGH_SPEED = 700.0f;   // Reverse always uses high speed

const float TARGET_STEP = 0.05f;
const unsigned long MIX_WAIT_MS = 2500;
const unsigned long HOMING_TIMEOUT_MS = 20000;
const float STEPPER_MAX_SPEED = 700.0f;
const float STEPPER_ACCEL = 350.0f;
const float JOG_SPEED = 450.0f;

// ========================= MOTOR DIRECTION =========================
// Final requested direction: both dosing directions are REVERSED
// compared with the previous version.
// +1 or -1 can be changed individually later if a pump is mechanically opposite.
const int8_t ACID_DISPENSE_DIR = -1;
const int8_t BASE_DISPENSE_DIR = -1;

// Reverse jog direction is opposite to dispensing.
const int8_t ACID_REVERSE_DIR = -ACID_DISPENSE_DIR;
const int8_t BASE_REVERSE_DIR = -BASE_DISPENSE_DIR;

// ========================= RUNTIME =========================
enum SystemState {
  ST_READY,
  ST_AUTO_DECIDE,
  ST_DISPENSE_ACID,
  ST_DISPENSE_BASE,
  ST_MIXING,
  ST_MANUAL_ACID,
  ST_MANUAL_BASE,
  ST_JOG_ACID,
  ST_JOG_BASE,
  ST_COMPLETE,
  ST_STOPPED,
  ST_FAULT
};

SystemState state = ST_READY;
float currentPH = 7.00f;
float phVoltage = 0.0f;
float acidUsedMl = 0.0f;
float baseUsedMl = 0.0f;
float pendingDoseMl = 0.0f;

unsigned long titrationStartMs = 0;
unsigned long stateStartMs = 0;
unsigned long lastDisplayMs = 0;
unsigned long lastButtonMs = 0;
unsigned long lastPhSampleMs = 0;
unsigned long lastEncoderMs = 0;

int lastEncCLK = HIGH;


// Web jog runtime
bool jogHomeward = false;
long jogStepsRequested = 0;

// Starting analyte classification for final concentration calculation
enum LiquidType {
  LIQUID_ACIDIC,
  LIQUID_NEUTRAL,
  LIQUID_BASIC
};
LiquidType initialAnalyteType = LIQUID_NEUTRAL;

// LCD page navigation
uint8_t lcdPage = 0;
const uint8_t LCD_PAGE_COUNT = 5;


// DS18B20 temperature
float currentTempC = NAN;
unsigned long lastTempRequestMs = 0;
bool tempConversionPending = false;
const unsigned long TEMP_SAMPLE_MS = 1500;

// RAM data logger - 30 minutes at 2-second intervals.
struct LogRecord {
  time_t timestamp;
  uint32_t elapsed_s;
  float ph;
  float temp_c;
  float acid_ml;
  float base_ml;
  float titrant_used_ml;
  float titrant_conc;
  float analyte_volume_ml;
  float analyte_conc;
  uint8_t liquid_type_id;
  uint8_t state_id;
};
const uint16_t MAX_LOG_RECORDS = 900;
LogRecord logRecords[MAX_LOG_RECORDS];
uint16_t logCount = 0;
unsigned long lastLogMs = 0;
const unsigned long LOG_INTERVAL_MS = 2000;
bool loggingActive = false;

// pH rolling average
const int PH_AVG_SAMPLES = 20;
uint16_t phRawBuffer[PH_AVG_SAMPLES];
int phBufIndex = 0;
uint32_t phRawSum = 0;
bool phBufferFilled = false;

// ========================= WEB PAGE =========================
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Automated Titration</title>
<style>
*{box-sizing:border-box}body{margin:0;font-family:Arial,sans-serif;background:#0b1220;color:#e8eef8}.wrap{max-width:1050px;margin:auto;padding:18px}.top{display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap}.title{font-size:24px;font-weight:700}.pill{padding:8px 13px;border-radius:20px;background:#17233a;font-size:13px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px;margin-top:16px}.card{background:#121d31;border:1px solid #233552;border-radius:14px;padding:16px;box-shadow:0 5px 18px #0003}.label{color:#9fb0c9;font-size:13px}.value{font-size:32px;font-weight:700;margin-top:5px}.small{font-size:14px;color:#b8c5d8}.row{display:flex;gap:9px;flex-wrap:wrap;align-items:center}.btn{border:0;border-radius:10px;padding:12px 15px;font-weight:700;cursor:pointer;background:#2a6df4;color:white}.btn.stop{background:#d33c4b}.btn.alt{background:#2c3a52}.btn.warn{background:#d78b22}.btn.good{background:#19945d}.section{margin-top:14px}.section h3{font-size:16px;margin:0 0 11px}input{background:#0d1728;color:white;border:1px solid #334865;border-radius:8px;padding:10px;width:110px}.wide{width:150px}.status{font-size:20px;font-weight:700}.bar{height:8px;background:#22324c;border-radius:5px;overflow:hidden;margin-top:8px}.bar i{display:block;height:100%;background:#2a6df4;width:0%}.note{font-size:12px;color:#8295b2;line-height:1.5}.footer{margin:18px 0 4px;color:#7184a2;font-size:12px}.err{color:#ff8e98}.ok{color:#61d99d}
</style></head><body><div class="wrap">
<div class="top"><div class="title">Automated Titration System</div><div class="pill" id="net">ESP32 Dashboard</div></div>
<div class="grid">
 <div class="card"><div class="label">CURRENT pH</div><div class="value" id="ph">--</div><div class="small">Voltage: <span id="phv">--</span> V | <b id="liquid">--</b></div></div>
 <div class="card"><div class="label">TEMPERATURE</div><div class="value"><span id="temp">--</span> <span class="small">°C</span></div><div class="small">DS18B20 sensor</div></div>
 <div class="card"><div class="label">TARGET pH</div><div class="value" id="target">--</div><div class="row"><input id="targetIn" type="number" min="0" max="14" step="0.01"><button class="btn" onclick="setTarget()">SET</button></div></div>
 <div class="card"><div class="label">SYSTEM STATUS</div><div class="status" id="status">Loading...</div><div class="small" id="detail">-</div></div>
 <div class="card"><div class="label">TITRATION TIME</div><div class="value" id="elapsed">0 s</div><div class="small">Endpoint tolerance: ±<span id="tol">--</span></div></div>
 <div class="card"><div class="label">FINAL ANALYTE CONCENTRATION</div><div class="value"><span id="finalConc">--</span></div><div class="small">mol dm<sup>-3</sup> | Titrant used: <span id="titrantUsed">0.00</span> mL</div></div>
</div>
<div class="grid">
 <div class="card"><div class="label">ACID USED</div><div class="value"><span id="acid">0.00</span> <span class="small">mL</span></div></div>
 <div class="card"><div class="label">BASE USED</div><div class="value"><span id="base">0.00</span> <span class="small">mL</span></div></div>
</div>
<div class="card section"><h3>Titration Calculation Inputs</h3><div class="row">
 <label class="small">Titrant Concentration (mol dm-3)</label><input class="wide" id="titrantConc" type="number" min="0.000001" step="0.0001">
 <label class="small">Analyte Volume (mL)</label><input class="wide" id="analyteVol" type="number" min="0.001" step="0.01">
 <button class="btn alt" onclick="saveCalcInputs()">SAVE INPUTS</button>
</div><p class="note">Final analyte concentration assumes a 1:1 acid-base reaction: C(analyte) = C(titrant) x V(titrant) / V(analyte).</p></div>
<div class="card section"><h3>Data Logging / Excel Export</h3><div class="row">
 <a class="btn good" style="text-decoration:none" href="/download.csv">DOWNLOAD EXCEL CSV</a>
 <button class="btn alt" onclick="cmd('/api/clearlog')">CLEAR LOG</button>
</div><p class="note">The CSV opens directly in Microsoft Excel and contains date, time, pH, temperature, liquid type, Acid/Base volumes, titration inputs, titrant used, final analyte concentration and system state.</p></div>
<div class="card section"><h3>Automatic Titration</h3><div class="row">
 <button class="btn good" onclick="cmd('/api/start')">START AUTO</button>
 <button class="btn stop" onclick="cmd('/api/stop')">STOP / E-STOP</button>
 <button class="btn alt" onclick="cmd('/api/reset')">RESET / READY</button>
</div></div>
<div class="card section"><h3>Manual Syringe Control</h3><div class="row">
 <label class="small">Dose (mL)</label><input id="dose" type="number" min="0.01" max="50" step="0.01" value="1.00">
 <label class="small">Speed (steps/s)</label><input id="manualSpeed" type="number" min="50" max="700" step="10" value="350">
 <button class="btn warn" onclick="manual('acid')">DISPENSE ACID</button>
 <button class="btn" onclick="manual('base')">DISPENSE BASE</button>
</div><p class="note">Manual dosing is accepted only while the system is READY.</p></div>

<div class="card section"><h3>Manual Motor Forward / Reverse</h3><div class="row">
 <label class="small">Move (mL)</label><input id="jogMl" type="number" min="0.01" max="50" step="0.01" value="1.00">
 <label class="small">Speed (steps/s)</label><input id="jogSpeed" type="number" min="50" max="700" step="10" value="350">
 <button class="btn warn" onclick="jog('acid','forward')">ACID FORWARD</button>
 <button class="btn alt" onclick="jog('acid','reverse')">ACID REVERSE</button>
 <button class="btn" onclick="jog('base','forward')">BASE FORWARD</button>
 <button class="btn alt" onclick="jog('base','reverse')">BASE REVERSE</button>
 <button class="btn stop" onclick="cmd('/api/stop')">STOP MOTORS</button>
</div><p class="note">50 mL syringe mode: enter the required movement in mL. FORWARD uses the selected speed. REVERSE always uses HIGH SPEED (700 steps/s). Both directions stop automatically after the selected mL movement. Reverse also stops immediately if the related limit switch is activated.</p></div>
<div class="card section"><h3>Calibration & Pump Settings</h3><div class="row">
 <label class="small">Acid steps/mL</label><input class="wide" id="aspm" type="number" step="1">
 <label class="small">Base steps/mL</label><input class="wide" id="bspm" type="number" step="1">
 <label class="small">pH cal voltage</label><input id="pcv" type="number" step="0.001">
 <label class="small">pH at cal</label><input id="pac" type="number" step="0.01">
 <label class="small">pH / volt slope</label><input id="ppv" type="number" step="0.01">
 <label class="small">Voltage correction</label><input id="vcor" type="number" step="0.001">
 <button class="btn alt" onclick="saveSettings()">SAVE SETTINGS</button>
</div><p class="note">Do not guess calibration values for chemical testing. Calibrate the pH probe/module with certified buffer solutions and calibrate each syringe pump by measured dispensed volume.</p></div>
<div class="footer">ESP32 local controls remain active: LCD, encoder, START/STOP, limit switches, indicators and buzzer.</div>
</div><script>
let first=true;
async function jsonFetch(url,opt){let r=await fetch(url,opt);let j=await r.json();if(!r.ok)throw new Error(j.message||'Request failed');return j}
async function refresh(){try{let s=await jsonFetch('/api/status');
 document.getElementById('ph').textContent=Number(s.ph).toFixed(2);document.getElementById('phv').textContent=Number(s.voltage).toFixed(3);document.getElementById('temp').textContent=(s.temp_c===null?'--':Number(s.temp_c).toFixed(2));document.getElementById('liquid').textContent=s.liquid_type;
 document.getElementById('target').textContent=Number(s.target).toFixed(2);document.getElementById('acid').textContent=Number(s.acid_ml).toFixed(2);document.getElementById('base').textContent=Number(s.base_ml).toFixed(2);
 document.getElementById('status').textContent=s.state;document.getElementById('detail').textContent=s.detail;document.getElementById('elapsed').textContent=s.elapsed_s+' s';document.getElementById('tol').textContent=Number(s.tolerance).toFixed(2);document.getElementById('net').textContent=s.network+' | '+s.ip;
 document.getElementById('finalConc').textContent=(s.final_analyte_concentration===null?'--':Number(s.final_analyte_concentration).toFixed(6));document.getElementById('titrantUsed').textContent=Number(s.titrant_used_ml).toFixed(3);
 if(first){targetIn.value=s.target;aspm.value=s.acid_steps_ml;bspm.value=s.base_steps_ml;pcv.value=s.ph_cal_voltage;pac.value=s.ph_at_cal;ppv.value=s.ph_per_volt;vcor.value=s.voltage_correction;titrantConc.value=s.titrant_concentration;analyteVol.value=s.analyte_volume_ml;manualSpeed.value=s.manual_speed;jogSpeed.value=s.manual_speed;first=false}
 }catch(e){document.getElementById('status').textContent='Disconnected';document.getElementById('detail').textContent=e.message}}
async function cmd(path){try{await jsonFetch(path,{method:'POST'});setTimeout(refresh,120)}catch(e){alert(e.message)}}
async function setTarget(){try{await jsonFetch('/api/target?value='+encodeURIComponent(targetIn.value),{method:'POST'});refresh()}catch(e){alert(e.message)}}
async function manual(which){try{await jsonFetch('/api/manual?pump='+which+'&ml='+encodeURIComponent(dose.value)+'&speed='+encodeURIComponent(manualSpeed.value),{method:'POST'});refresh()}catch(e){alert(e.message)}}
async function jog(pump,dir){try{await jsonFetch('/api/jog?pump='+pump+'&dir='+dir+'&ml='+encodeURIComponent(jogMl.value)+'&speed='+encodeURIComponent(jogSpeed.value),{method:'POST'});refresh()}catch(e){alert(e.message)}}
async function saveCalcInputs(){let q=new URLSearchParams({titrant_concentration:titrantConc.value,analyte_volume_ml:analyteVol.value});try{await jsonFetch('/api/calcinputs?'+q.toString(),{method:'POST'});alert('Titration inputs saved');refresh()}catch(e){alert(e.message)}}
async function saveSettings(){let q=new URLSearchParams({acid_steps_ml:aspm.value,base_steps_ml:bspm.value,ph_cal_voltage:pcv.value,ph_at_cal:pac.value,ph_per_volt:ppv.value,voltage_correction:vcor.value});try{await jsonFetch('/api/settings?'+q.toString(),{method:'POST'});alert('Settings saved in ESP32 memory');refresh()}catch(e){alert(e.message)}}
setInterval(refresh,600);refresh();
</script></body></html>
)rawliteral";

// ========================= HELPERS =========================
bool pressed(uint8_t pin) { return digitalRead(pin) == LOW; }

// COM + NO limit switch debounce.
// With the required external 10k pull-up: released=HIGH, pressed=LOW.
bool limitActive(uint8_t pin) {
  uint8_t activeCount = 0;
  for (uint8_t i = 0; i < 5; i++) {
    if (digitalRead(pin) == LIMIT_ACTIVE_LEVEL) activeCount++;
    delayMicroseconds(250);
  }
  return activeCount >= 4;
}


LiquidType classifyLiquid(float ph) {
  if (ph < 6.95f) return LIQUID_ACIDIC;
  if (ph > 7.05f) return LIQUID_BASIC;
  return LIQUID_NEUTRAL;
}

const char* liquidTypeName(LiquidType t) {
  switch (t) {
    case LIQUID_ACIDIC: return "ACIDIC";
    case LIQUID_BASIC: return "BASIC";
    default: return "NEUTRAL";
  }
}

String currentDateString(time_t ts = 0) {
  if (ts == 0) time(&ts);
  if (ts < 100000) return "UNSYNCED";
  struct tm timeinfo;
  localtime_r(&ts, &timeinfo);
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

String currentTimeString(time_t ts = 0) {
  if (ts == 0) time(&ts);
  if (ts < 100000) return "UNSYNCED";
  struct tm timeinfo;
  localtime_r(&ts, &timeinfo);
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

void calculateFinalAnalyteConcentration() {
  if (analyteVolumeMl <= 0.0f || titrantConcentration <= 0.0f) {
    finalAnalyteConcentration = NAN;
    finalTitrantUsedMl = 0.0f;
    return;
  }

  if (initialAnalyteType == LIQUID_ACIDIC) {
    finalTitrantUsedMl = baseUsedMl;
  } else if (initialAnalyteType == LIQUID_BASIC) {
    finalTitrantUsedMl = acidUsedMl;
  } else {
    finalTitrantUsedMl = 0.0f;
    finalAnalyteConcentration = NAN;
    return;
  }

  // 1:1 stoichiometry. mL cancels because both volumes use the same unit.
  finalAnalyteConcentration =
      titrantConcentration * finalTitrantUsedMl / analyteVolumeMl;
}

void setIndicators(bool green, bool yellow, bool red) {
  digitalWrite(LED_GREEN, green ? HIGH : LOW);
  digitalWrite(LED_YELLOW, yellow ? HIGH : LOW);
  digitalWrite(LED_RED, red ? HIGH : LOW);
}

void beep(uint16_t ms = 100) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

void allStepperOff() {
  acidStepper.disableOutputs();
  baseStepper.disableOutputs();
}

void emergencyStop(const char* why) {
  acidStepper.disableOutputs();
  baseStepper.disableOutputs();
  pendingDoseMl = 0;
  state = ST_STOPPED;
  loggingActive = false;
  stateStartMs = millis();
  setIndicators(false, false, true);
}

void setFault(const char* why) {
  acidStepper.disableOutputs();
  baseStepper.disableOutputs();
  pendingDoseMl = 0;
  state = ST_FAULT;
  loggingActive = false;
  stateStartMs = millis();
  setIndicators(false, false, true);
  beep(180);
}

const char* stateName() {
  switch (state) {
    case ST_READY: return "READY";
    case ST_AUTO_DECIDE: return "AUTO RUNNING";
    case ST_DISPENSE_ACID: return "DISPENSING ACID";
    case ST_DISPENSE_BASE: return "DISPENSING BASE";
    case ST_MIXING: return "MIXING / SETTLING";
    case ST_MANUAL_ACID: return "MANUAL ACID";
    case ST_MANUAL_BASE: return "MANUAL BASE";
    case ST_JOG_ACID: return "JOG ACID";
    case ST_JOG_BASE: return "JOG BASE";
    case ST_COMPLETE: return "COMPLETE";
    case ST_STOPPED: return "STOPPED";
    case ST_FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

String stateDetail() {
  switch (state) {
    case ST_READY: return "Ready for command";
    case ST_AUTO_DECIDE: return "Checking pH and choosing next dose";
    case ST_DISPENSE_ACID: return "Acid dose in progress";
    case ST_DISPENSE_BASE: return "Base dose in progress";
    case ST_MIXING: return "Waiting for solution to stabilize";
    case ST_MANUAL_ACID: return "Manual Acid dose in progress";
    case ST_MANUAL_BASE: return "Manual Base dose in progress";
    case ST_JOG_ACID: return jogHomeward ? "Acid reverse jog" : "Acid forward jog";
    case ST_JOG_BASE: return jogHomeward ? "Base reverse jog" : "Base forward jog";
    case ST_COMPLETE: return "Target pH reached";
    case ST_STOPPED: return "Motors disabled - reset when safe";
    case ST_FAULT: return "Safety fault - inspect system";
  }
  return "";
}

bool isBusy() {
  return          state == ST_AUTO_DECIDE || state == ST_DISPENSE_ACID ||
         state == ST_DISPENSE_BASE || state == ST_MIXING ||
         state == ST_MANUAL_ACID || state == ST_MANUAL_BASE ||
         state == ST_JOG_ACID || state == ST_JOG_BASE;
}

// ========================= PREFERENCES =========================
void loadSettings() {
  prefs.begin("titrator", true);
  acidStepsPerMl = prefs.getFloat("aspm", acidStepsPerMl);
  baseStepsPerMl = prefs.getFloat("bspm", baseStepsPerMl);
  phCalVoltage = prefs.getFloat("pcv", phCalVoltage);
  phAtCal = prefs.getFloat("pac", phAtCal);
  phPerVolt = prefs.getFloat("ppv", phPerVolt);
  phVoltageCorrection = prefs.getFloat("vcor", phVoltageCorrection);
  targetPH = prefs.getFloat("target", targetPH);
  titrantConcentration = prefs.getFloat("tconc", titrantConcentration);
  analyteVolumeMl = prefs.getFloat("avol", analyteVolumeMl);
  manualMotorSpeed = prefs.getFloat("mspeed", manualMotorSpeed);
  prefs.end();
}

void saveCalibrationSettings() {
  prefs.begin("titrator", false);
  prefs.putFloat("aspm", acidStepsPerMl);
  prefs.putFloat("bspm", baseStepsPerMl);
  prefs.putFloat("pcv", phCalVoltage);
  prefs.putFloat("pac", phAtCal);
  prefs.putFloat("ppv", phPerVolt);
  prefs.putFloat("vcor", phVoltageCorrection);
  prefs.putFloat("target", targetPH);
  prefs.putFloat("tconc", titrantConcentration);
  prefs.putFloat("avol", analyteVolumeMl);
  prefs.putFloat("mspeed", manualMotorSpeed);
  prefs.end();
}

// ========================= pH SAMPLING =========================
void updatePH() {
  if (millis() - lastPhSampleMs < 25) return;
  lastPhSampleMs = millis();

  uint16_t raw = analogRead(PH_PIN);
  phRawSum -= phRawBuffer[phBufIndex];
  phRawBuffer[phBufIndex] = raw;
  phRawSum += raw;
  phBufIndex++;
  if (phBufIndex >= PH_AVG_SAMPLES) {
    phBufIndex = 0;
    phBufferFilled = true;
  }

  int count = phBufferFilled ? PH_AVG_SAMPLES : max(phBufIndex, 1);
  float avgRaw = (float)phRawSum / count;
  phVoltage = (avgRaw / 4095.0f) * 3.3f * phVoltageCorrection;
  currentPH = phAtCal + (phVoltage - phCalVoltage) * phPerVolt;
  currentPH = constrain(currentPH, 0.0f, 14.0f);
}

// ========================= TEMPERATURE =========================
void updateTemperature() {
  unsigned long now = millis();

  if (!tempConversionPending && now - lastTempRequestMs >= TEMP_SAMPLE_MS) {
    tempSensors.requestTemperatures();
    lastTempRequestMs = now;
    tempConversionPending = true;
    return;
  }

  // DS18B20 at 10-bit resolution needs about 188 ms.
  if (tempConversionPending && now - lastTempRequestMs >= 200) {
    float t = tempSensors.getTempCByIndex(0);
    if (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f) {
      currentTempC = t;
    }
    tempConversionPending = false;
  }
}

// ========================= DATA LOGGING =========================
void clearLog() {
  logCount = 0;
  lastLogMs = 0;
}

void addLogRecord() {
  if (!loggingActive || logCount >= MAX_LOG_RECORDS) return;
  if (millis() - lastLogMs < LOG_INTERVAL_MS && logCount > 0) return;
  lastLogMs = millis();

  LogRecord &r = logRecords[logCount++];
  time(&r.timestamp);
  r.elapsed_s = titrationStartMs ? (millis() - titrationStartMs) / 1000UL : 0;
  r.ph = currentPH;
  r.temp_c = currentTempC;
  r.acid_ml = acidUsedMl;
  r.base_ml = baseUsedMl;
  if (initialAnalyteType == LIQUID_ACIDIC) r.titrant_used_ml = baseUsedMl;
  else if (initialAnalyteType == LIQUID_BASIC) r.titrant_used_ml = acidUsedMl;
  else r.titrant_used_ml = 0.0f;
  r.titrant_conc = titrantConcentration;
  r.analyte_volume_ml = analyteVolumeMl;
  r.analyte_conc = finalAnalyteConcentration;
  r.liquid_type_id = (uint8_t)classifyLiquid(currentPH);
  r.state_id = (uint8_t)state;
}

const char* stateNameFromId(uint8_t id) {
  switch ((SystemState)id) {
    case ST_READY: return "READY";
    case ST_AUTO_DECIDE: return "AUTO RUNNING";
    case ST_DISPENSE_ACID: return "DISPENSING ACID";
    case ST_DISPENSE_BASE: return "DISPENSING BASE";
    case ST_MIXING: return "MIXING / SETTLING";
    case ST_MANUAL_ACID: return "MANUAL ACID";
    case ST_MANUAL_BASE: return "MANUAL BASE";
    case ST_JOG_ACID: return "JOG ACID";
    case ST_JOG_BASE: return "JOG BASE";
    case ST_COMPLETE: return "COMPLETE";
    case ST_STOPPED: return "STOPPED";
    case ST_FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

void handleCsvDownload() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=titration_full_report.csv");
  server.send(200, "text/csv", "");

  server.sendContent(
    "Record,Date,Time,Elapsed_s,pH,Liquid_Type,Temperature_C,"
    "Target_pH,Acid_mL,Base_mL,Titrant_Concentration_mol_dm-3,"
    "Analyte_Volume_mL,Titrant_Used_mL,"
    "Final_Analyte_Concentration_mol_dm-3,State\r\n"
  );

  for (uint16_t i = 0; i < logCount; i++) {
    const LogRecord &r = logRecords[i];

    String line;
    line.reserve(240);

    line = String(i + 1);
    line += ",";
    line += currentDateString(r.timestamp);
    line += ",";
    line += currentTimeString(r.timestamp);
    line += ",";
    line += String(r.elapsed_s);
    line += ",";
    line += String(r.ph, 3);
    line += ",";
    line += liquidTypeName((LiquidType)r.liquid_type_id);
    line += ",";

    if (!isnan(r.temp_c)) {
      line += String(r.temp_c, 2);
    }

    line += ",";
    line += String(targetPH, 3);
    line += ",";
    line += String(r.acid_ml, 3);
    line += ",";
    line += String(r.base_ml, 3);
    line += ",";
    line += String(r.titrant_conc, 6);
    line += ",";
    line += String(r.analyte_volume_ml, 3);
    line += ",";
    line += String(r.titrant_used_ml, 3);
    line += ",";

    if (!isnan(r.analyte_conc)) {
      line += String(r.analyte_conc, 8);
    }

    line += ",";
    line += stateNameFromId(r.state_id);
    line += "\r\n";

    server.sendContent(line);
    delay(0);
  }

  server.sendContent("");
}

// ========================= DOSE LOGIC =========================
float chooseDose(float errorAbs) {
  if (errorAbs > 2.00f) return 0.20f;
  if (errorAbs > 1.00f) return 0.10f;
  if (errorAbs > 0.30f) return 0.05f;
  if (errorAbs > 0.10f) return 0.02f;
  return 0.01f;
}

bool endpointReached() {
  return fabsf(currentPH - targetPH) <= phTolerance;
}

bool startAcidDose(float ml, bool manualMode = false, float speed = STEPPER_MAX_SPEED) {
  if (ml <= 0 || ml > SYRINGE_CAPACITY_ML) return false;
  if (acidUsedMl + ml > maxAcidMl) { setFault("Acid maximum volume reached"); return false; }
  pendingDoseMl = ml;
  long steps = lroundf(ml * acidStepsPerMl) * ACID_DISPENSE_DIR;
  acidStepper.setMaxSpeed(constrain(speed, 50.0f, STEPPER_MAX_SPEED));
  acidStepper.enableOutputs();
  acidStepper.move(steps);
  state = manualMode ? ST_MANUAL_ACID : ST_DISPENSE_ACID;
  stateStartMs = millis();
  setIndicators(false, true, false);
  return true;
}

bool startBaseDose(float ml, bool manualMode = false, float speed = STEPPER_MAX_SPEED) {
  if (ml <= 0 || ml > SYRINGE_CAPACITY_ML) return false;
  if (baseUsedMl + ml > maxBaseMl) { setFault("Base maximum volume reached"); return false; }
  pendingDoseMl = ml;
  long steps = lroundf(ml * baseStepsPerMl) * BASE_DISPENSE_DIR;
  baseStepper.setMaxSpeed(constrain(speed, 50.0f, STEPPER_MAX_SPEED));
  baseStepper.enableOutputs();
  baseStepper.move(steps);
  state = manualMode ? ST_MANUAL_BASE : ST_DISPENSE_BASE;
  stateStartMs = millis();
  setIndicators(false, true, false);
  return true;
}


bool startJog(const String& pump, const String& dir, float ml, float speed) {
  if (state != ST_READY) return false;
  if (ml <= 0.0f || ml > SYRINGE_CAPACITY_ML) return false;

  bool forward = (dir == "forward");
  bool reverse = (dir == "reverse");
  if (!forward && !reverse) return false;

  jogHomeward = reverse;

  float selectedSpeed = forward
                        ? constrain(speed, 50.0f, STEPPER_MAX_SPEED)
                        : REVERSE_HIGH_SPEED;

  if (pump == "acid") {
    if (reverse && limitActive(ACID_LIMIT_PIN)) {
      acidStepper.disableOutputs();
      return false;
    }

    long steps = lroundf(ml * acidStepsPerMl);
    if (steps < 1) steps = 1;
    jogStepsRequested = steps;

    long signedSteps = steps * (forward ? ACID_DISPENSE_DIR : ACID_REVERSE_DIR);
    acidStepper.setMaxSpeed(selectedSpeed);
    acidStepper.setAcceleration(STEPPER_ACCEL);
    acidStepper.enableOutputs();
    acidStepper.move(signedSteps);
    state = ST_JOG_ACID;

  } else if (pump == "base") {
    if (reverse && limitActive(BASE_LIMIT_PIN)) {
      baseStepper.disableOutputs();
      return false;
    }

    long steps = lroundf(ml * baseStepsPerMl);
    if (steps < 1) steps = 1;
    jogStepsRequested = steps;

    long signedSteps = steps * (forward ? BASE_DISPENSE_DIR : BASE_REVERSE_DIR);
    baseStepper.setMaxSpeed(selectedSpeed);
    baseStepper.setAcceleration(STEPPER_ACCEL);
    baseStepper.enableOutputs();
    baseStepper.move(signedSteps);
    state = ST_JOG_BASE;

  } else {
    return false;
  }

  stateStartMs = millis();
  setIndicators(false, true, false);
  return true;
}

void beginAuto() {
  if (isBusy()) return;
  acidUsedMl = 0;
  baseUsedMl = 0;
  clearLog();
  loggingActive = true;
  titrationStartMs = millis();
  initialAnalyteType = classifyLiquid(currentPH);
  finalAnalyteConcentration = NAN;
  finalTitrantUsedMl = 0.0f;

  state = ST_AUTO_DECIDE;
  stateStartMs = millis();
  setIndicators(false, true, false);
  beep(60);
}

// ========================= STATE MACHINE =========================
void serviceStateMachine() {
  // Hardware STOP always has priority.
  if (pressed(STOP_BTN) && state != ST_STOPPED) {
    emergencyStop("Local STOP button");
    return;
  }

  switch (state) {
    case ST_READY:
    case ST_COMPLETE:
    case ST_STOPPED:
    case ST_FAULT:
      break;

    case ST_AUTO_DECIDE: {
      if (endpointReached()) {
        state = ST_COMPLETE;
        calculateFinalAnalyteConcentration();
        addLogRecord();
        loggingActive = false;
        setIndicators(true, false, false);
        beep(120); delay(80); beep(120);
        break;
      }
      float error = targetPH - currentPH;
      float dose = chooseDose(fabsf(error));
      if (error > 0) startBaseDose(dose, false);
      else startAcidDose(dose, false);
      break;
    }

    case ST_DISPENSE_ACID:
      acidStepper.run();
      if (acidStepper.distanceToGo() == 0) {
        acidStepper.disableOutputs();
        acidUsedMl += pendingDoseMl;
        pendingDoseMl = 0;
        state = ST_MIXING;
        stateStartMs = millis();
      }
      break;

    case ST_DISPENSE_BASE:
      baseStepper.run();
      if (baseStepper.distanceToGo() == 0) {
        baseStepper.disableOutputs();
        baseUsedMl += pendingDoseMl;
        pendingDoseMl = 0;
        state = ST_MIXING;
        stateStartMs = millis();
      }
      break;

    case ST_MIXING:
      if (millis() - stateStartMs >= MIX_WAIT_MS) {
        state = ST_AUTO_DECIDE;
        stateStartMs = millis();
      }
      break;

    case ST_JOG_ACID:
      // Reverse jog = toward HOME. Stop immediately when COM+NO switch closes.
      if (jogHomeward && limitActive(ACID_LIMIT_PIN)) {
        acidStepper.stop();
        acidStepper.setCurrentPosition(0);
        acidStepper.disableOutputs();
        state = ST_READY;
        setIndicators(true, false, false);
        beep(50);
      } else {
        acidStepper.run();
        if (acidStepper.distanceToGo() == 0) {
          acidStepper.disableOutputs();
          state = ST_READY;
          setIndicators(true, false, false);
          beep(50);
        }
      }
      break;

    case ST_JOG_BASE:
      if (jogHomeward && limitActive(BASE_LIMIT_PIN)) {
        baseStepper.stop();
        baseStepper.setCurrentPosition(0);
        baseStepper.disableOutputs();
        state = ST_READY;
        setIndicators(true, false, false);
        beep(50);
      } else {
        baseStepper.run();
        if (baseStepper.distanceToGo() == 0) {
          baseStepper.disableOutputs();
          state = ST_READY;
          setIndicators(true, false, false);
          beep(50);
        }
      }
      break;

    case ST_MANUAL_ACID:
      acidStepper.run();
      if (acidStepper.distanceToGo() == 0) {
        acidStepper.disableOutputs();
        acidUsedMl += pendingDoseMl;
        pendingDoseMl = 0;
        state = ST_READY;
        setIndicators(true, false, false);
        beep(60);
      }
      break;

    case ST_MANUAL_BASE:
      baseStepper.run();
      if (baseStepper.distanceToGo() == 0) {
        baseStepper.disableOutputs();
        baseUsedMl += pendingDoseMl;
        pendingDoseMl = 0;
        state = ST_READY;
        setIndicators(true, false, false);
        beep(60);
      }
      break;
  }
}

// ========================= LOCAL CONTROLS =========================
void handleEncoder() {
  // FINAL V6 BEHAVIOR:
  // - LCD page changes ONLY when encoder is physically rotated.
  // - Encoder SW is NOT used for LCD page changes.
  // - Encoder does NOT change target pH.
  // - Target pH is changed only from the Web Dashboard.

  static int lastClkState = HIGH;
  static unsigned long lastStepMs = 0;
  const unsigned long ROTARY_DEBOUNCE_MS = 4;

  int clkNow = digitalRead(ENC_CLK);

  // One detent/event is accepted on the falling edge of CLK.
  if (lastClkState == HIGH &&
      clkNow == LOW &&
      millis() - lastStepMs >= ROTARY_DEBOUNCE_MS) {

    int dtNow = digitalRead(ENC_DT);

    if (dtNow != clkNow) {
      // Clockwise -> next page
      lcdPage++;
      if (lcdPage >= LCD_PAGE_COUNT) {
        lcdPage = 0;
      }
    } else {
      // Counter-clockwise -> previous page
      if (lcdPage == 0) {
        lcdPage = LCD_PAGE_COUNT - 1;
      } else {
        lcdPage--;
      }
    }

    lastStepMs = millis();

    // Force immediate refresh on new page.
    lastDisplayMs = 0;
    updateLCD();
  }

  lastClkState = clkNow;
}

void handleLocalButtons() {
  // Encoder SW is intentionally ignored in FINAL V6.
  // LCD pages are controlled only by encoder rotation.

  if (millis() - lastButtonMs < 350) return;

  if (pressed(START_BTN) && !isBusy()) {
    lastButtonMs = millis();
    if (state == ST_FAULT || state == ST_STOPPED) {
      state = ST_READY;
      setIndicators(false, false, false);
      beep(60);
    } else {
      beginAuto();
    }
    return;
  }
}

// ========================= LCD =========================
void printPadded(int row, String text) {
  if (text.length() > 20) text = text.substring(0, 20);
  while (text.length() < 20) text += ' ';
  lcd.setCursor(0, row);
  lcd.print(text);
}

void updateLCD() {
  if (millis() - lastDisplayMs < 250) return;
  lastDisplayMs = millis();

  String tempText = isnan(currentTempC) ? "--.-" : String(currentTempC, 1);
  String typeText = liquidTypeName(classifyLiquid(currentPH));

  switch (lcdPage) {
    case 0:
      printPadded(0, "1 LIVE MONITOR");
      printPadded(1, "pH:" + String(currentPH, 2) + " " + typeText);
      printPadded(2, "Temp:" + tempText + "C");
      printPadded(3, String(stateName()));
      break;

    case 1:
      printPadded(0, "2 TITRATION INPUT");
      printPadded(1, "Target:" + String(targetPH, 2));
      printPadded(2, "Ct:" + String(titrantConcentration, 4));
      printPadded(3, "Va:" + String(analyteVolumeMl, 2) + "mL");
      break;

    case 2: {
      printPadded(0, "3 PROCESS");
      printPadded(1, "Acid:" + String(acidUsedMl, 2) + "mL");
      printPadded(2, "Base:" + String(baseUsedMl, 2) + "mL");
      float tv = 0.0f;
      if (initialAnalyteType == LIQUID_ACIDIC) tv = baseUsedMl;
      else if (initialAnalyteType == LIQUID_BASIC) tv = acidUsedMl;
      printPadded(3, "Titrant:" + String(tv, 2) + "mL");
      break;
    }

    case 3:
      printPadded(0, "4 FINAL OUTPUT");
      printPadded(1, "Final pH:" + String(currentPH, 2));
      if (state == ST_COMPLETE && !isnan(finalAnalyteConcentration)) {
        printPadded(2, "C:" + String(finalAnalyteConcentration, 6));
        printPadded(3, "mol/dm3");
      } else {
        printPadded(2, "Analyte Conc:");
        printPadded(3, "PENDING");
      }
      break;

    case 4:
    default:
      printPadded(0, "5 NETWORK / INFO");
      printPadded(1, networkName());
      printPadded(2, currentIP());
      printPadded(3, currentDateString() + " " + currentTimeString());
      break;
  }
}

// ========================= WEB API =========================
void sendJsonMessage(bool ok, const String& message, int code = 200) {
  String json = String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + message + "\"}";
  server.send(code, "application/json", json);
}

String currentIP() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

String networkName() {
  if (WiFi.status() == WL_CONNECTED) return "WiFi";
  return "AP: " + String(AP_SSID);
}

void handleStatus() {
  unsigned long elapsed = (titrationStartMs > 0 && (isBusy() || state == ST_COMPLETE)) ? (millis() - titrationStartMs) / 1000UL : 0;
  String json = "{";
  json += "\"ph\":" + String(currentPH, 3) + ",";
  json += "\"voltage\":" + String(phVoltage, 4) + ",";
  if (isnan(currentTempC)) json += "\"temp_c\":null,";
  else json += "\"temp_c\":" + String(currentTempC, 2) + ",";
  json += "\"target\":" + String(targetPH, 2) + ",";
  json += "\"liquid_type\":\"" + String(liquidTypeName(classifyLiquid(currentPH))) + "\",";
  json += "\"initial_analyte_type\":\"" + String(liquidTypeName(initialAnalyteType)) + "\",";
  json += "\"acid_ml\":" + String(acidUsedMl, 3) + ",";
  json += "\"base_ml\":" + String(baseUsedMl, 3) + ",";
  json += "\"titrant_concentration\":" + String(titrantConcentration, 6) + ",";
  json += "\"analyte_volume_ml\":" + String(analyteVolumeMl, 3) + ",";
  json += "\"titrant_used_ml\":" + String(finalTitrantUsedMl, 3) + ",";
  if (isnan(finalAnalyteConcentration)) json += "\"final_analyte_concentration\":null,";
  else json += "\"final_analyte_concentration\":" + String(finalAnalyteConcentration, 8) + ",";
  json += "\"manual_speed\":" + String(manualMotorSpeed, 1) + ",";
  json += "\"reverse_high_speed\":" + String(REVERSE_HIGH_SPEED, 1) + ",";
  json += "\"syringe_capacity_ml\":" + String(SYRINGE_CAPACITY_ML, 1) + ",";
  json += "\"date\":\"" + currentDateString() + "\",";
  json += "\"time\":\"" + currentTimeString() + "\",";
  json += "\"acid_home\":" + String(limitActive(ACID_LIMIT_PIN) ? "true" : "false") + ",";
  json += "\"base_home\":" + String(limitActive(BASE_LIMIT_PIN) ? "true" : "false") + ",";
  json += "\"state\":\"" + String(stateName()) + "\",";
  json += "\"detail\":\"" + stateDetail() + "\",";
  json += "\"elapsed_s\":" + String(elapsed) + ",";
  json += "\"tolerance\":" + String(phTolerance, 3) + ",";
  json += "\"log_records\":" + String(logCount) + ",";
  json += "\"acid_steps_ml\":" + String(acidStepsPerMl, 1) + ",";
  json += "\"base_steps_ml\":" + String(baseStepsPerMl, 1) + ",";
  json += "\"ph_cal_voltage\":" + String(phCalVoltage, 4) + ",";
  json += "\"ph_at_cal\":" + String(phAtCal, 3) + ",";
  json += "\"ph_per_volt\":" + String(phPerVolt, 4) + ",";
  json += "\"voltage_correction\":" + String(phVoltageCorrection, 4) + ",";
  json += "\"network\":\"" + networkName() + "\",";
  json += "\"ip\":\"" + currentIP() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", DASHBOARD_HTML); });
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/download.csv", HTTP_GET, handleCsvDownload);
  server.on("/api/clearlog", HTTP_POST, []() {
    if (isBusy()) { sendJsonMessage(false, "Stop system before clearing log", 409); return; }
    clearLog();
    sendJsonMessage(true, "Log cleared");
  });

  server.on("/api/target", HTTP_POST, []() {
    if (!server.hasArg("value")) { sendJsonMessage(false, "Missing target value", 400); return; }
    float v = server.arg("value").toFloat();
    if (v < 0.0f || v > 14.0f) { sendJsonMessage(false, "Target must be 0-14", 400); return; }
    targetPH = v;
    saveCalibrationSettings();
    sendJsonMessage(true, "Target updated");
  });

  server.on("/api/start", HTTP_POST, []() {
    if (isBusy()) { sendJsonMessage(false, "System is busy", 409); return; }
    if (state == ST_FAULT || state == ST_STOPPED) { sendJsonMessage(false, "Reset system first", 409); return; }
    beginAuto();
    sendJsonMessage(true, "Automatic titration started");
  });

  server.on("/api/stop", HTTP_POST, []() {
    emergencyStop("Web dashboard STOP");
    sendJsonMessage(true, "System stopped");
  });

  server.on("/api/reset", HTTP_POST, []() {
    if (isBusy()) { sendJsonMessage(false, "Stop system before reset", 409); return; }
    acidStepper.disableOutputs(); baseStepper.disableOutputs();
    state = ST_READY; pendingDoseMl = 0; titrationStartMs = 0; loggingActive = false; finalAnalyteConcentration = NAN; finalTitrantUsedMl = 0.0f; allStepperOff(); setIndicators(true, false, false);
    setIndicators(false, false, false);
    sendJsonMessage(true, "System ready");
  });

  server.on("/api/manual", HTTP_POST, []() {
    if (state != ST_READY) { sendJsonMessage(false, "Manual dosing requires READY state", 409); return; }
    if (!server.hasArg("pump") || !server.hasArg("ml")) { sendJsonMessage(false, "Missing pump or volume", 400); return; }

    float ml = server.arg("ml").toFloat();
    float speed = server.hasArg("speed") ? server.arg("speed").toFloat() : manualMotorSpeed;

    if (ml <= 0 || ml > SYRINGE_CAPACITY_ML) { sendJsonMessage(false, "Dose must be >0 and <=50mL", 400); return; }
    if (speed < 50 || speed > STEPPER_MAX_SPEED) { sendJsonMessage(false, "Speed must be 50-700 steps/s", 400); return; }

    manualMotorSpeed = speed;
    String pump = server.arg("pump");
    bool ok = (pump == "acid") ? startAcidDose(ml, true, speed)
                               : (pump == "base" ? startBaseDose(ml, true, speed) : false);
    if (!ok) { sendJsonMessage(false, "Invalid pump or dose", 400); return; }
    sendJsonMessage(true, "Manual dose started");
  });

  server.on("/api/jog", HTTP_POST, []() {
    if (state != ST_READY) { sendJsonMessage(false, "Jog requires READY state", 409); return; }
    if (!server.hasArg("pump") || !server.hasArg("dir") || !server.hasArg("ml")) {
      sendJsonMessage(false, "Missing pump, direction or mL", 400); return;
    }

    String pump = server.arg("pump");
    String dir = server.arg("dir");
    float ml = server.arg("ml").toFloat();
    float speed = server.hasArg("speed") ? server.arg("speed").toFloat() : manualMotorSpeed;

    if (ml <= 0.0f || ml > SYRINGE_CAPACITY_ML) {
      sendJsonMessage(false, "Move must be >0 and <=50mL", 400); return;
    }
    if (dir == "forward" && (speed < 50 || speed > STEPPER_MAX_SPEED)) {
      sendJsonMessage(false, "Forward speed must be 50-700 steps/s", 400); return;
    }

    if (dir == "reverse") {
      if (pump == "acid" && limitActive(ACID_LIMIT_PIN)) {
        sendJsonMessage(false, "Acid reverse limit is active", 409); return;
      }
      if (pump == "base" && limitActive(BASE_LIMIT_PIN)) {
        sendJsonMessage(false, "Base reverse limit is active", 409); return;
      }
    }

    manualMotorSpeed = speed;
    if (!startJog(pump, dir, ml, speed)) {
      sendJsonMessage(false, "Unable to start motor move", 400); return;
    }

    sendJsonMessage(true, pump + " " + dir + " move started");
  });

  server.on("/api/calcinputs", HTTP_POST, []() {
    if (isBusy()) { sendJsonMessage(false, "Change inputs only when idle", 409); return; }
    if (!server.hasArg("titrant_concentration") || !server.hasArg("analyte_volume_ml")) {
      sendJsonMessage(false, "Missing titration inputs", 400); return;
    }

    float tc = server.arg("titrant_concentration").toFloat();
    float av = server.arg("analyte_volume_ml").toFloat();

    if (tc <= 0.0f || tc > 100.0f) {
      sendJsonMessage(false, "Invalid titrant concentration", 400); return;
    }
    if (av <= 0.0f || av > 10000.0f) {
      sendJsonMessage(false, "Invalid analyte volume", 400); return;
    }

    titrantConcentration = tc;
    analyteVolumeMl = av;
    finalAnalyteConcentration = NAN;
    finalTitrantUsedMl = 0.0f;
    saveCalibrationSettings();
    sendJsonMessage(true, "Titration calculation inputs saved");
  });

  server.on("/api/settings", HTTP_POST, []() {
    if (isBusy()) { sendJsonMessage(false, "Change settings only when idle", 409); return; }
    if (server.hasArg("acid_steps_ml")) acidStepsPerMl = max(server.arg("acid_steps_ml").toFloat(), 1.0f);
    if (server.hasArg("base_steps_ml")) baseStepsPerMl = max(server.arg("base_steps_ml").toFloat(), 1.0f);
    if (server.hasArg("ph_cal_voltage")) phCalVoltage = server.arg("ph_cal_voltage").toFloat();
    if (server.hasArg("ph_at_cal")) phAtCal = server.arg("ph_at_cal").toFloat();
    if (server.hasArg("ph_per_volt")) phPerVolt = server.arg("ph_per_volt").toFloat();
    if (server.hasArg("voltage_correction")) phVoltageCorrection = max(server.arg("voltage_correction").toFloat(), 0.01f);
    if (server.hasArg("manual_speed")) manualMotorSpeed = constrain(server.arg("manual_speed").toFloat(), 50.0f, STEPPER_MAX_SPEED);
    saveCalibrationSettings();
    sendJsonMessage(true, "Settings saved");
  });

  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

// ========================= WIFI =========================
void startNetwork() {
  bool useStation = strlen(WIFI_SSID) > 0 && String(WIFI_SSID) != "YOUR_WIFI_NAME";
  if (useStation) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
      updateLCD();
      delay(2);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Sri Lanka time UTC+5:30. Change GMT_OFFSET_SEC if deployed elsewhere.
    const long GMT_OFFSET_SEC = 19800;
    configTime(GMT_OFFSET_SEC, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
  }
}

// ========================= SETUP / LOOP =========================
void setup() {
  delay(250);

  pinMode(LED_GREEN, OUTPUT); pinMode(LED_YELLOW, OUTPUT); pinMode(LED_RED, OUTPUT); pinMode(BUZZER_PIN, OUTPUT);
  pinMode(START_BTN, INPUT_PULLUP); pinMode(STOP_BTN, INPUT_PULLUP);
  pinMode(ENC_CLK, INPUT_PULLUP); pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT); pinMode(ACID_LIMIT_PIN, INPUT); pinMode(BASE_LIMIT_PIN, INPUT); pinMode(PH_PIN, INPUT);

  digitalWrite(BUZZER_PIN, LOW); setIndicators(false, false, false);

  analogReadResolution(12);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  tempSensors.begin();
  tempSensors.setResolution(10);
  tempSensors.setWaitForConversion(false);

  acidStepper.setMaxSpeed(STEPPER_MAX_SPEED); acidStepper.setAcceleration(STEPPER_ACCEL);
  baseStepper.setMaxSpeed(STEPPER_MAX_SPEED); baseStepper.setAcceleration(STEPPER_ACCEL);
  allStepperOff();

  Wire.begin(21, 22);
  lcd.init(); lcd.backlight();
  lcd.clear(); printPadded(0, "AUTOMATED TITRATOR"); printPadded(1, "Web + START/STOP"); printPadded(2, "Starting...");

  loadSettings();
  memset(phRawBuffer, 0, sizeof(phRawBuffer));
  lastEncCLK = digitalRead(ENC_CLK);
  lcdPage = 0;

  // HOME FUNCTION REMOVED:
  // Motors do not move automatically at power-up/reset.
  // System starts in READY state.
  allStepperOff();
  state = ST_READY;
  setIndicators(true, false, false);

  startNetwork();
  setupWebServer();

  printPadded(2, networkName());
  printPadded(3, currentIP());
  beep(70);
  delay(250);
}

void loop() {
  server.handleClient();
  updatePH();
  updateTemperature();
  addLogRecord();
  handleEncoder();
  handleLocalButtons();
  serviceStateMachine();
  updateLCD();
  delay(1);
}
