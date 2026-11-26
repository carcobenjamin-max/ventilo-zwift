/*
  ============================================================================
   VENTILO ZWIFT — version BLUETOOTH
   Arduino Nano ESP32 + gradateur RobotDyn 8A (Z-C / Dim) + brasseur HF-45A
  ============================================================================

   CE QUI CHANGE PAR RAPPORT À LA VERSION WIFI
     - Plus de WiFi : le téléphone se connecte en Bluetooth (appli web Android).
     - 3 modes : MANUEL (curseur), CARDIO (ceinture BLE), PUISSANCE (home trainer BLE).
     - La Nano se connecte elle-même à la ceinture et/ou au home trainer :
       le téléphone n'est utile que pour régler. Les modes auto marchent sans lui.
     - Le pilotage du triac est STRICTEMENT celui de la version validée le 24/11.

   LIBRAIRIE À INSTALLER (gestionnaire de bibliothèques de l'IDE)
     - « NimBLE-Arduino » par h2zero (version 2.x). Compilation vérifiée en 2.5.2.

   CÂBLAGE : identique à la version WiFi
     3V3 -> VCC | GND -> GND | D2 -> Z-C | D3 -> Dim
     Option potentiomètre (USE_POT = 1) : 3V3 / GND / curseur sur A0

   SÉCURITÉ
     - Ne jamais brancher l'USB sur le PC quand le gradateur est sous tension.
     - Mise à jour du programme : boîtier débranché du secteur, puis USB.

   LED RGB DE LA NANO
     - Nom Bluetooth : Ventilo-Zwift-XXXX (XXXX propre à chaque carte, affiché au démarrage
       dans le moniteur série et en haut de l'appli)
     - Bleu clignotant : en attente du téléphone (les modes auto tournent quand même)
     - Vert            : téléphone connecté
     - Rouge           : sortie demandée mais pas de signal Z-C (secteur absent)

   IDE : Arduino IDE 2 + paquet "Arduino ESP32 Boards" (par Arduino)
         Carte : "Arduino Nano ESP32", Pin Numbering : "By Arduino pin (default)"
         Compilation vérifiée avec Arduino ESP32 Boards 2.0.17 + NimBLE-Arduino 2.5.2.
  ============================================================================
*/

#include <NimBLEDevice.h>
#include <Preferences.h>
#include <esp_arduino_version.h>
#include <math.h>

// ============================ NOM BLUETOOTH ================================
// Nom de base. Chaque boîtier ajoute 4 caractères tirés de sa puce (ex. Ventilo-Zwift-3FA2)
// pour qu'on distingue deux ventilos dans la liste du téléphone. Le même code sert pour tous.
const char* BLE_NAME = "Ventilo-Zwift";
char        bleName[24];

// ============================ BROCHAGE =====================================
const uint8_t PIN_ZC  = D2;   // Z-C du gradateur
const uint8_t PIN_DIM = D3;   // Dim du gradateur
#define USE_POT 0             // 1 = potentiomètre filaire sur A0 (passe en mode manuel)
const uint8_t PIN_POT = A0;

// ============================ RÉGLAGES MOTEUR ==============================
const uint8_t  MIN_PCT_DEFAULT = 35;     // minimum moteur par défaut (réglable dans l'appli)
const bool     RESTORE_ON_BOOT = true;   // mode manuel : reprend la dernière vitesse
const uint32_t KICK_MS         = 2000;   // démarrage à 100 % pendant 2 s
const uint32_t SAVE_DELAY_MS   = 3000;   // sauvegarde flash après 3 s sans changement

// ============================ RÉGULATION AUTO ==============================
const float    RAMP_UP_PCT_S   = 10.0f;  // montée maxi : 10 %/s
const float    RAMP_DOWN_PCT_S = 3.0f;   // descente maxi : 3 %/s (on reste ventilé après l'effort)
const uint32_t STALE_MS        = 5000;   // plus de mesure depuis 5 s : on garde la vitesse
const uint32_t LOST_STOP_MS    = 180000; // plus de mesure depuis 3 min : arrêt du ventilo
const uint8_t  AUTO_OFF_BELOW  = 3;      // en auto : arrêt sous 3 %...
const uint8_t  AUTO_ON_ABOVE   = 6;      // ...redémarrage au-dessus de 6 % (hystérésis)
const uint8_t  HR_AVG_S        = 5;      // lissage cardio : moyenne sur 5 s

