// =============================================================================
// HI-FI SPOTIFY OLED DISPLAY — ESP32 + SH1106 1.3"
// Features: 
//   - Screen 1: Spotify Now Playing (Vinyl + Tonearm + 7-Band Peak-Hold EQ)
//   - Screen 2: Standby Clock & Animated Face (Daytime Gaming Mode vs Nighttime Sleep Mode)
// =============================================================================

#include <WiFi.h>
#include <WiFiUdp.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <math.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const char* ssid     = "Xenomorph-RSCP";
const char* password = "112358mn";

WiFiUDP udp;
const unsigned int UDP_PORT = 8888;
char packetBuf[512];

// Data Spotify State
#define SLEN 48

char dTitle[SLEN]    = "Waiting...";
char dArtist[SLEN]   = "Spotify";
char dDate[SLEN]     = "--/--/----";
char dTime[SLEN]     = "--:--:--";

// Local clock
unsigned long recvPosMs  = 0;
unsigned long recvDurMs  = 180000;
unsigned long recvTimeMs = 0;
bool isPlaying           = false;

// Vinyl Disc & Tonearm animation state
float discAngle    = 0.0f;
float armAngle     = 0.0f; // Angle of tonearm needle (0.0 = lifted, 1.0 = down on vinyl)

// Title marquee
int  titleScrollX        = 0;
int  titlePhase          = 0;
unsigned long titlePhaseStart = 0;
const int TITLE_PAUSE_MS = 2000;

// Progress smooth
float smoothProgress = 0.0f;

// 7-Band Equalizer Peak-Hold Physics State
#define BANDS 7
float eqBars[BANDS]       = {0};
float eqPeaks[BANDS]      = {0};
float eqVelocities[BANDS] = {0};

// Face & Eye Animation State
unsigned long lastBlinkMs = 0;
bool isBlinking           = false;

// Screen Override Mode: 0 = Auto, 1 = Force Screen 1 (Spotify), 2 = Force Screen 2 (Clock & Face)
int forceScreenMode       = 0;

// Frame timing
unsigned long lastFrameMs = 0;
const int FRAME_MS = 33; // ~30 FPS

// Helper: Extract Hour (0-23) from "HH:MM:SS" time string
int getHourFromTimeStr() {
  if (strlen(dTime) >= 2 && dTime[0] >= '0' && dTime[0] <= '9' && dTime[1] >= '0' && dTime[1] <= '9') {
    return (dTime[0] - '0') * 10 + (dTime[1] - '0');
  }
  return 12; // Default to 12 (Daytime)
}

void parseUDP(char* buf) {
  char* f[8];
  int n = 0;
  f[0] = buf;
  for (char* p = buf; *p && n < 7; p++) {
    if (*p == '\t') { *p = '\0'; n++; f[n] = p + 1; }
  }
  if (n < 6) return;

  if (strcmp(f[0], dTitle) != 0) {
    titleScrollX = 0;
    titlePhase = 0;
    titlePhaseStart = millis();
  }

  strncpy(dTitle,  f[0], SLEN - 1);
  strncpy(dArtist, f[1], SLEN - 1);
  if (strlen(f[2]) > 0) strncpy(dDate, f[2], SLEN - 1);
  if (strlen(f[3]) > 0) strncpy(dTime, f[3], SLEN - 1);
  recvPosMs  = strtoul(f[4], NULL, 10);
  recvDurMs  = strtoul(f[5], NULL, 10);
  recvTimeMs = millis();
  isPlaying  = (f[6][0] == '1');
  if (recvDurMs < 1000) recvDurMs = 180000;

  if (n >= 7) {
    forceScreenMode = atoi(f[7]);
  }
}

unsigned long getLocalPosMs() {
  if (!isPlaying) return recvPosMs;
  unsigned long pos = recvPosMs + (millis() - recvTimeMs);
  return (pos > recvDurMs) ? recvDurMs : pos;
}

