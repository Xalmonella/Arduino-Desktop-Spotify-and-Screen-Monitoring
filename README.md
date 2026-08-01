# Arduino Desktop Spotify & Screen Monitoring

Sistem Pemantauan Desktop, Informasi Spotify Now Playing, dan AI Companion ("Kira") menggunakan **ESP32** dan **Display SH1106 OLED 1.3" (I2C)**.

## 🌟 Fitur Utama
1. **Screen 1 - Spotify Now Playing**:
   - Visualisasi Animasi Vinyl Disc & Tonearm (Needle arm).
   - Teks Marquee Smooth (Time-based scrolling) untuk Judul Lagu & Nama Artis yang panjang.
   - 7-Band Peak-Hold Spectrum Equalizer.
   - Progress bar lagu interaktif & timer posisi.
2. **Screen 2 - Standby Clock & Animated Face**:
   - Animasi Maskot Robot Hover-Bot (Eye blinking, daytime/nighttime mode Zzz animations).
   - Real-time continuous ECG Heartbeat pulse.
3. **Screen 3 - Xenomorph Logo & Clock**:
   - Animated Logo Xenomorph dengan indikator CPU, GPU, dan Temperatur PC.
4. **Screen 4 - Generative AI Companion ("Kira")**:
   - Obrolan AI interaktif terhubung ke Google Gemini API dengan animasi ekspresi piksel.
5. **Screen 5 & 6 - System & Storage Monitoring**:
   - Monitoring statistik PC (CPU, GPU, RAM, Kecepatan Jaringan, Disk C/D, dan Total Storage).
6. **Screen 7 - Interactive Menu**:
   - Menu pilihan layar dengan kursor interaktif via kontrol keyboard terminal.

## 🛠️ Persyaratan Hardware & Software
- **Hardware**: ESP32 Development Board, Display OLED 1.3" SH1106 I2C.
- **Software**: Python 3.10+, Arduino IDE (Library: `U8g2`, `WiFi`, `WiFiUDP`).
- **Python Dependencies**: `pykakasi`, `anyascii`, `unidecode`, `psutil`, `winrt-Windows.Media.Control`.

## 🚀 Cara Penggunaan
1. Flash file `esp32_spotify_oled/esp32_spotify_oled.ino` ke ESP32 menggunakan Arduino IDE.
2. Jalankan `START_SPOTIFY_OLED.bat` atau via terminal Python:
   ```bash
   python spotify_sender.py
   ```

## 🔑 Tutorial Setup Gemini API Key (Multi-Key Rotation)

Sistem ini menggunakan **Gemini API** (Google AI) untuk fitur AI Companion "Kira". Mendukung **multi-key rotation** — jika satu key habis kuota (rate limit), otomatis pindah ke key berikutnya!

### Langkah 1: Dapatkan API Key Gratis

1. Buka **[Google AI Studio](https://aistudio.google.com/apikey)**
2. Login dengan akun Google kamu
3. Klik **"Create API Key"**
4. Pilih project (atau buat baru) → klik **"Create API key in existing project"**
5. **Copy** API key yang muncul (format: `AIzaSy...`)

> 💡 **Tips**: Buat **3-5 API key** dari akun Google yang berbeda untuk rotasi otomatis!
> Setiap key gratis mendapat kuota harian yang melimpah.

### Langkah 2: Simpan Key ke File

Edit file `gemini_key.txt`, tambahkan key satu per baris:

```
# Gemini API Keys (satu key per baris)
# Baris dimulai # = komentar (diabaikan)
AIzaSyA_key_pertama_dari_akun_1
AIzaSyB_key_kedua_dari_akun_2
AIzaSyC_key_ketiga_dari_akun_3
```

### Langkah 3: (Opsional) Tambah Key via Chat

Saat sedang chat dengan Kira, kamu bisa tambah key langsung:

```
[Kamu]: key AIzaSyXXXX_key_utama     → Set key utama (replace semua)
[Kamu]: addkey AIzaSyYYYY_key_baru   → Tambah key ke pool rotasi
[Kamu]: keys                         → Lihat daftar key aktif
```

### Cara Kerja Auto-Rotation

```
Request Chat → Key #1 → Sukses? → Response ✅
                  ↓ (429 Rate Limit)
              Key #2 → Sukses? → Response ✅
                  ↓ (429 Rate Limit)
              Key #3 → Sukses? → Response ✅
                  ↓ (Semua habis)
              Offline Fallback Response 💬
```

- Sistem mencoba **5 model Gemini** per key: `gemini-3.5-flash`, `gemini-3.6-flash`, `gemini-3.5-flash-lite`, `gemini-2.0-flash`, `gemini-2.0-flash-lite`
- Jika semua model pada Key #1 kena rate limit → otomatis pindah ke Key #2
- Key terakhir yang berhasil diingat, sehingga request berikutnya langsung ke key aktif

### Chat Commands Reference

| Command | Fungsi |
|---|---|
| `key <KEY>` | Set API key utama (replace file) |
| `addkey <KEY>` | Tambah key ke pool rotasi |
| `keys` | Lihat semua key aktif (masked) |
| `clear` | Hapus riwayat chat |
| `exit` / `keluar` | Keluar dari chat session |
| `0`-`7` | Ganti screen mode OLED |