// Pilotage triac (50 Hz) — inchangé
const uint32_t TICK_US         = 50;
const uint32_t HALF_PERIOD_US  = 10000;
const uint32_t ZC_DEBOUNCE_US  = 7000;
const uint32_t FIRE_MAX_US     = 8800;
const uint32_t GATE_PULSE_US   = 1500;
const uint32_t GATE_END_MAX_US = 9300;
const uint8_t  FULL_ON_PCT     = 97;
const int      ZC_EDGE         = RISING;

// ============================ UUID =========================================
// Service privé du ventilo (doivent correspondre à ceux de l'appli web)
#define SVC_UUID   "8f1d0001-7c3b-4e5a-9a2b-5f6e7a8b9c0d"
#define STATE_UUID "8f1d0002-7c3b-4e5a-9a2b-5f6e7a8b9c0d"   // lecture + notification
#define CMD_UUID   "8f1d0003-7c3b-4e5a-9a2b-5f6e7a8b9c0d"   // écriture commandes
#define CFG_UUID   "8f1d0004-7c3b-4e5a-9a2b-5f6e7a8b9c0d"   // lecture / écriture réglages
#define SCAN_UUID  "8f1d0005-7c3b-4e5a-9a2b-5f6e7a8b9c0d"   // notification résultats de recherche

// Services standard des capteurs
static const NimBLEUUID UUID_HRS ((uint16_t)0x180D), UUID_HRM ((uint16_t)0x2A37);  // cardio
static const NimBLEUUID UUID_CPS ((uint16_t)0x1818), UUID_CPM ((uint16_t)0x2A63);  // puissance
static const NimBLEUUID UUID_FTMS((uint16_t)0x1826), UUID_IBD ((uint16_t)0x2AD2);  // home trainer FTMS

// Commandes reçues du téléphone (1er octet)
enum : uint8_t {
  CMD_MANUAL = 0x01,   // [v]           vitesse manuelle 0..100 (passe en mode manuel)
  CMD_MODE   = 0x02,   // [m]           0 manuel, 1 cardio, 2 puissance
  CMD_MIN    = 0x03,   // [m]           minimum moteur 0..80
  CMD_SCAN   = 0x10,   // [slot]        recherche capteurs (0 cardio, 1 puissance)
  CMD_SCANOFF= 0x11,
  CMD_SENSOR = 0x20,   // [slot][type][6 octets adresse]
  CMD_FORGET = 0x21    // [slot]
};
enum : uint8_t { MODE_MANUAL = 0, MODE_HR = 1, MODE_POWER = 2 };

// ============================ ÉTAT TRIAC ===================================
enum : uint32_t { OUT_OFF = 0, OUT_PHASE = 1, OUT_FULL = 2 };
volatile uint32_t gCtrl     = 0;
volatile uint16_t gTicks    = 0xFFFF;
volatile uint32_t gLastZcUs = 0;
volatile uint32_t gZcCount  = 0;
volatile bool     gGate     = false;
hw_timer_t*       tickTimer = nullptr;
uint16_t          delayTableUs[101];

// ============================ ÉTAT GÉNÉRAL =================================
Preferences prefs;

struct Config {              // réglages modifiables depuis l'appli (9 octets sur le BLE)
  uint8_t  hrLo   = 110;     // FC où le ventilo est au "bas de plage"
  uint8_t  hrHi   = 165;     // FC où il est à 100 %
  uint16_t pLo    = 100;     // puissance (W) bas de plage
  uint16_t pHi    = 280;     // puissance (W) à 100 %
  uint8_t  lowPct = 20;      // vitesse en bas de plage (0 = arrêt)
  uint8_t  avgSec = 10;      // lissage puissance (s)
} cfg;

uint8_t  mode       = MODE_MANUAL;
uint8_t  manualPct  = 0;
uint8_t  minPct     = MIN_PCT_DEFAULT;
float    autoF      = 0;     // consigne auto après rampe
uint8_t  uiPct      = 0;     // consigne appliquée 0..100 (échelle curseur)
uint8_t  outPct     = 0;     // sortie gradateur réelle
uint8_t  lastOut    = 255;
bool     kicking    = false;
uint32_t kickUntil  = 0;
bool     dirty      = false;
uint32_t dirtyAt    = 0;
bool     zcOk       = false;
uint32_t lastZcSeen = 0, lastZcCnt = 0;

