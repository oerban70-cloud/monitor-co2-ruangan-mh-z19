// Monitor Kadar CO2 Ruangan MH-Z19 — ESP32 + sensor CO2 NDIR + LCD I2C
//
// Alat kecil yang mengukur kadar karbon dioksida (CO2) di udara ruangan
// dan menampilkannya sebagai angka PPM di LCD. Kalau CO2 tembus 1000 PPM,
// buzzer berbunyi, lampu merah menyala, dan kipas pembuangan otomatis
// jalan lewat relay sampai udara kembali segar.
//
// Sensor MH-Z19 memakai sinar inframerah (NDIR): udara masuk ke ruang
// ukur, sensor mengukur seberapa banyak sinar inframerah yang diserap
// molekul CO2, lalu mengubahnya jadi angka PPM (0-5000). ESP32 meminta
// data lewat UART (9600 baud) dengan perintah baca 0x86; sensor membalas
// paket 9 byte yang berisi nilai PPM plus ceksum. ESP32 memeriksa
// ceksumnya dulu — paket yang ceksumnya salah dibuang, tidak ditampilkan.
//
// Ambang (pedoman kualitas udara dalam ruangan yang umum dipakai):
//   di bawah 800 PPM  = BAIK, lampu mati, kipas mati
//   800-1000 PPM      = WASPADA, lampu merah menyala
//   di atas 1000 PPM  = BAHAYA, buzzer berbunyi + kipas jalan
// Alarm berhenti (histeresis) saat CO2 turun di bawah 950 PPM, supaya
// buzzer dan kipas tidak hidup-mati terus di sekitar ambang.
//
// Pin (sama persis dengan schematic.png):
//   MH-Z19 TX -> GPIO16 (RX2 ESP32), RX -> GPIO17 (TX2 ESP32)
//   MH-Z19 Vin -> 5V, GND -> GND (sensor butuh 5V, logika UART 3V3)
//   LCD I2C SDA -> GPIO21, SCL -> GPIO22, VCC -> 5V, GND -> GND
//   Buzzer  +   -> GPIO26 (aktif HIGH), - -> GND
//   LED merah    -> GPIO25, lewat resistor 220 ohm ke GND
//   Relay IN     -> GPIO27 (aktif LOW), VCC -> 5V, GND -> GND
//   Relay NO     -> kipas 12V (adaptor 12V sendiri, JANGAN dari ESP32)
//
// Catatan: biarkan sensor menyala sekitar 3 menit pertama supaya
// pemanas inframerahnya stabil sebelum mempercayai angka yang tampil.
// Jangan hubungkan pin TX/RX sensor ke GPIO1/GPIO3 (dipakai Serial
// monitor USB); Serial2 (GPIO16/17) sengaja dipilih supaya bebas.
// MH-Z19B/C memakai perintah yang sama; MH-Z19 lama (tanpa huruf)
// memakai protokol yang sama pula untuk perintah baca 0x86.

#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// ---------- Pin ----------
const uint8_t PIN_MHZ_TX = 17;    // TX2 ESP32 -> RX sensor MH-Z19
const uint8_t PIN_MHZ_RX = 16;    // RX2 ESP32 <- TX sensor MH-Z19
const uint8_t PIN_BUZZER = 26;    // buzzer aktif, HIGH = bunyi
const uint8_t PIN_LED_MERAH = 25; // lampu peringatan, resistor 220 ohm
const uint8_t PIN_RELAY = 27;     // relay kipas, aktif LOW