// -----------------------------------------------------------------------------
// DRAW: Animated Vinyl Disc + Tonearm Needle
// -----------------------------------------------------------------------------
void drawVinylAndArm(int cx, int cy, int r) {
  // Piringan hitam (Vinyl)
  u8g2.drawDisc(cx, cy, r);

  // Alur piringan (Grooves)
  u8g2.setDrawColor(0);
  u8g2.drawCircle(cx, cy, r - 3);
  u8g2.drawCircle(cx, cy, r - 6);
  u8g2.drawCircle(cx, cy, r - 8);

  // Label tengah & Lubang
  u8g2.drawDisc(cx, cy, 3);
  u8g2.setDrawColor(1);
  u8g2.drawCircle(cx, cy, 3);
  u8g2.drawDisc(cx, cy, 1);

  // Rotasi vinyl saat playing
  if (isPlaying) {
    discAngle += 0.10f;
    if (discAngle > 6.2832f) discAngle -= 6.2832f;
  }

  // Garis tanda putaran vinyl
  int lx = cx + (int)(cosf(discAngle) * (r - 2));
  int ly = cy + (int)(sinf(discAngle) * (r - 2));
  u8g2.setDrawColor(0);
  u8g2.drawLine(cx, cy, lx, ly);
  u8g2.setDrawColor(1);

  // Animasi Lengan Jarum (Tonearm)
  float targetArm = isPlaying ? 1.0f : 0.0f;
  armAngle += (targetArm - armAngle) * 0.10f; // Smooth transition

  // Titik pangkal tonearm (pivot) di pojok kanan-atas piringan
  int px = cx + 10;
  int py = cy - 11;

  // Ujung jarum mendarat di piringan
  int nx = cx + (int)(3.0f * armAngle);
  int ny = cy - 4 + (int)(3.0f * armAngle);

  // Gambar Tonearm (Pangkal + Batang + Jarum)
  u8g2.drawDisc(px, py, 2);
  u8g2.drawLine(px, py, nx, ny);
  u8g2.drawBox(nx - 1, ny - 1, 2, 2); // Headshell jarum
}

// -----------------------------------------------------------------------------
// DRAW: 7-Band Hi-Fi Spectrum Equalizer with Peak-Hold Floating Dots
// -----------------------------------------------------------------------------
void drawSpectrumEqualizer(int x, int baseY, int maxH) {
  float t = millis() / 1000.0f;

  for (int i = 0; i < BANDS; i++) {
    int barX = x + (i * 5);

    if (isPlaying) {
      // Kalkulasi tinggi target menggunakan fungsi gelombang multi-frekuensi
      float freq = 4.0f + (i * 1.5f);
      float phase = i * 0.8f;
      float val = (sinf(t * freq + phase) + 1.0f) * 0.5f;
      val += (cosf(t * (freq * 0.7f) - phase) + 1.0f) * 0.25f;
      val = constrain(val, 0.1f, 1.0f);

      float targetH = val * maxH;
      eqBars[i] += (targetH - eqBars[i]) * 0.35f; // Responsive bar lerp

      // Peak-Hold Physics (Titik puncak melayang lalu jatuh perlahan)
      if (eqBars[i] > eqPeaks[i]) {
        eqPeaks[i] = eqBars[i];
        eqVelocities[i] = 0.0f;
      } else {
        eqVelocities[i] += 0.25f; // Gravitasi jatuh
        eqPeaks[i] -= eqVelocities[i];
        if (eqPeaks[i] < eqBars[i]) eqPeaks[i] = eqBars[i];
      }
    } else {
      // Pause: Bar turun ke minimum
      eqBars[i] += (1.0f - eqBars[i]) * 0.2f;
      eqPeaks[i] += (1.0f - eqPeaks[i]) * 0.2f;
    }

    int bh = (int)eqBars[i];
    int ph = (int)eqPeaks[i];
    bh = constrain(bh, 1, maxH);
    ph = constrain(ph, 1, maxH);

    // Gambar Batang Bar Equalizer (Lebar 3px)
    u8g2.drawBox(barX, baseY - bh, 3, bh);

    // Gambar Titik Peak Hold Melayang di Atasnya
    if (ph > bh + 1) {
      u8g2.drawHLine(barX, baseY - ph, 3);
    }
  }
}