// ============================ CAPTEURS =====================================
struct Slot {
  uint8_t           addr[6] = {0};
  uint8_t           type    = 0xFF;   // 0xFF = aucun capteur mémorisé
  volatile uint32_t gen     = 0;      // change quand on choisit un autre capteur
  uint32_t          usedGen = 0;
  NimBLEClient*     client  = nullptr;
  volatile bool     connected = false;
  volatile bool     isFtms  = false;
  volatile uint16_t value   = 0;      // bpm ou watts (instantané)
  volatile uint32_t lastMs  = 0;      // heure de la dernière mesure
  uint32_t          lastTry = 0;
};
Slot slots[2];                         // 0 = cardio, 1 = puissance
portMUX_TYPE slotMux = portMUX_INITIALIZER_UNLOCKED;

volatile bool    scanRequest   = false;
volatile bool    scanStopReq   = false;
volatile uint8_t scanSlot      = 0;
volatile bool    scanning      = false;

// Moyennes glissantes (échantillon toutes les 250 ms)
const int RING = 4 * 30;
uint16_t  hrRing[RING],  pwRing[RING];
int       hrIdx = 0, hrN = 0, pwIdx = 0, pwN = 0;
uint16_t  hrAvg = 0, pwAvg = 0;

// ============================ BLE SERVEUR ==================================
NimBLEServer*         server   = nullptr;
NimBLECharacteristic* chState  = nullptr;
NimBLECharacteristic* chCmd    = nullptr;
NimBLECharacteristic* chCfg    = nullptr;
NimBLECharacteristic* chScan   = nullptr;
volatile uint16_t     phoneHandle = BLE_HS_CONN_HANDLE_NONE;

struct Msg { uint8_t src; uint8_t len; uint8_t d[23]; };   // src : 0 commande, 1 réglages, 2 recherche
QueueHandle_t cmdQueue;    // téléphone -> loop
QueueHandle_t scanQueue;   // résultats de recherche -> loop -> téléphone

// ============================ INTERRUPTIONS (inchangé) =====================
static void IRAM_ATTR gateSet(bool on) {
  if (on != gGate) { gGate = on; digitalWrite(PIN_DIM, on ? HIGH : LOW); }
}

void IRAM_ATTR onZeroCross() {
  uint32_t now = micros();
  if ((uint32_t)(now - gLastZcUs) < ZC_DEBOUNCE_US) return;
  gLastZcUs = now;
  gZcCount = gZcCount + 1;
  gTicks = 0;
  if (((gCtrl >> 16) & 0xFF) != OUT_FULL) gateSet(false);
}

void IRAM_ATTR onTick() {
  uint32_t c = gCtrl, m = (c >> 16) & 0xFF;
  if (m == OUT_OFF) { gateSet(false); return; }
  if (gTicks < 0xFFFF) gTicks = gTicks + 1;
  if (gTicks > (2 * HALF_PERIOD_US / TICK_US)) { gateSet(false); return; }
  if (m == OUT_FULL) { gateSet(true); return; }
  uint32_t fire = (c >> 8) & 0xFF, end = c & 0xFF;
  gateSet(gTicks >= fire && gTicks < end);
}

// ============================ PILOTAGE (inchangé) ==========================
void buildDelayTable() {
  for (int p = 0; p <= 100; p++) {
    double target = p / 100.0, lo = 0, hi = M_PI;
    for (int i = 0; i < 40; i++) {
      double a = (lo + hi) / 2;
      double v = 1.0 - a / M_PI + sin(2 * a) / (2 * M_PI);
      v = v > 0 ? sqrt(v) : 0;
      if (v > target) lo = a; else hi = a;
    }
    uint32_t d = (uint32_t)(((lo + hi) / 2) / M_PI * HALF_PERIOD_US);
    delayTableUs[p] = d > FIRE_MAX_US ? FIRE_MAX_US : d;
  }
}

void applyOutput(uint8_t pct) {
  uint32_t c;
  if (pct == 0)                c = OUT_OFF << 16;
  else if (pct >= FULL_ON_PCT) c = OUT_FULL << 16;
  else {
    uint32_t d = delayTableUs[pct], fire = d / TICK_US;
    if (fire < 1) fire = 1;
    uint32_t endUs = d + GATE_PULSE_US;
    if (endUs > GATE_END_MAX_US) endUs = GATE_END_MAX_US;
    uint32_t end = endUs / TICK_US;
    if (end <= fire) end = fire + 1;
    c = (OUT_PHASE << 16) | (fire << 8) | end;
  }
  gCtrl = c;
  analogWrite(LED_BUILTIN, (uint32_t)pct * 255 / 100);
}

uint8_t uiToOut(uint8_t ui) {
  if (ui == 0) return 0;
  return minPct + (uint32_t)(100 - minPct) * (ui - 1) / 99;
}