// ---------- Konstanta ----------
const uint32_t BAUD_MHZ = 9600;             // baud UART MH-Z19 (tetap)
const uint8_t PANJANG_PAKET = 9;            // paket MH-Z19 selalu 9 byte
const uint8_t PERINTAH_BACA = 0x86;         // perintah minta kadar CO2
const uint16_t AMBANG_WASPADA_PPM = 800;    // 800-1000 PPM = WASPADA
const uint16_t AMBANG_BAHAYA_PPM = 1000;    // di atas 1000 PPM = BAHAYA
const uint16_t AMBANG_ALARM_MATI_PPM = 950; // histeresis: alarm mati < 950
const uint16_t INTERVAL_BACA_MS = 2000;     // minta data tiap 2 detik
const uint16_t BATAS_TUNGGU_RESPON_MS = 500; // tunggu balasan sensor 0,5 dtk
const uint16_t DURASI_BUNYI_BUZZER_MS = 300; // tiap bunyi buzzer 0,3 detik
const uint16_t JEDA_BUNYI_BUZZER_MS = 2000;  // buzzer bunyi tiap 2 detik
const uint8_t MAKSIMAL_ULANG_BACA = 3;      // ulang kirim perintah bila gagal

// Perintah baca MH-Z19: FF 01 86 00 00 00 00 00 79 (ceksum tetap 0x79)
const uint8_t PERINTAH_BACA_MHZ[PANJANG_PAKET] = {
  0xFF, 0x01, PERINTAH_BACA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79
};

// ---------- Status ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);  // LCD 16x2, alamat I2C 0x27
int16_t ppmTerakhir = -1;             // PPM terakhir yang valid (-1 = belum ada)
bool alarmAktif = false;              // true selama CO2 di atas ambang bahaya
bool buzzerBunyi = false;             // true selama buzzer sedang berbunyi
unsigned long waktuBacaBerikutnya = 0;   // jadwal minta data berikutnya
unsigned long waktuBuzzerBerikutnya = 0; // jadwal bunyi buzzer berikutnya
unsigned long waktuBuzzerMati = 0;       // jadwal matikan buzzer
uint32_t jumlahPaketValid = 0;        // total paket lolos ceksum
uint32_t jumlahPaketRusak = 0;        // total paket gagal ceksum

// ---------- Fungsi ----------

// Hitung ceksum paket MH-Z19: 0xFF dikurangi jumlah byte 1-7, tambah 1.
uint8_t hitungCeksum(const uint8_t *paket) {
  uint8_t jumlah = 0;
  for (uint8_t i = 1; i < PANJANG_PAKET - 1; i++) {
    jumlah += paket[i];
  }
  return (uint8_t)(0xFF - jumlah + 1);
}

// Minta satu pembacaan CO2 ke sensor. Mengembalikan PPM (0-5000),
// atau -1 bila sensor tidak menjawab / ceksum paket salah.
int16_t bacaCO2() {
  // Kosongkan sisa byte lama supaya balasan yang dibaca adalah yang baru.
  while (Serial2.available()) {
    Serial2.read();
  }
  Serial2.write(PERINTAH_BACA_MHZ, PANJANG_PAKET);

  // Tunggu sampai 9 byte balasan lengkap, atau sampai batas waktu.
  unsigned long batasWaktu = millis() + BATAS_TUNGGU_RESPON_MS;
  while (Serial2.available() < PANJANG_PAKET) {
    if ((long)(millis() - batasWaktu) >= 0) {
      return -1;  // sensor tidak menjawab tepat waktu
    }
    delay(5);
  }

  uint8_t paket[PANJANG_PAKET];
  Serial2.readBytes(paket, PANJANG_PAKET);

  // Balasan harus diawali 0xFF 0x86, kalau tidak, paket ini bukan balasan baca.
  if (paket[0] != 0xFF || paket[1] != PERINTAH_BACA) {
    return -1;
  }
  // Ceksum byte ke-9 harus cocok, kalau tidak, datanya tidak bisa dipercaya.
  if (paket[PANJANG_PAKET - 1] != hitungCeksum(paket)) {
    return -1;
  }
  return (int16_t)((paket[2] << 8) | paket[3]);
}

// Tampilkan PPM + status di LCD (baris 1 angka, baris 2 status + kipas).
// Kedua baris dipangkas ke 16 karakter supaya tidak meluber ke baris lain.
void tampilkanLcd(int16_t ppm, const char *status, bool kipasJalan) {
  char baris1[17];
  char baris2[17];
  if (ppm < 0) {
    snprintf(baris1, sizeof(baris1), "CO2: ---- ppm");
  } else {
    snprintf(baris1, sizeof(baris1), "CO2: %d ppm", ppm);
  }
  snprintf(baris2, sizeof(baris2), "%-7s%s", status,
           kipasJalan ? "KIPAS ON" : "kipas off");
  lcd.setCursor(0, 0);
  lcd.print(baris1);
  lcd.setCursor(0, 1);
  lcd.print(baris2);
}

