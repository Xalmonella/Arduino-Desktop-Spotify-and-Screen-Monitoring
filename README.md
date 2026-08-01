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