void startTickTimer() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  tickTimer = timerBegin(1000000);
  timerAttachInterrupt(tickTimer, &onTick);
  timerAlarm(tickTimer, TICK_US, true, 0);
#else
  tickTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(tickTimer, &onTick, true);
  timerAlarmWrite(tickTimer, TICK_US, true);
  timerAlarmEnable(tickTimer);
#endif
}

// Applique une consigne (échelle curseur) avec démarrage à 100 % depuis l'arrêt
void setUi(uint8_t v) {
  v = v > 100 ? 100 : v;
  if (uiPct == 0 && v > 0 && KICK_MS > 0) { kicking = true; kickUntil = millis() + KICK_MS; }
  if (v == 0) kicking = false;
  uiPct = v;
}

void updateOutput() {
  uint8_t out = uiToOut(uiPct);
  if (kicking) {
    if ((int32_t)(millis() - kickUntil) < 0) out = 100;
    else kicking = false;
  }
  outPct = out;
  if (out != lastOut) { applyOutput(out); lastOut = out; }
}

void markDirty() { dirty = true; dirtyAt = millis(); }

// ============================ RÉGULATION ===================================
static float mapRange(float x, float lo, float hi, float outLo) {
  if (hi <= lo) hi = lo + 1;
  float k = (x - lo) / (hi - lo);
  k = k < 0 ? 0 : (k > 1 ? 1 : k);
  return outLo + k * (100.0f - outLo);
}

static uint16_t ringAvg(const uint16_t* r, int idx, int n, int want) {
  if (n == 0) return 0;
  if (want > n) want = n;
  uint32_t s = 0;
  for (int i = 1; i <= want; i++) s += r[(idx - i + RING) % RING];
  return s / want;
}

void sampleSensors() {               // toutes les 250 ms
  uint32_t now = millis();
  for (int s = 0; s < 2; s++) {
    Slot& sl = slots[s];
    bool fresh = sl.connected && sl.lastMs && (now - sl.lastMs) < STALE_MS;
    if (!fresh) { if (s == 0) hrN = 0; else pwN = 0; continue; }   // pas de vieilles valeurs dans la moyenne
    if (s == 0) { hrRing[hrIdx] = sl.value; hrIdx = (hrIdx + 1) % RING; if (hrN < RING) hrN++; }
    else        { pwRing[pwIdx] = sl.value; pwIdx = (pwIdx + 1) % RING; if (pwN < RING) pwN++; }
  }
  hrAvg = ringAvg(hrRing, hrIdx, hrN, HR_AVG_S * 4);
  pwAvg = ringAvg(pwRing, pwIdx, pwN, constrain(cfg.avgSec, 1, 30) * 4);
}

void regulate(float dt) {            // toutes les 100 ms
  if (mode == MODE_MANUAL) { if (uiPct != manualPct) setUi(manualPct); autoF = uiPct; return; }

  Slot& sl = slots[mode == MODE_HR ? 0 : 1];
  uint32_t age = sl.lastMs ? millis() - sl.lastMs : UINT32_MAX;
  float target;
  if (sl.connected && age < STALE_MS) {
    target = (mode == MODE_HR) ? mapRange(hrAvg, cfg.hrLo, cfg.hrHi, cfg.lowPct)
                               : mapRange(pwAvg, cfg.pLo,  cfg.pHi,  cfg.lowPct);
  } else if (age < LOST_STOP_MS) {
    target = autoF;                  // mesure perdue depuis peu : on garde la vitesse
  } else {
    target = 0;                      // plus de mesure depuis 3 min : arrêt
  }

  float step = (target > autoF ? RAMP_UP_PCT_S : RAMP_DOWN_PCT_S) * dt;
  if (fabsf(target - autoF) <= step) autoF = target;
  else autoF += (target > autoF) ? step : -step;

  int v = (int)lroundf(autoF);
  if (uiPct == 0) { if (v < AUTO_ON_ABOVE) v = 0; }        // hystérésis arrêt/marche
  else if (v < AUTO_OFF_BELOW) v = 0;
  if (v != uiPct) setUi(v);
}

// ============================ PERSISTANCE ==================================
void saveAll() {
  prefs.putUChar("mode", mode);
  prefs.putUChar("man", manualPct);
  prefs.putUChar("min", minPct);
  prefs.putBytes("cfg", &cfg, sizeof cfg);
  for (int s = 0; s < 2; s++) {
    uint8_t b[7]; b[0] = slots[s].type; memcpy(b + 1, slots[s].addr, 6);
    prefs.putBytes(s ? "pwA" : "hrA", b, 7);
  }
}