// Atur keluaran (lampu, relay, buzzer) sesuai status alarm.
void aturKeluaran(bool bahaya, bool waspada) {
  digitalWrite(PIN_LED_MERAH, (bahaya || waspada) ? HIGH : LOW);
  // Relay aktif LOW: LOW = kipas jalan, HIGH = kipas mati.
  digitalWrite(PIN_RELAY, bahaya ? LOW : HIGH);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(BAUD_MHZ, SERIAL_8N1, PIN_MHZ_RX, PIN_MHZ_TX);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_RELAY, HIGH);  // kipas mati saat mulai

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Monitor CO2");
  lcd.setCursor(0, 1);
  lcd.print("MH-Z19 siap...");
  delay(1500);
  lcd.clear();

  Serial.println("Monitor CO2 MH-Z19 mulai.");
  Serial.println("Tunggu ~3 menit supaya sensor stabil.");
}

void loop() {
  unsigned long sekarang = millis();

  // --- Minta data baru tiap 2 detik ---
  if ((long)(sekarang - waktuBacaBerikutnya) >= 0) {
    waktuBacaBerikutnya = sekarang + INTERVAL_BACA_MS;

    int16_t ppm = -1;
    for (uint8_t ulang = 0; ulang < MAKSIMAL_ULANG_BACA && ppm < 0; ulang++) {
      ppm = bacaCO2();
    }

    if (ppm >= 0) {
      ppmTerakhir = ppm;
      jumlahPaketValid++;
    } else {
      jumlahPaketRusak++;
    }

    // --- Tentukan status dari PPM terakhir yang valid ---
    const char *status = "BAIK";
    bool waspada = false;
    if (ppmTerakhir >= AMBANG_BAHAYA_PPM) {
      alarmAktif = true;  // bahaya: alarm nyala sampai turun di bawah 950
    } else if (ppmTerakhir >= 0 && ppmTerakhir < AMBANG_ALARM_MATI_PPM) {
      alarmAktif = false;  // histeresis: mati hanya kalau sudah di bawah 950
    }
    if (alarmAktif) {
      status = "BAHAYA";
    } else if (ppmTerakhir >= AMBANG_WASPADA_PPM) {
      status = "WASPADA";
      waspada = true;
    }

    aturKeluaran(alarmAktif, waspada);
    tampilkanLcd(ppmTerakhir, status, alarmAktif);

    Serial.print("CO2: ");
    if (ppmTerakhir < 0) {
      Serial.print("----");
    } else {
      Serial.print(ppmTerakhir);
    }
    Serial.print(" ppm  status: ");
    Serial.print(status);
    Serial.print("  kipas: ");
    Serial.print(alarmAktif ? "ON" : "OFF");
    Serial.print("  valid: ");
    Serial.print(jumlahPaketValid);
    Serial.print("  rusak: ");
    Serial.println(jumlahPaketRusak);
  }

  // --- Buzzer: bunyi 0,3 detik tiap 2 detik selama alarm aktif ---
  if (alarmAktif) {
    if (!buzzerBunyi && (long)(sekarang - waktuBuzzerBerikutnya) >= 0) {
      digitalWrite(PIN_BUZZER, HIGH);
      buzzerBunyi = true;
      waktuBuzzerMati = sekarang + DURASI_BUNYI_BUZZER_MS;
    }
    if (buzzerBunyi && (long)(sekarang - waktuBuzzerMati) >= 0) {
      digitalWrite(PIN_BUZZER, LOW);
      buzzerBunyi = false;
      waktuBuzzerBerikutnya = sekarang + JEDA_BUNYI_BUZZER_MS;
    }
  } else if (buzzerBunyi) {
    digitalWrite(PIN_BUZZER, LOW);
    buzzerBunyi = false;
  }
}