// -----------------------------------------------------------------------------
// SCREEN 1: SPOTIFY NOW PLAYING
// -----------------------------------------------------------------------------
void drawSpotifyScreen() {
  unsigned long now = millis();
  unsigned long posMs = getLocalPosMs();

  int curSec = posMs / 1000;
  int curMins = curSec / 60;
  int curSecs = curSec % 60;

  int durSec = recvDurMs / 1000;
  int durMins = durSec / 60;
  int durSecs = durSec % 60;

  float targetPct = (float)posMs / (float)recvDurMs * 100.0f;
  if (targetPct > 100.0f) targetPct = 100.0f;
  smoothProgress += (targetPct - smoothProgress) * 0.15f;

  u8g2.clearBuffer();

  // 1. TOP HEADER: BADGE PLAY/PAUSE, DATE & TIME CLOCK (Y: 0..10)
  u8g2.setFont(u8g2_font_4x6_tf);
  
  if (isPlaying) {
    u8g2.setDrawColor(1);
    u8g2.drawRBox(0, 0, 26, 9, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(4, 7, "PLAY");
    u8g2.setDrawColor(1);
  } else {
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(0, 0, 26, 9, 2);
    u8g2.drawStr(3, 7, "PAUSE");
  }

  // Tanggal di tengah header: "01/08/2026"
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(30, 7, dDate);

  // Jam di kanan header: "13:24:32"
  int timeW = u8g2.getStrWidth(dTime);
  u8g2.drawStr(128 - timeW, 7, dTime);

  // Pembatas garis horizontal header
  u8g2.drawHLine(0, 10, 128);

  // 2. VINYL DISC WITH ANIMATED TONEARM (KIRI, CX: 14, CY: 24, R: 11)
  drawVinylAndArm(14, 24, 11);

  // 3. TRACK TITLE & ARTIST (KANAN) (Y: 12..37)
  u8g2.setFont(u8g2_font_7x13B_tf);
  int titleW = u8g2.getStrWidth(dTitle);
  int textAreaStart = 30;
  int textAreaWidth = 128 - textAreaStart;

  if (titleW <= textAreaWidth) {
    u8g2.drawStr(textAreaStart, 21, dTitle);
  } else {
    int maxScroll = titleW - textAreaWidth;
    switch (titlePhase) {
      case 0:
        titleScrollX = 0;
        if (now - titlePhaseStart > TITLE_PAUSE_MS) { titlePhase = 1; titlePhaseStart = now; }
        break;
      case 1:
        titleScrollX++;
        if (titleScrollX >= maxScroll) { titleScrollX = maxScroll; titlePhase = 2; titlePhaseStart = now; }
        break;
      case 2:
        if (now - titlePhaseStart > TITLE_PAUSE_MS) { titlePhase = 3; titlePhaseStart = now; }
        break;
      case 3:
        titleScrollX--;
        if (titleScrollX <= 0) { titleScrollX = 0; titlePhase = 0; titlePhaseStart = now; }
        break;
    }
    u8g2.setClipWindow(textAreaStart, 11, 128, 23);
    u8g2.drawStr(textAreaStart - titleScrollX, 21, dTitle);
    u8g2.setMaxClipWindow();
  }

  // Artist Name
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(textAreaStart, 33, dArtist);

  // 4. 7-BAND SPECTRUM EQUALIZER & TRACK TIMER (Y: 38..52)
  drawSpectrumEqualizer(0, 52, 13);

  // Timer Posisi Lagu di kanan bawah visualizer: "01:25 / 03:45"
  u8g2.setFont(u8g2_font_5x7_tf);
  char timeBuf[16];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d/%02d:%02d", curMins, curSecs, durMins, durSecs);
  int tw = u8g2.getStrWidth(timeBuf);
  u8g2.drawStr(128 - tw, 50, timeBuf);

  // 5. BOTTOM BAR: SLEEK PROGRESS BAR & PERCENTAGE (Y: 54..64)
  u8g2.drawHLine(0, 55, 128);

  int barFill = (int)(smoothProgress * 1.28f);
  barFill = constrain(barFill, 0, 128);

  if (barFill > 0) {
    u8g2.drawBox(0, 54, barFill, 3);
    u8g2.drawDisc(barFill, 55, 2); // Handle slider
  }

  // Persentase kecil di pojok kanan bawah
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", (int)smoothProgress);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(128 - u8g2.getStrWidth(pctBuf), 64, pctBuf);

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 2: STANDBY CLOCK & ANIMATED FACE (DAY GAMING MODE vs NIGHT SLEEP MODE)
// -----------------------------------------------------------------------------
// Helper: Medical P-Q-R-S-T ECG Heartbeat waveform generator
int getECGOffset(int phase) {
  phase = ((phase % 36) + 36) % 36; // 36-pixel repeating cycle
  if (phase >= 2 && phase <= 5) {
    return -1; // P-wave (small bump)
  } else if (phase == 8) {
    return 1;  // Q-wave (small dip)
  } else if (phase == 9) {
    return -8; // R-wave (sharp peak UP!)
  } else if (phase == 10) {
    return 3;  // S-wave (sharp dip DOWN!)
  } else if (phase >= 14 && phase <= 19) {
    int t = phase - 14;
    return (t >= 1 && t <= 4) ? -2 : -1; // T-wave (smooth recovery bump)
  }
  return 0; // Baseline
}

void drawClockAndFaceScreen() {
  u8g2.clearBuffer();
  unsigned long now = millis();
  int hour = getHourFromTimeStr();
  bool isDaytime = (hour >= 6 && hour < 22);

  // ---------------------------------------------------------------------------
  // 1. TOP HEADER BAR (Y: 0..11)
  // ---------------------------------------------------------------------------
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, dDate);

  u8g2.setFont(u8g2_font_4x6_tf);
  if (isDaytime) {
    // Mode Badge: "XENOMORPH ⚡"
    u8g2.setDrawColor(1);
    u8g2.drawRBox(58, 0, 70, 10, 2);
    u8g2.setDrawColor(0);
    u8g2.drawStr(62, 7, "XENOMORPH ⚡");
    u8g2.setDrawColor(1);
  } else {
    // Mode Badge: "NAP TIME 🌙"
    u8g2.setDrawColor(1);
    u8g2.drawRFrame(60, 0, 68, 10, 2);
    u8g2.drawStr(64, 7, "NAP TIME 🌙");
  }

  u8g2.drawHLine(0, 11, 128);

  // ---------------------------------------------------------------------------
  // 2. LEFT SIDE: BIGGER CUTE HOVER-BOT AVATAR (ENLARGED HEAD & BIG GLOWING EYES)
  // ---------------------------------------------------------------------------
  
  // Smooth Hover Bobbing Y offset (moves 0 to 1 px up and down over time)
  int hoverY = ((now / 350) % 2 == 0) ? 0 : 1;

  if (isDaytime) {
    // === DAYTIME MODE: ACTIVE HOVER-BOT (BIG PROPER OVAL EYES) ===
    
    // Top Antennas (Left & Right) with Ball Tips
    u8g2.drawVLine(10, 15 + hoverY, 7);
    u8g2.drawDisc(10, 14 + hoverY, 1);
    
    u8g2.drawVLine(44, 15 + hoverY, 7);
    u8g2.drawDisc(44, 14 + hoverY, 1);

    // Top Head Crest (Cyan Cap)
    u8g2.drawBox(23, 17 + hoverY, 8, 2);

    // Bigger Robot Helmet Head (X = 11 to 43, W = 32px, H = 24px, Y = 19..43)
    u8g2.drawRFrame(11, 19 + hoverY, 32, 24, 10);
    
    // Dark Curved Visor Container (X = 13 to 41, W = 28px, H = 18px, Y = 22..40)
    u8g2.drawRBox(13, 22 + hoverY, 28, 18, 7);

    // Eye Blinking Logic (Blinks for 120ms every 3.2 seconds)
    if (now - lastBlinkMs > 3200) {
      isBlinking = true;
      if (now - lastBlinkMs > 3320) {
        isBlinking = false;
        lastBlinkMs = now;
      }
    }

    if (isBlinking) {
      // Sleek horizontal blink slots inside visor
      u8g2.drawHLine(17, 31 + hoverY, 7);
      u8g2.drawHLine(30, 31 + hoverY, 7);
    } else {
      // BIG BOLD GLOWING OVAL EYES (7x11px each!)
      u8g2.setDrawColor(1);
      u8g2.drawRBox(17, 26 + hoverY, 7, 11, 3);
      u8g2.drawRBox(30, 26 + hoverY, 7, 11, 3);

      // Inner Pupil Cutout & Shiny Catchlight
      u8g2.setDrawColor(0);
      u8g2.drawPixel(19, 28 + hoverY);
      u8g2.drawPixel(32, 28 + hoverY);
      u8g2.setDrawColor(1);
    }

    // Hover Torso (Egg-shaped body below head: X = 16..38, Y = 43..57)
    u8g2.drawRFrame(16, 43 + hoverY, 22, 14, 6);

    // Glowing Chest Reactor Core Ring (Center of Chest)
    u8g2.drawCircle(27, 49 + hoverY, 3);
    u8g2.drawDisc(27, 49 + hoverY, 1);

    // Floating Side Arms / Flippers
    u8g2.drawRFrame(8, 44 + hoverY, 5, 10, 2);
    u8g2.drawRFrame(41, 44 + hoverY, 5, 10, 2);

    // Floating Ground Shadow
    u8g2.drawHLine(19, 60, 16);
    u8g2.drawHLine(22, 61, 10);

  } else {
    // === NIGHTTIME MODE: SLEEPING HOVER-BOT ("NAP TIME") ===

    // Antenna (lower)
    u8g2.drawVLine(10, 17 + hoverY, 5);
    u8g2.drawDisc(10, 16 + hoverY, 1);
    u8g2.drawVLine(44, 17 + hoverY, 5);
    u8g2.drawDisc(44, 16 + hoverY, 1);

    // Head Frame & Visor
    u8g2.drawRFrame(11, 19 + hoverY, 32, 24, 10);
    u8g2.drawRBox(13, 22 + hoverY, 28, 18, 7);

    // Narrow Sleeping Eyes (- -)
    u8g2.drawHLine(17, 31 + hoverY, 7);
    u8g2.drawHLine(30, 31 + hoverY, 7);

    // Torso & Core
    u8g2.drawRFrame(16, 43 + hoverY, 22, 14, 6);
    u8g2.drawCircle(27, 49 + hoverY, 2);

    // Arms & Shadow
    u8g2.drawRFrame(8, 44 + hoverY, 5, 10, 2);
    u8g2.drawRFrame(41, 44 + hoverY, 5, 10, 2);
    u8g2.drawHLine(19, 60, 16);

    // Floating Animated Zzz... out of the head towards top right
    int animY = (int)((now / 120) % 16);
    u8g2.setFont(u8g2_font_7x13B_tf);
    u8g2.drawStr(48, 30 - animY, "Z");

    int animY2 = (int)(((now + 300) / 120) % 16);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(54, 24 - animY2, "z");

    int animY3 = (int)(((now + 600) / 120) % 16);
    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(60, 18 - animY3, "z");
  }

  // ---------------------------------------------------------------------------
  // 3. RIGHT SIDE: SLEEK DIGITAL CLOCK & ULTRA-SMOOTH CONTINUOUS ECG WAVE
  // ---------------------------------------------------------------------------
  
  // Separator Vertical Line
  u8g2.setDrawColor(1);
  u8g2.drawVLine(54, 14, 48);

  // Parse HH:MM and SS from dTime
  char timeHM[8] = "--:--";
  char timeS[4]  = ":--";
  if (strlen(dTime) >= 8) {
    strncpy(timeHM, dTime, 5);
    timeHM[5] = '\0';
    strncpy(timeS, dTime + 5, 3);
    timeS[3] = '\0';
  }

  // Large Bold HH:MM Clock
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(58, 29, timeHM);

  // Seconds in smaller font
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(98, 29, timeS);

  // Divider Line
  u8g2.drawHLine(58, 33, 68);

  // Status Indicator Text
  u8g2.setFont(u8g2_font_4x6_tf);
  if (isDaytime) {
    u8g2.drawStr(58, 41, "SYS: ONLINE");
    u8g2.drawStr(58, 48, "MODE: OVERDRIVE");
  } else {
    u8g2.drawStr(58, 41, "SYS: STANDBY");
    u8g2.drawStr(58, 48, "MODE: SLEEP");
  }

  // ULTRA-SMOOTH CONTINUOUS MEDICAL ECG WAVE
  int baselineY = 58;
  int waveShift = (int)(now / 18) % 36;
  int prevY = baselineY + getECGOffset(0 - waveShift);

  for (int x = 56; x <= 126; x++) {
    int phase = (x - 56) - waveShift;
    int curY = baselineY + getECGOffset(phase);
    u8g2.drawLine(x - 1, prevY, x, curY);
    prevY = curY;
  }

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// RENDER FRAME ROUTER
// -----------------------------------------------------------------------------
void renderFrame() {
  if (forceScreenMode == 1) {
    drawSpotifyScreen();
  } else if (forceScreenMode == 2) {
    drawClockAndFaceScreen();
  } else {
    // Mode Otomatis (Auto)
    if (isPlaying) {
      drawSpotifyScreen();
    } else {
      drawClockAndFaceScreen();
    }
  }
}

void setup() {
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setBusClock(400000); // 400kHz Fast I2C

  // Boot screen Hi-Fi
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.drawRBox(10, 6, 108, 22, 4);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_7x13B_tf);
  u8g2.drawStr(18, 21, "HI-FI SPOTIFY");
  
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(24, 44, "Connecting Wi-Fi...");
  u8g2.sendBuffer();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(4, 15, "WI-FI CONNECTED!");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(4, 30, "IP:");
  u8g2.drawStr(22, 30, WiFi.localIP().toString().c_str());
  u8g2.drawStr(4, 44, "UDP Receiver: 8888");
  u8g2.drawStr(4, 58, "Waiting for Spotify...");
  u8g2.sendBuffer();

  udp.begin(UDP_PORT);
  delay(1200);
  titlePhaseStart = millis();
  recvTimeMs = millis();
}

void loop() {
  bool gotData = false;
  while (udp.parsePacket()) {
    int len = udp.read(packetBuf, 511);
    if (len > 0) { packetBuf[len] = '\0'; gotData = true; }
  }
  if (gotData) parseUDP(packetBuf);

  unsigned long now = millis();
  if (now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    renderFrame();
  }
}