void loadAll() {
  mode      = prefs.getUChar("mode", MODE_MANUAL); if (mode > MODE_POWER) mode = MODE_MANUAL;
  manualPct = prefs.getUChar("man", 0);
  minPct    = prefs.getUChar("min", MIN_PCT_DEFAULT);
  if (prefs.getBytesLength("cfg") == sizeof cfg) prefs.getBytes("cfg", &cfg, sizeof cfg);
  for (int s = 0; s < 2; s++) {
    uint8_t b[7];
    if (prefs.getBytes(s ? "pwA" : "hrA", b, 7) == 7) { slots[s].type = b[0]; memcpy(slots[s].addr, b + 1, 6); }
  }
}

// ============================ PAQUETS BLE ==================================
void buildState(uint8_t* p) {      // 12 octets
  uint32_t now = millis();
  bool hrFresh = slots[0].connected && slots[0].lastMs && now - slots[0].lastMs < STALE_MS;
  bool pwFresh = slots[1].connected && slots[1].lastMs && now - slots[1].lastMs < STALE_MS;
  p[0] = mode;
  p[1] = uiPct;
  p[2] = outPct;
  p[3] = manualPct;
  p[4] = (zcOk << 0) | (kicking << 1) | (slots[0].connected << 2) | (slots[1].connected << 3) |
         (scanning << 4) | (hrFresh << 5) | (pwFresh << 6);
  p[5] = ((slots[0].type != 0xFF) << 0) | ((slots[1].type != 0xFF) << 1) | (slots[1].isFtms << 2) |
         (scanSlot << 3);
  uint16_t hr = slots[0].value;
  p[6] = hrFresh ? (uint8_t)(hr > 255 ? 255 : hr) : 0;
  uint16_t pi = pwFresh ? slots[1].value : 0;
  p[7] = pi & 0xFF; p[8] = pi >> 8;
  uint16_t pa = pwFresh ? pwAvg : 0;
  p[9] = pa & 0xFF; p[10] = pa >> 8;
  p[11] = minPct;
}

void buildCfg(uint8_t* p) {        // 23 octets
  p[0] = 1;                        // version
  p[1] = cfg.hrLo; p[2] = cfg.hrHi;
  p[3] = cfg.pLo & 0xFF; p[4] = cfg.pLo >> 8;
  p[5] = cfg.pHi & 0xFF; p[6] = cfg.pHi >> 8;
  p[7] = cfg.lowPct; p[8] = cfg.avgSec;
  p[9] = slots[0].type;  memcpy(p + 10, slots[0].addr, 6);
  p[16] = slots[1].type; memcpy(p + 17, slots[1].addr, 6);
}

void refreshCfgChar() {
  uint8_t b[23]; buildCfg(b);
  chCfg->setValue(b, sizeof b);
}

// ============================ CALLBACKS SERVEUR ============================
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& ci) override {
    phoneHandle = ci.getConnHandle();
    Serial.printf("Téléphone connecté (%s)\n", ci.getAddress().toString().c_str());
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& ci, int reason) override {
    phoneHandle = BLE_HS_CONN_HANDLE_NONE;
    Serial.printf("Téléphone déconnecté (%d)\n", reason);
    NimBLEDevice::startAdvertising();
  }
} serverCB;

class CmdCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& ci) override {
    NimBLEAttValue v = c->getValue();
    Msg m; m.src = (c == chCfg) ? 1 : 0;
    m.len = v.size() > sizeof m.d ? sizeof m.d : v.size();
    memcpy(m.d, v.data(), m.len);
    if (m.len) xQueueSend(cmdQueue, &m, 0);
  }
} cmdCB;

// ============================ CAPTEURS : RECHERCHE =========================
class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    uint8_t kind = 0;
    if (d->isAdvertisingService(UUID_HRS))  kind |= 1;
    if (d->isAdvertisingService(UUID_CPS))  kind |= 2;
    if (d->isAdvertisingService(UUID_FTMS)) kind |= 4;
    if (!kind) return;
    Msg m; m.src = 2;
    std::string name = d->haveName() ? d->getName() : "";
    if (name.size() > 14) name.resize(14);
    m.d[0] = kind;
    m.d[1] = (uint8_t)(int8_t)d->getRSSI();
    m.d[2] = d->getAddress().getType();
    memcpy(m.d + 3, d->getAddress().getVal(), 6);
    memcpy(m.d + 9, name.data(), name.size());
    m.len = 9 + name.size();
    xQueueSend(scanQueue, &m, 0);
  }
  void onScanEnd(const NimBLEScanResults& r, int reason) override {
    scanning = false;
    Serial.printf("Recherche terminée (%d appareils)\n", r.getCount());
  }
} scanCB;

