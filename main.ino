#include <Arduino.h>
#include <DHT.h>
#include <ArduinoJson.h>

// ===== PIN CONFIG =====
#define DHT_PIN     4
#define LED_GREEN   2
#define LED_RED     15
#define DHT_TYPE    DHT22

DHT dht(DHT_PIN, DHT_TYPE);

// ===== BLOCKCHAIN STRUCT =====
struct Block {
  uint32_t index;
  String   timestamp;
  String   sensorData;   // JSON payload
  String   prevHash;
  String   hash;
  uint32_t nonce;
};

// ===== CHAIN STORAGE (max 10 blok di RAM) =====
const int MAX_BLOCKS = 10;
Block chain[MAX_BLOCKS];
int   chainLen = 0;

// ===== SIMPLE HASH (32-bit XOR fold) =====
String computeHash(uint32_t index, String ts, String data,
                   String prevHash, uint32_t nonce) {
  String raw = String(index) + ts + data + prevHash + String(nonce);
  uint32_t h = 5381;
  for (int i = 0; i < (int)raw.length(); i++) {
    h = ((h << 5) + h) ^ (uint8_t)raw[i];
  }
  // Buat hex string 8-digit
  char buf[9];
  sprintf(buf, "%08X", h);
  return String(buf);
}

// ===== BUAT BLOK BARU =====
Block createBlock(uint32_t idx, String prevHash, String data) {
  Block b;
  b.index      = idx;
  b.timestamp  = String(millis());
  b.sensorData = data;
  b.prevHash   = prevHash;
  b.nonce      = random(0, 99999);
  b.hash       = computeHash(b.index, b.timestamp, b.sensorData,
                              b.prevHash, b.nonce);
  return b;
}

// ===== GENESIS BLOCK =====
void initChain() {
  Block genesis;
  genesis.index      = 0;
  genesis.timestamp  = "0";
  genesis.sensorData = "{\"type\":\"genesis\"}";
  genesis.prevHash   = "00000000";
  genesis.nonce      = 0;
  genesis.hash       = computeHash(0, "0", genesis.sensorData,
                                   "00000000", 0);
  chain[0] = genesis;
  chainLen = 1;
  Serial.println("=== GENESIS BLOCK DIBUAT ===");
  printBlock(chain[0]);
}

// ===== TAMBAH BLOK =====
bool addBlock(String sensorJson) {
  if (chainLen >= MAX_BLOCKS) {
    Serial.println("[WARN] Chain penuh! Reset diperlukan.");
    return false;
  }
  String prevHash = chain[chainLen - 1].hash;
  Block nb = createBlock(chainLen, prevHash, sensorJson);
  chain[chainLen] = nb;
  chainLen++;
  return true;
}

// ===== VALIDASI CHAIN =====
bool validateChain() {
  for (int i = 1; i < chainLen; i++) {
    // Cek prevHash
    if (chain[i].prevHash != chain[i-1].hash) {
      Serial.printf("[INVALID] Blok #%d: prevHash tidak cocok!\n", i);
      return false;
    }
    // Recompute hash
    String expected = computeHash(chain[i].index, chain[i].timestamp,
                                  chain[i].sensorData, chain[i].prevHash,
                                  chain[i].nonce);
    if (chain[i].hash != expected) {
      Serial.printf("[INVALID] Blok #%d: hash rusak!\n", i);
      return false;
    }
  }
  return true;
}

// ===== PRINT BLOK KE SERIAL =====
void printBlock(Block& b) {
  Serial.println("┌─────────────────────────────────┐");
  Serial.printf( "│ Blok     : #%u\n", b.index);
  Serial.printf( "│ Waktu    : %s ms\n", b.timestamp.c_str());
  Serial.printf( "│ Data     : %s\n", b.sensorData.c_str());
  Serial.printf( "│ PrevHash : %s\n", b.prevHash.c_str());
  Serial.printf( "│ Hash     : %s\n", b.hash.c_str());
  Serial.printf( "│ Nonce    : %u\n", b.nonce);
  Serial.println("└─────────────────────────────────┘");
}

// ===== BACA SENSOR DHT22 =====
String readSensors() {
  float temp  = dht.readTemperature();
  float humid = dht.readHumidity();

  // Gunakan random jika sensor error (simulasi Wokwi)
  if (isnan(temp))  temp  = random(2200, 3500) / 100.0;
  if (isnan(humid)) humid = random(5000, 8500) / 100.0;

  // Buat JSON payload
  StaticJsonDocument<128> doc;
  doc["temp"]   = serialized(String(temp,  2));
  doc["humid"]  = serialized(String(humid, 2));
  doc["co2"]    = random(400, 1200);  // simulasi MQ-135
  doc["light"]  = random(100, 1000);  // simulasi BH1750
  doc["device"] = "ESP32-NODE-01";

  String out;
  serializeJson(doc, out);
  return out;
}

// ===== LED FEEDBACK =====
void blinkLED(int pin, int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(ms);
    digitalWrite(pin, LOW);
    delay(ms);
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(500);
  dht.begin();

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED,   OUTPUT);

  Serial.println("\n=============================");
  Serial.println("  IoT BLOCKCHAIN SIMULATOR  ");
  Serial.println("  ESP32 + DHT22  |  Wokwi   ");
  Serial.println("=============================\n");

  initChain();
  blinkLED(LED_GREEN, 3, 150);
}

// ===== LOOP =====
uint32_t lastMine = 0;
const uint32_t INTERVAL = 5000;  // Tambah blok tiap 5 detik

void loop() {
  if (millis() - lastMine >= INTERVAL) {
    lastMine = millis();

    // 1. Baca sensor
    String payload = readSensors();
    Serial.printf("\n[INFO] Data sensor: %s\n", payload.c_str());

    // 2. Tambah ke blockchain
    if (addBlock(payload)) {
      Serial.printf("[OK] Blok #%d ditambahkan ke chain\n", chainLen - 1);
      printBlock(chain[chainLen - 1]);
      blinkLED(LED_GREEN, 1, 200);
    }

    // 3. Validasi chain
    if (validateChain()) {
      Serial.println("[VALID] Semua blok valid ✓");
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED,   LOW);
    } else {
      Serial.println("[ERROR] Integritas chain RUSAK!");
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_RED,   HIGH);
    }

    // 4. Print ringkasan
    Serial.printf("[CHAIN] Total blok: %d\n", chainLen);
  }

  // Simulasi tamper via Serial Monitor
  // Ketik 'T' + Enter di Serial Monitor untuk tamper
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'T' && chainLen > 1) {
      Serial.println("\n[TAMPER] Memodifikasi blok #1...");
      chain[1].sensorData = "{\"temp\":\"99.99\",\"tampered\":true}";
      // hash TIDAK diperbarui — simulasi serangan
      Serial.println("[TAMPER] Data diubah, hash lama dipertahankan");
      blinkLED(LED_RED, 5, 100);
    }
    if (c == 'R') {
      Serial.println("\n[RESET] Membuat ulang blockchain...");
      initChain();
      blinkLED(LED_GREEN, 3, 150);
    }
  }
}