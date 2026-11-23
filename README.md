# Monitor Kadar CO2 Ruangan MH-Z19 — ESP32 + Sensor CO2 NDIR + LCD I2C

Alat kecil yang mengukur kadar karbon dioksida (CO2) di udara ruanganmu
dan menampilkannya sebagai angka PPM di LCD. Kalau CO2 tembus 1000 PPM,
buzzer berbunyi, lampu merah menyala, dan kipas pembuangan otomatis
jalan lewat relay sampai udara kembali segar.

Video penjelasan (60 detik):
https://youtube.com/shorts/8HFhKKzaSwE

## Komponen

| Bagian | Keterangan |
|---|---|
| ESP32 DevKit | Otak alat |
| Modul sensor MH-Z19 | Sensor CO2 NDIR, rentang 0-5000 PPM, UART 9600 baud, catu 5V |
| LCD 16x2 I2C (alamat 0x27) | Menampilkan angka PPM + status udara + kipas |
| Buzzer aktif | Bunyi saat pin HIGH |
| LED merah + resistor 220 ohm | Menyala saat CO2 800 PPM ke atas |
| Relay 1 channel (aktif LOW) | Menyalakan kipas 12V saat CO2 di atas 1000 PPM |
| Kipas 12V | Kipas pembuangan, dicatu adaptor 12V sendiri |
| Adaptor 12V | Catu kipas (JANGAN dari ESP32) |
| Catu USB 5V | Adaptor atau port laptop |

## Wiring

| Dari | Ke |
|---|---|
| MH-Z19 TX | ESP32 GPIO16 (RX2) |
| MH-Z19 RX | ESP32 GPIO17 (TX2) |
| MH-Z19 Vin | 5V |
| MH-Z19 GND | GND |
| LCD SDA | ESP32 GPIO21 |
| LCD SCL | ESP32 GPIO22 |
| LCD VCC | 5V |
| LCD GND | GND |
| Buzzer + | ESP32 GPIO26 (aktif HIGH) |
| Buzzer - | GND |
| LED merah anoda | ESP32 GPIO25, lewat resistor 220 ohm ke GND |
| Relay IN | ESP32 GPIO27 (aktif LOW) |
| Relay VCC | 5V |
| Relay GND | GND |
| Relay COM | Adaptor 12V (+) |
| Relay NO | Kipas 12V (+) |
| Kipas 12V (-) | Adaptor 12V (-) |
| USB 5V | ESP32 VIN (lewat konektor USB board) |
| Semua GND | Digabung (sensor, LCD, ESP32, buzzer, LED, relay, USB -) |

Skema lengkap: [schematic.png](schematic.png)

Perhatian daya: modul MH-Z19 dicatu 5V, tapi logika UART-nya 3V3 —
langsung cocok dengan pin GPIO ESP32. Kipas 12V WAJIB dicatu adaptor
12V sendiri lewat relay — jangan ambil dari pin 5V ESP32. Jangan
hubungkan pin TX/RX sensor ke GPIO1/GPIO3 (dipakai Serial monitor USB);
sketch ini memakai Serial2 (GPIO16/17) supaya bebas.

## Cara kerja

ESP32 mengirim perintah baca (0x86) ke MH-Z19 lewat UART (9600 baud)
tiap 2 detik. Sensor membalas paket 9 byte yang berisi nilai CO2
(2 byte) plus ceksum. ESP32 memeriksa ceksumnya dulu (0xFF dikurangi
jumlah byte 1-7, tambah 1) — paket yang ceksumnya salah dibuang, tidak
dipakai angkanya. Dari paket yang valid, diambil nilai PPM-nya.

Sensor MH-Z19 memakai sinar inframerah (NDIR): udara masuk ke ruang
ukur, sensor mengukur seberapa banyak sinar inframerah yang diserap
molekul CO2, lalu mengubahnya jadi angka PPM (0-5000).

Status mengikuti ambang kualitas udara dalam ruangan yang umum dipakai:
di bawah 800 PPM = BAIK (lampu mati, kipas mati), 800-1000 PPM =
WASPADA (lampu merah menyala), di atas 1000 PPM = BAHAYA (buzzer
berbunyi 0,3 detik tiap 2 detik + kipas pembuangan jalan). Alarm
berhenti saat CO2 turun di bawah 950 PPM (histeresis), supaya buzzer
dan kipas tidak hidup-mati terus di sekitar ambang. LCD menampilkan
angka PPM beserta status dan kondisi kipas. Setiap paket yang lolos
atau gagal ceksum dihitung dan bisa dipantau lewat Serial Monitor
(115200 baud).

Biarkan sensor menyala sekitar 3 menit pertama supaya pemanas
inframerahnya stabil sebelum mempercayai angka yang tampil.

## Konfigurasi (di awal sketch)

- `AMBANG_WASPADA_PPM` (800) — CO2 800-1000 PPM = WASPADA.
- `AMBANG_BAHAYA_PPM` (1000) — CO2 di atas 1000 PPM = BAHAYA.
- `AMBANG_ALARM_MATI_PPM` (950) — histeresis: alarm mati di bawah 950 PPM.
- `INTERVAL_BACA_MS` (2000) — minta data tiap 2 detik.
- `DURASI_BUNYI_BUZZER_MS` (300) — lamanya tiap bunyi buzzer.
- `JEDA_BUNYI_BUZZER_MS` (2000) — jeda antar bunyi buzzer selama alarm.
- `MAKSIMAL_ULANG_BACA` (3) — ulang kirim perintah bila sensor tidak menjawab.

Kalau alarm terlalu mudah berbunyi, naikkan `AMBANG_BAHAYA_PPM`;
kalau mau tahu angka pastinya setiap saat, buka Serial Monitor —
PPM dan status dicetak setiap pembacaan.

## Library yang dibutuhkan

- LiquidCrystal_I2C 1.1.2 (LCD 16x2 via backpack PCF8574).

Tidak perlu library khusus MH-Z19 — sketch ini membaca dan memeriksa
paket UART-nya sendiri (driver NDIR ditulis langsung di sketch),
supaya tidak bergantung pada library tambahan.

## Compile

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 MonitorCO2RuanganMHZ19
```

Terverifikasi 23 Nov 2026: sketch 316.965 byte (24% flash),
RAM 24.640 byte (7%), ESP32 core 4.0.0.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
