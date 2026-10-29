# Smart Socket - Wemos D1 Mini

Kontrol 3 relay lewat Telegram (internet) dengan cadangan hotspot + web lokal.

## Fitur
- 3 relay (via transistor NPN), 3 tombol fisik (toggle)
- Mode online: kontrol lewat bot Telegram
- Mode lokal: hotspot `Wemos_D1_Mini_AP`, web di http://192.168.4.1
- Buzzer saat toggle, 3x bip saat aktif, bip panjang saat reset
- Reset: tahan tombol 1 + 2 selama 3 detik

## Pin
| Pin | Fungsi |
|---|---|
| D8, D7, D6 | Relay 1-3 (ke basis transistor lewat 1 kΩ) |
| D1, D2, D3 | Tombol 1-3 (ke GND) |
| D5 | Buzzer |
| D4 | LED indikator (bawaan) |

## Cara pakai
1. Salin `src/secrets.h.example` menjadi `src/secrets.h`, lalu isi data Anda.
2. Buat bot lewat @BotFather dan pakai tokennya.
3. `pio run -t upload`
4. Di Telegram, kirim `/daftar PIN_ANDA` ke bot.

## Perintah bot
/menu, /status, /on 1, /off 1, /toggle 1, /allon, /alloff, /unpair

## Catatan
- Jangan menahan tombol 3 (D3) saat power on.
- D8 harus LOW saat boot.