// ============================ CAPTEURS : CONNEXION =========================
static int slotOfClient(NimBLEClient* c) {
  for (int s = 0; s < 2; s++) if (slots[s].client == c) return s;
  return -1;
}

class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient* c, int reason) override {
    int s = slotOfClient(c);
    if (s >= 0) { slots[s].connected = false; Serial.printf("Capteur %s déconnecté (%d)\n", s ? "puissance" : "cardio", reason); }
  }
} clientCB;

static void onHr(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  if (n < 2) return;
  uint16_t bpm = (d[0] & 0x01) ? (n >= 3 ? (d[1] | (d[2] << 8)) : 0) : d[1];
  if (bpm == 0 || bpm > 250) return;
  slots[0].value = bpm; slots[0].lastMs = millis();
}

static void onCp(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {   // Cycling Power
  if (n < 4) return;
  int16_t w = (int16_t)(d[2] | (d[3] << 8));
  slots[1].value = w < 0 ? 0 : w; slots[1].lastMs = millis();
}

static void onIbd(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {  // FTMS Indoor Bike Data
  if (n < 2) return;
  uint16_t f = d[0] | (d[1] << 8);
  size_t i = 2;
  if (!(f & 0x0001)) i += 2;   // vitesse instantanée
  if (f & 0x0002) i += 2;      // vitesse moyenne
  if (f & 0x0004) i += 2;      // cadence instantanée
  if (f & 0x0008) i += 2;      // cadence moyenne
  if (f & 0x0010) i += 3;      // distance totale
  if (f & 0x0020) i += 2;      // niveau de résistance
  if (!(f & 0x0040) || i + 2 > n) return;   // pas de puissance dans ce paquet
  int16_t w = (int16_t)(d[i] | (d[i + 1] << 8));
  slots[1].value = w < 0 ? 0 : w; slots[1].lastMs = millis();
}

static bool subscribeSlot(int s, NimBLEClient* c) {
  if (s == 0) {
    NimBLERemoteService* sv = c->getService(UUID_HRS);
    NimBLERemoteCharacteristic* ch = sv ? sv->getCharacteristic(UUID_HRM) : nullptr;
    return ch && ch->canNotify() && ch->subscribe(true, onHr);
  }
  NimBLERemoteService* sv = c->getService(UUID_CPS);            // 1er choix : Cycling Power
  NimBLERemoteCharacteristic* ch = sv ? sv->getCharacteristic(UUID_CPM) : nullptr;
  if (ch && ch->canNotify() && ch->subscribe(true, onCp)) { slots[1].isFtms = false; return true; }
  sv = c->getService(UUID_FTMS);                                 // sinon : FTMS
  ch = sv ? sv->getCharacteristic(UUID_IBD) : nullptr;
  if (ch && ch->canNotify() && ch->subscribe(true, onIbd)) { slots[1].isFtms = true; return true; }
  return false;
}

static void manageSlot(int s) {
  Slot& sl = slots[s];
  uint8_t addr[6], type; uint32_t gen;
  portENTER_CRITICAL(&slotMux);
  memcpy(addr, sl.addr, 6); type = sl.type; gen = sl.gen;
  portEXIT_CRITICAL(&slotMux);

  // Capteur changé ou oublié : on lâche l'ancien
  if (sl.client && sl.usedGen != gen) {
    if (sl.client->isConnected()) sl.client->disconnect();
    sl.connected = false;
    sl.lastMs = 0;
    sl.usedGen = gen;
    sl.lastTry = 0;
  }
  if (type == 0xFF) return;
  if (sl.client && sl.client->isConnected()) { sl.connected = true; return; }
  sl.connected = false;
  if (scanning || millis() - sl.lastTry < 5000) return;
  sl.lastTry = millis();

  if (!sl.client) {
    sl.client = NimBLEDevice::createClient();
    sl.client->setClientCallbacks(&clientCB, false);
    sl.client->setConnectTimeout(4000);
    sl.client->setConnectionParams(24, 40, 0, 300);   // 30–50 ms, tolérant avec 3 liaisons
  }
  NimBLEAddress a(addr, type);
  if (!sl.client->connect(a)) return;                  // capteur éteint ou déjà pris
  if (subscribeSlot(s, sl.client)) {
    sl.connected = true;
    Serial.printf("Capteur %s connecté : %s%s\n", s ? "puissance" : "cardio",
                  a.toString().c_str(), (s && sl.isFtms) ? " (FTMS)" : "");
  } else {
    Serial.printf("Capteur %s : service introuvable\n", s ? "puissance" : "cardio");
    sl.client->disconnect();
  }
}

void sensorTask(void*) {
  for (;;) {
    if (scanStopReq) { scanStopReq = false; NimBLEDevice::getScan()->stop(); scanning = false; }
    if (scanRequest) {
      scanRequest = false;
      NimBLEScan* sc = NimBLEDevice::getScan();
      sc->stop();
      sc->clearResults();
      sc->setScanCallbacks(&scanCB, false);
      sc->setActiveScan(true);
      sc->setInterval(100);
      sc->setWindow(60);
      scanning = sc->start(10000, false, true);        // 10 s
    }
    manageSlot(0);
    manageSlot(1);
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ============================ COMMANDES ====================================
void handleMsg(const Msg& m) {
  const uint8_t* d = m.d;
  if (m.src == 1) {                                    // écriture des réglages
    if (m.len >= 9 && d[0] == 1) {
      cfg.hrLo = d[1]; cfg.hrHi = max<uint8_t>(d[2], d[1] + 5);
      cfg.pLo  = d[3] | (d[4] << 8);
      cfg.pHi  = max<uint16_t>(d[5] | (d[6] << 8), cfg.pLo + 10);
      cfg.lowPct = min<uint8_t>(d[7], 100);
      cfg.avgSec = constrain(d[8], 1, 30);
      markDirty();
    }
    refreshCfgChar();
    return;
  }
  switch (d[0]) {
    case CMD_MANUAL: if (m.len >= 2) { manualPct = min<uint8_t>(d[1], 100); mode = MODE_MANUAL; markDirty(); } break;
    case CMD_MODE:
      if (m.len >= 2 && d[1] <= MODE_POWER && d[1] != mode) {
        mode = d[1];
        autoF = uiPct;                                 // pas de saut au changement de mode
        if (mode == MODE_MANUAL) manualPct = uiPct;
        markDirty();
      }
      break;
    case CMD_MIN:    if (m.len >= 2) { minPct = min<uint8_t>(d[1], 80); lastOut = 255; markDirty(); } break;
    case CMD_SCAN:   if (m.len >= 2) { scanSlot = d[1] ? 1 : 0; scanRequest = true; scanning = true; } break;
    case CMD_SCANOFF: scanStopReq = true; break;
    case CMD_SENSOR:
      if (m.len >= 9 && d[1] <= 1) {
        Slot& sl = slots[d[1]];
        portENTER_CRITICAL(&slotMux);
        sl.type = d[2]; memcpy(sl.addr, d + 3, 6); sl.gen = sl.gen + 1;
        portEXIT_CRITICAL(&slotMux);
        scanStopReq = true;
        markDirty(); refreshCfgChar();
      }
      break;
    case CMD_FORGET:
      if (m.len >= 2 && d[1] <= 1) {
        Slot& sl = slots[d[1]];
        portENTER_CRITICAL(&slotMux);
        sl.type = 0xFF; memset(sl.addr, 0, 6); sl.gen = sl.gen + 1;
        portEXIT_CRITICAL(&slotMux);
        markDirty(); refreshCfgChar();
      }
      break;
  }
}

// ============================ LED D'ÉTAT ===================================
void setRgb(bool r, bool g, bool b) {
  digitalWrite(LED_RED, r ? LOW : HIGH);
  digitalWrite(LED_GREEN, g ? LOW : HIGH);
  digitalWrite(LED_BLUE, b ? LOW : HIGH);
}

void updateStatusLed() {
  if (outPct > 0 && !zcOk)                        setRgb(true, false, false);
  else if (phoneHandle != BLE_HS_CONN_HANDLE_NONE) setRgb(false, true, false);
  else                                             setRgb(false, false, (millis() / 500) % 2);
}

// ============================ POTENTIOMÈTRE (option) =======================
#if USE_POT
void readPot() {
  static uint32_t t = 0;
  static float ema = -1;
  static int lastPotPct = -100;
  if (millis() - t < 50) return;
  t = millis();
  int raw = analogRead(PIN_POT);
  ema = (ema < 0) ? raw : ema * 0.8f + raw * 0.2f;
  int p = constrain((int)lroundf((ema - 80.0f) * 100.0f / (4095.0f - 160.0f)), 0, 100);
  if (lastPotPct == -100) { lastPotPct = p; return; }          // pas de prise de main au démarrage
  if (abs(p - lastPotPct) >= 3 || (p == 0 && lastPotPct != 0)) {
    lastPotPct = p; manualPct = p; mode = MODE_MANUAL; markDirty();
  }
}
#endif

// ============================ SETUP / LOOP =================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_DIM, OUTPUT);
  digitalWrite(PIN_DIM, LOW);
  pinMode(PIN_ZC, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_RED, OUTPUT); pinMode(LED_GREEN, OUTPUT); pinMode(LED_BLUE, OUTPUT);
  setRgb(false, false, false);
#if USE_POT
  analogReadResolution(12);
#endif

  buildDelayTable();
  applyOutput(0);
  attachInterrupt(digitalPinToInterrupt(PIN_ZC), onZeroCross, ZC_EDGE);
  startTickTimer();

  prefs.begin("ventilo", false);
  loadAll();

  cmdQueue  = xQueueCreate(16, sizeof(Msg));
  scanQueue = xQueueCreate(24, sizeof(Msg));

  uint64_t mac = ESP.getEfuseMac();
  snprintf(bleName, sizeof bleName, "%s-%02X%02X", BLE_NAME, (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  NimBLEDevice::init(bleName);
  NimBLEDevice::setMTU(185);
  server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCB);
  NimBLEService* svc = server->createService(SVC_UUID);
  chState = svc->createCharacteristic(STATE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  chCmd   = svc->createCharacteristic(CMD_UUID,   NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  chCfg   = svc->createCharacteristic(CFG_UUID,   NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  chScan  = svc->createCharacteristic(SCAN_UUID,  NIMBLE_PROPERTY::NOTIFY);
  chCmd->setCallbacks(&cmdCB);
  chCfg->setCallbacks(&cmdCB);
  server->start();
  refreshCfgChar();
  uint8_t st[12]; buildState(st); chState->setValue(st, sizeof st);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->enableScanResponse(true);      // AVANT setName : le nom part dans la réponse de scan
  adv->setName(bleName);             // (l'UUID 128 bits remplit déjà l'annonce principale)
  adv->addServiceUUID(SVC_UUID);
  adv->start();

  xTaskCreatePinnedToCore(sensorTask, "capteurs", 6144, nullptr, 1, nullptr, 0);

  if (mode == MODE_MANUAL && RESTORE_ON_BOOT && manualPct > 0)
    Serial.printf("Reprise de la dernière vitesse : %u %%\n", manualPct);
  if (mode == MODE_MANUAL && !RESTORE_ON_BOOT) manualPct = 0;

  Serial.printf("Bluetooth prêt : \"%s\" — mode %u\n", bleName, mode);
}

void loop() {
  uint32_t now = millis();

  Msg m;
  while (xQueueReceive(cmdQueue, &m, 0) == pdTRUE) handleMsg(m);

  // Résultats de recherche -> téléphone
  while (xQueueReceive(scanQueue, &m, 0) == pdTRUE) {
    uint16_t h = phoneHandle;
    if (h == BLE_HS_CONN_HANDLE_NONE) continue;
    uint16_t room = server->getPeerMTU(h);
    room = room > 3 ? room - 3 : 20;
    chScan->notify(m.d, m.len < room ? m.len : room, h);
  }

#if USE_POT
  readPot();
#endif

  static uint32_t tSample = 0, tReg = 0, tNotify = 0, tLed = 0;
  if (now - tSample >= 250) { tSample = now; sampleSensors(); }
  if (now - tReg >= 100)    { float dt = tReg ? (now - tReg) / 1000.0f : 0.1f; tReg = now; regulate(dt); }
  updateOutput();

  // Surveillance du passage à zéro
  uint32_t cnt = gZcCount;
  if (cnt != lastZcCnt) { lastZcCnt = cnt; lastZcSeen = now; }
  zcOk = (now - lastZcSeen) < 300 && lastZcSeen != 0;

  // État -> téléphone (2 fois par seconde)
  if (now - tNotify >= 500) {
    tNotify = now;
    uint8_t st[12]; buildState(st);
    chState->setValue(st, sizeof st);
    if (phoneHandle != BLE_HS_CONN_HANDLE_NONE) chState->notify();
  }

  if (dirty && now - dirtyAt > SAVE_DELAY_MS) { saveAll(); dirty = false; }
  if (now - tLed > 250) { tLed = now; updateStatusLed(); }

  delay(5);
}
