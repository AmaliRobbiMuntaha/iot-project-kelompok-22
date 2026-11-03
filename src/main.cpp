/*
  Kontrol 3 Relay lewat Telegram - Wemos D1 Mini
  Library: UniversalTelegramBot + ArduinoJson 6.x
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

// PENGATURAN
// CONFIG PLACEHOLDER
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";
const char* BOT_TOKEN = "";
const char* PIN_DAFTAR = "";   // dipakai untuk /daftar
const char* AP_SSID = "";
const char* AP_PASS = "";

// MODE TES: true = hanya tes pin (buka Serial Monitor 115200), false = program normal
const bool MODE_TES = false;

// POLARITAS (false = aktif HIGH, true = aktif LOW)
const bool RELAY_AKTIF_LOW  = false;  // relay via transistor NPN: HIGH = ON
const bool BUZZER_AKTIF_LOW = false;  // HIGH = bunyi
const bool LED_AKTIF_LOW    = true;   // LED bawaan D4 nyala saat LOW

// PIN
const int JUMLAH = 3;
const uint8_t RELAY[JUMLAH]  = {D8, D7, D6};
const uint8_t TOMBOL[JUMLAH] = {D1, D2, D3};   // tombol ke GND
const uint8_t BUZZER = D5;
const uint8_t LED = D4;

// VARIABEL
WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);
ESP8266WebServer web(80);

bool nyala[JUMLAH] = {false, false, false};
bool online = false;
String pemilik = "";   // chat ID pemilik, disimpan di EEPROM
String catatan = "";   // laporan dari tombol fisik, dikirim ke Telegram

unsigned long tCek = 0, tPoll = 0, tOk = 0, tCoba = 0;

const char* BANTUAN =
  "Perintah:\n"
  "/menu - tombol kontrol\n"
  "/status - lihat status relay\n"
  "/on 1 (atau 2, 3, all)\n"
  "/off 1 (atau 2, 3, all)\n"
  "/toggle 1\n"
  "/allon dan /alloff\n"
  "/unpair - hapus pendaftaran";

// BUZZER & LED
void bunyi(int ms) {
  digitalWrite(BUZZER, !BUZZER_AKTIF_LOW);
  delay(ms);
  digitalWrite(BUZZER, BUZZER_AKTIF_LOW);
}

void ledSet(bool on) {
  digitalWrite(LED, on != LED_AKTIF_LOW);
}

// PEMILIK (EEPROM)
void bacaPemilik() {
  char buf[20];
  EEPROM.get(0, buf);
  buf[19] = 0;
  bool valid = (buf[0] == '-' || isDigit(buf[0]));
  pemilik = valid ? String(buf) : "";
}

void simpanPemilik(String id) {
  char buf[20] = {0};
  id.toCharArray(buf, 19);
  EEPROM.put(0, buf);
  EEPROM.commit();
  pemilik = id;
}

// RELAY
void setRelay(int i, bool on, bool dariTombol = false, bool bip = true) {
  nyala[i] = on;
  digitalWrite(RELAY[i], on != RELAY_AKTIF_LOW);
  if (bip) bunyi(60);
  if (dariTombol && catatan.length() < 200) {
    catatan += "Relay " + String(i + 1) + " -> " + (on ? "ON" : "OFF") + " (tombol)\n";
  }
}

String status() {
  String s = "Status relay:\n";
  for (int i = 0; i < JUMLAH; i++) {
    s += "Relay " + String(i + 1) + ": " + (nyala[i] ? "ON" : "OFF") + "\n";
  }
  return s;
}

// TES PIN (aktif kalau MODE_TES = true, hasilnya tampil di Serial Monitor)
void tesPin() {
  Serial.println("\n=== MODE TES PIN ===");

  Serial.println("LED (D4): nyala 1 detik");
  ledSet(true);
  delay(1000);
  ledSet(false);

  Serial.println("Buzzer (D5): bunyi 2 kali");
  bunyi(200);
  delay(200);
  bunyi(200);

  for (int i = 0; i < JUMLAH; i++) {
    Serial.printf("Relay %d (GPIO%d): ON 1,5 detik\n", i + 1, RELAY[i]);
    setRelay(i, true, false, false);
    delay(1500);
    setRelay(i, false, false, false);
    delay(500);
  }

  Serial.println("Tes tombol: tekan tombol 1/2/3, relay ikut berubah");
  bool terakhir[JUMLAH] = {false, false, false};
  while (true) {                                  // tidak keluar dari sini
    for (int i = 0; i < JUMLAH; i++) {
      bool tekan = !digitalRead(TOMBOL[i]);       // ditekan = LOW
      if (tekan != terakhir[i]) {
        terakhir[i] = tekan;
        Serial.printf("Tombol %d (GPIO%d): %s\n", i + 1, TOMBOL[i], tekan ? "DITEKAN" : "DILEPAS");
        if (tekan) setRelay(i, !nyala[i]);
        delay(40);                                // debounce sederhana
      }
    }
    delay(5);
  }
}

// TELEGRAM
void kirimMenu(String chat, String judul) {
  String k = "[[";
  for (int i = 0; i < JUMLAH; i++) {
    if (i) k += ",";
    k += "{\"text\":\"Relay " + String(i + 1) + ": " + (nyala[i] ? "ON" : "OFF") +
         "\",\"callback_data\":\"/toggle " + String(i + 1) + "\"}";
  }
  k += "],[{\"text\":\"Semua ON\",\"callback_data\":\"/allon\"},"
       "{\"text\":\"Semua OFF\",\"callback_data\":\"/alloff\"}],"
       "[{\"text\":\"Refresh\",\"callback_data\":\"/menu\"}]]";
  bot.sendMessageWithInlineKeyboard(chat, judul + "\nTekan tombol untuk kontrol:", "", k);
}

void proses(telegramMessage& m) {
  String chat = m.chat_id;
  String teks = m.text;
  teks.trim();
  if (m.type == "callback_query") bot.answerCallbackQuery(m.query_id);

  // pecah jadi perintah + argumen
  int sp = teks.indexOf(' ');
  String cmd = (sp < 0) ? teks : teks.substring(0, sp);
  String arg = (sp < 0) ? "" : teks.substring(sp + 1);
  cmd.toLowerCase();
  arg.trim();
  arg.toLowerCase();
  int at = cmd.indexOf('@');
  if (at > 0) cmd.remove(at);

  // belum ada pemilik -> hanya /daftar
  if (pemilik == "") {
    if (cmd != "/daftar") {
      bot.sendMessage(chat, "Belum terdaftar. Ketik: /daftar <PIN>", "");
    } else if (arg == PIN_DAFTAR) {
      simpanPemilik(chat);
      bot.sendMessage(chat, "Pendaftaran berhasil.\n\n" + String(BANTUAN), "");
      kirimMenu(chat, "");
    } else {
      bot.sendMessage(chat, "PIN salah.", "");
    }
    return;
  }

  // orang lain ditolak
  if (chat != pemilik) {
    bot.sendMessage(chat, "Akses ditolak.", "");
    return;
  }

  if (cmd == "/on" || cmd == "/off" || cmd == "/toggle") {
    bool semua = (arg == "all");
    int id = arg.toInt();
    String hasil = "Selesai.";
    if (!semua && (id < 1 || id > JUMLAH)) {
      hasil = "Format: " + cmd + " 1 (nomor 1-" + String(JUMLAH) + " atau all)";
    } else {
      for (int i = semua ? 0 : id - 1; i < (semua ? JUMLAH : id); i++) {
        setRelay(i, cmd == "/on" ? true : (cmd == "/off" ? false : !nyala[i]));
      }
    }
    kirimMenu(chat, hasil + "\n" + status());
  }
  else if (cmd == "/allon" || cmd == "/alloff") {
    for (int i = 0; i < JUMLAH; i++) setRelay(i, cmd == "/allon", false, i == 0);
    kirimMenu(chat, status());
  }
  else if (cmd == "/start" || cmd == "/menu") kirimMenu(chat, status());
  else if (cmd == "/status") bot.sendMessage(chat, status(), "");
  else if (cmd == "/unpair") {
    simpanPemilik("");
    bot.sendMessage(chat, "Pendaftaran dihapus. Daftar lagi dengan /daftar <PIN>.", "");
  }
  else bot.sendMessage(chat, BANTUAN, "");
}

void cekPesan() {
  int n = bot.getUpdates(bot.last_message_received + 1);
  while (n) {
    for (int i = 0; i < n; i++) proses(bot.messages[i]);
    n = bot.getUpdates(bot.last_message_received + 1);
  }
}

// buang pesan lama yang menumpuk saat Wemos mati/offline
void buangPesanLama() {
  while (bot.getUpdates(bot.last_message_received + 1)) {}
}

bool telegramAda() {
  return WiFi.status() == WL_CONNECTED && bot.getMe();
}

// WEB LOKAL (cadangan)
void halaman() {
  String h = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
             "<style>body{font-family:Arial;text-align:center;margin-top:30px}"
             "a{display:inline-block;padding:14px 28px;color:#fff;border-radius:8px;"
             "text-decoration:none;margin:6px;font-size:18px}"
             ".on{background:#28a745}.off{background:#dc3545}</style></head><body>"
             "<h2>Kontrol Wemos</h2><p>Mode lokal (Telegram tidak tersedia)</p>";
  for (int i = 0; i < JUMLAH; i++) {
    String n = String(i + 1);
    h += "<p>Relay " + n + ": <b>" + (nyala[i] ? "ON" : "OFF") + "</b></p>";
    h += "<a href='/toggle?id=" + n + "' class='" + (nyala[i] ? "off" : "on") + "'>" +
         (nyala[i] ? "Matikan" : "Nyalakan") + " Relay " + n + "</a>";
  }
  web.send(200, "text/html", h + "</body></html>");
}

void toggleWeb() {
  int id = web.arg("id").toInt();
  if (id >= 1 && id <= JUMLAH) setRelay(id - 1, !nyala[id - 1]);
  web.sendHeader("Location", "/");
  web.send(303);
}

// GANTI MODE
void modeOnline() {
  web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  online = true;
  tOk = tCek = tPoll = millis();
  buangPesanLama();
  Serial.println("MODE: ONLINE (Telegram)");
  if (pemilik != "") bot.sendMessage(pemilik, "Wemos online.\n" + status() + "Ketik /menu untuk kontrol.", "");
}

void modeLokal() {
  online = false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  web.begin();
  tCoba = millis();
  Serial.println("MODE: LOKAL (hotspot)");
}

// TOMBOL FISIK
// relay berubah saat tombol DILEPAS, supaya tombol 1+2 (reset) tidak ikut toggle
void bacaTombol() {
  static bool tekan[JUMLAH], mentah[JUMLAH], kombo = false;
  static unsigned long berubah[JUMLAH], mulaiKombo = 0;
  unsigned long now = millis();

  for (int i = 0; i < JUMLAH; i++) {
    bool r = !digitalRead(TOMBOL[i]);
    if (r != mentah[i]) { mentah[i] = r; berubah[i] = now; }
    if (now - berubah[i] > 40 && r != tekan[i]) {
      tekan[i] = r;
      if (!r && !kombo) setRelay(i, !nyala[i], true);
    }
  }

  // tahan tombol 1 + 2 selama 3 detik -> reset
  if (tekan[0] && tekan[1]) {
    kombo = true;
    if (mulaiKombo == 0) mulaiKombo = now;
    else if (now - mulaiKombo >= 3000) {
      for (int i = 0; i < JUMLAH; i++) setRelay(i, false, false, false);
      if (online && pemilik != "") bot.sendMessage(pemilik, "Reset lewat tombol. Wemos restart.", "");
      bunyi(1000);
      ESP.restart();
    }
  } else {
    mulaiKombo = 0;
  }
  if (!tekan[0] && !tekan[1]) kombo = false;
}

// SETUP
void setup() {
  Serial.begin(115200);
  Serial.println("\nReset reason: " + ESP.getResetReason());
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());

  EEPROM.begin(32);
  bacaPemilik();
  Serial.println("Pemilik tersimpan: [" + pemilik + "]");
  simpanPemilik("");   // HAPUS BARIS INI setelah sekali upload

  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, BUZZER_AKTIF_LOW);
  pinMode(LED, OUTPUT);
  ledSet(false);

  for (int i = 0; i < JUMLAH; i++) {
    pinMode(RELAY[i], OUTPUT);
    setRelay(i, false, false, false);     // semua relay mati saat boot
    pinMode(TOMBOL[i], INPUT_PULLUP);
  }

  if (MODE_TES) tesPin();                 // mode tes: tidak lanjut ke bawah

  web.on("/", halaman);
  web.on("/toggle", toggleWeb);

  client.setInsecure();                   // tanpa cek sertifikat
  bot.longPoll = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);

  if (telegramAda()) modeOnline();
  else modeLokal();

  for (int i = 0; i < 3; i++) { bunyi(70); delay(70); }   // tanda sistem aktif
}

// LOOP
void loop() {
  bacaTombol();
  ledSet(online ? true : (millis() / 500) % 2);   // online: nyala terus, lokal: kedip

  unsigned long now = millis();

  if (online) {
    if (now - tCek >= 10000) {                    // cek koneksi tiap 10 detik
      Serial.printf("Heap: %u | WiFi: %d\n", ESP.getFreeHeap(), WiFi.status());
      tCek = now;
      if (telegramAda()) tOk = now;
    }
    if (now - tOk > 30000) { modeLokal(); return; }   // putus > 30 detik -> mode lokal

    if (now - tPoll >= 1500) {                    // cek pesan tiap 1,5 detik
      tPoll = now;
      cekPesan();
      if (catatan != "" && pemilik != "") bot.sendMessage(pemilik, catatan, "");
      catatan = "";
    }
  } else {
    web.handleClient();
    if (now - tCoba > 20000) {                    // coba online lagi tiap 20 detik
      tCoba = now;
      if (WiFi.status() != WL_CONNECTED) WiFi.begin(WIFI_SSID, WIFI_PASS);
      else if (telegramAda()) modeOnline();
    }
  }
}
