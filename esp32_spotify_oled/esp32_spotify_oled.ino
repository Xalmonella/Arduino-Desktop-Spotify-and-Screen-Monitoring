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
char packetBuf[640];

// 32x32 Pixel-Art Xenomorph Sprite (Front-Facing Pixel Art XBMP format)
static const unsigned char xenomorph_bits[] PROGMEM = {
  0x00, 0x07, 0xe0, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x00,
  0x00, 0x0f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x1f, 0xf8, 0x00,
  0x00, 0x1f, 0xf8, 0x00, 0x00, 0x1f, 0xf8, 0x00, 0x00, 0x1f, 0xf8, 0x00, 0x00, 0x1f, 0xf8, 0x00,
  0x00, 0x3f, 0xfc, 0x00, 0x00, 0x36, 0x6c, 0x00, 0x00, 0x3f, 0xfc, 0x00, 0x08, 0x7f, 0xfe, 0x10,
  0x1c, 0xef, 0xf7, 0x38, 0x3e, 0xcf, 0xf3, 0x7c, 0x77, 0x9f, 0xf9, 0xee, 0xe3, 0xbf, 0xfd, 0xc7,
  0xc1, 0xff, 0xff, 0x83, 0x80, 0xfe, 0x7f, 0x01, 0x00, 0xef, 0xf7, 0x00, 0x00, 0xc7, 0xe3, 0x00,
  0x00, 0xef, 0xf7, 0x00, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0x7c, 0x3e, 0x00, 0x00, 0x38, 0x1c, 0x00,
  0x00, 0x38, 0x1c, 0x00, 0x00, 0x7c, 0x3e, 0x00, 0x00, 0xfe, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Data Spotify State
#define SLEN 48

char dTitle[SLEN]    = "Waiting...";
char dArtist[SLEN]   = "Spotify";
char dDate[SLEN]     = "--/--/----";
char dTime[SLEN]     = "--:--:--";
char dSysLabel[24]   = "SYS: ONLINE";
char dSysDetail[32]  = "MODE: OVERDRIVE";
char dAiEmotion[16]  = "HAPPY";
char dAiText[256]    = "Halo! Aku Kira, pacar AI kamu! ( > ‿ < )";

// System stats data
char dCpu[8]         = "0";
char dRam[8]         = "0";
char dGpu[8]         = "0";
char dCpuTemp[8]     = "0";
char dGpuTemp[8]     = "0";
char dNetDl[16]      = "0K";
char dNetUl[16]      = "0K";

// Laptop Specs & Storage Data (Screen 6)
char dDiskC[20]     = "354G/370G";
char dDiskCPct[8]   = "95";
char dDiskD[20]     = "555G/653G";
char dDiskDPct[8]   = "85";
char dDiskTot[28]   = "909G/1024G (1.0TB)";
char dOwner[24]     = "XENOMORPH";
char dCpuModel[24]  = "Ryzen 7 8845HS";
char dGpuModel[24]  = "RTX 3050 6GB";

// Local clock
unsigned long recvPosMs  = 0;
unsigned long recvDurMs  = 180000;
unsigned long recvTimeMs = 0;
bool isPlaying           = false;

// Vinyl Disc & Tonearm animation state
float discAngle    = 0.0f;
float armAngle     = 0.0f; // Angle of tonearm needle (0.0 = lifted, 1.0 = down on vinyl)

// Title marquee (smooth time-based)
float titleScrollPx       = 0.0f;  // Sub-pixel smooth scroll position
int   titlePhase          = 0;
unsigned long titlePhaseStart = 0;
const int TITLE_PAUSE_MS  = 1800;  // Pause at ends before scrolling
const float TITLE_SCROLL_SPEED = 0.028f; // Pixels per millisecond (~28px/sec)

// Artist marquee (smooth time-based)
float artistScrollPx       = 0.0f;
int   artistPhase          = 0;
unsigned long artistPhaseStart = 0;
const int ARTIST_PAUSE_MS  = 1600;
const float ARTIST_SCROLL_SPEED = 0.024f;

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

// Screen Override Mode: 0 = Auto, 1 = Force Screen 1 (Spotify), 2 = Force Screen 2 (Clock & Face), 3 = Force Screen 3 (Xenomorph Logo), 4 = Force Screen 4 (Kira AI), 5 = System Stats, 6 = Laptop Specs, 7 = Menu
int forceScreenMode       = 0;
int lastScreenMode        = -1;

// Frame timing
unsigned long lastFrameMs = 0;
unsigned long prevFrameMs = 0;  // For delta time calculations
const int FRAME_MS = 33; // ~30 FPS

// Helper: Extract Hour (0-23) from "HH:MM:SS" time string
int getHourFromTimeStr() {
  if (strlen(dTime) >= 2 && dTime[0] >= '0' && dTime[0] <= '9' && dTime[1] >= '0' && dTime[1] <= '9') {
    return (dTime[0] - '0') * 10 + (dTime[1] - '0');
  }
  return 12; // Default to 12 (Daytime)
}

// -----------------------------------------------------------------------------
// INTERNAL RTC CLOCK SYNC (Keeps live HH:MM:SS ticking during Laptop Sleep)
// -----------------------------------------------------------------------------
static unsigned long syncBaseSec = 0;
static unsigned long syncBaseMs  = 0;
static bool hasTimeSync          = false;

void updateTimeFromUDP(const char* timeStr) {
  int h = 0, m = 0, s = 0;
  if (sscanf(timeStr, "%d:%d:%d", &h, &m, &s) >= 2) {
    syncBaseSec = (unsigned long)h * 3600 + (unsigned long)m * 60 + (unsigned long)s;
    syncBaseMs  = millis();
    hasTimeSync = true;
  }
}

void getLiveTimeString(char* outBuf, size_t maxLen) {
  if (!hasTimeSync) {
    strncpy(outBuf, dTime[0] ? dTime : "00:00:00", maxLen - 1);
    outBuf[maxLen - 1] = '\0';
    return;
  }
  unsigned long elapsedSec = (millis() - syncBaseMs) / 1000;
  unsigned long currentSec = (syncBaseSec + elapsedSec) % 86400;
  int h = (currentSec / 3600) % 24;
  int m = (currentSec / 60) % 60;
  int s = currentSec % 60;
  snprintf(outBuf, maxLen, "%02d:%02d:%02d", h, m, s);
}

void parseUDP(char* buf) {
  char* f[28];
  int n = 0;
  f[0] = buf;
  for (char* p = buf; *p && n < 27; p++) {
    if (*p == '\t') { *p = '\0'; n++; f[n] = p + 1; }
  }
  if (n < 6) return;

  if (strcmp(f[0], dTitle) != 0) {
    titleScrollPx = 0.0f;
    titlePhase = 0;
    titlePhaseStart = millis();
  }
  if (strcmp(f[1], dArtist) != 0) {
    artistScrollPx = 0.0f;
    artistPhase = 0;
    artistPhaseStart = millis();
  }

  strncpy(dTitle,  f[0], SLEN - 1);
  strncpy(dArtist, f[1], SLEN - 1);
  if (strlen(f[2]) > 0) strncpy(dDate, f[2], SLEN - 1);
  if (strlen(f[3]) > 0) {
    strncpy(dTime, f[3], SLEN - 1);
    updateTimeFromUDP(f[3]);
  }
  recvPosMs  = strtoul(f[4], NULL, 10);
  recvDurMs  = strtoul(f[5], NULL, 10);
  recvTimeMs = millis();
  isPlaying  = (f[6][0] == '1');
  if (recvDurMs < 1000) recvDurMs = 180000;

  // Helper lambda untuk kerapihan parsing field UDP
  auto copyField = [&](int idx, char* dest, size_t destSize) {
    if (n >= idx && strlen(f[idx]) > 0) {
      strncpy(dest, f[idx], destSize - 1);
      dest[destSize - 1] = '\0';
    }
  };

  if (n >= 7) forceScreenMode = atoi(f[7]);

  copyField(8,  dSysLabel,  sizeof(dSysLabel));
  copyField(9,  dSysDetail, sizeof(dSysDetail));
  copyField(10, dAiEmotion, sizeof(dAiEmotion));
  copyField(11, dAiText,     sizeof(dAiText));
  copyField(12, dCpu,        sizeof(dCpu));
  copyField(13, dRam,        sizeof(dRam));
  copyField(14, dGpu,        sizeof(dGpu));
  copyField(15, dCpuTemp,    sizeof(dCpuTemp));
  copyField(16, dGpuTemp,    sizeof(dGpuTemp));
  copyField(17, dNetDl,      sizeof(dNetDl));
  copyField(18, dNetUl,      sizeof(dNetUl));
  copyField(19, dDiskC,      sizeof(dDiskC));
  copyField(20, dDiskCPct,   sizeof(dDiskCPct));
  copyField(21, dDiskD,      sizeof(dDiskD));
  copyField(22, dDiskDPct,   sizeof(dDiskDPct));
  copyField(23, dDiskTot,    sizeof(dDiskTot));
  copyField(24, dOwner,      sizeof(dOwner));
  copyField(25, dCpuModel,   sizeof(dCpuModel));
  copyField(26, dGpuModel,   sizeof(dGpuModel));
}


void drawHeart(int cx, int cy, int r) {
  if (r < 2) r = 2;
  u8g2.drawDisc(cx - (r / 2), cy - (r / 3), r / 2);
  u8g2.drawDisc(cx + (r / 2), cy - (r / 3), r / 2);
  u8g2.drawTriangle(cx - r, cy - (r / 6), cx + r, cy - (r / 6), cx, cy + r);
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
  armAngle += (targetArm - armAngle) * 0.14f; // Smoother tonearm transition

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
      eqBars[i] += (targetH - eqBars[i]) * 0.40f; // Snappy responsive bar lerp

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
  float dt = (float)(now - prevFrameMs); // Delta time in ms for smooth animations
  if (dt <= 0.0f) dt = 33.0f;
  if (dt > 200.0f) dt = 200.0f; // Clamp to prevent jumps

  int curSec = posMs / 1000;
  int curMins = curSec / 60;
  int curSecs = curSec % 60;

  int durSec = recvDurMs / 1000;
  int durMins = durSec / 60;
  int durSecs = durSec % 60;

  float targetPct = (float)posMs / (float)recvDurMs * 100.0f;
  if (targetPct > 100.0f) targetPct = 100.0f;
  smoothProgress += (targetPct - smoothProgress) * 0.18f; // Slightly snappier progress

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
    // Title fits — draw centered or left-aligned
    u8g2.drawStr(textAreaStart, 21, dTitle);
  } else {
    // Smooth time-based marquee scrolling (sub-pixel precision)
    float maxScroll = (float)(titleW - textAreaWidth);
    switch (titlePhase) {
      case 0: // Pause at start
        titleScrollPx = 0.0f;
        if (now - titlePhaseStart > TITLE_PAUSE_MS) { titlePhase = 1; titlePhaseStart = now; }
        break;
      case 1: // Scroll right (reveal end of title)
        titleScrollPx += TITLE_SCROLL_SPEED * dt;
        if (titleScrollPx >= maxScroll) { titleScrollPx = maxScroll; titlePhase = 2; titlePhaseStart = now; }
        break;
      case 2: // Pause at end
        if (now - titlePhaseStart > TITLE_PAUSE_MS) { titlePhase = 3; titlePhaseStart = now; }
        break;
      case 3: // Scroll back left
        titleScrollPx -= TITLE_SCROLL_SPEED * dt;
        if (titleScrollPx <= 0.0f) { titleScrollPx = 0.0f; titlePhase = 0; titlePhaseStart = now; }
        break;
    }
    u8g2.setClipWindow(textAreaStart, 11, 128, 23);
    u8g2.drawStr(textAreaStart - (int)titleScrollPx, 21, dTitle);
    u8g2.setMaxClipWindow();
  }

  // Artist Name (also scrolls if too long)
  u8g2.setFont(u8g2_font_6x10_tf);
  int artistW = u8g2.getStrWidth(dArtist);
  if (artistW <= textAreaWidth) {
    u8g2.drawStr(textAreaStart, 33, dArtist);
  } else {
    float maxArtistScroll = (float)(artistW - textAreaWidth);
    switch (artistPhase) {
      case 0:
        artistScrollPx = 0.0f;
        if (now - artistPhaseStart > ARTIST_PAUSE_MS) { artistPhase = 1; artistPhaseStart = now; }
        break;
      case 1:
        artistScrollPx += ARTIST_SCROLL_SPEED * dt;
        if (artistScrollPx >= maxArtistScroll) { artistScrollPx = maxArtistScroll; artistPhase = 2; artistPhaseStart = now; }
        break;
      case 2:
        if (now - artistPhaseStart > ARTIST_PAUSE_MS) { artistPhase = 3; artistPhaseStart = now; }
        break;
      case 3:
        artistScrollPx -= ARTIST_SCROLL_SPEED * dt;
        if (artistScrollPx <= 0.0f) { artistScrollPx = 0.0f; artistPhase = 0; artistPhaseStart = now; }
        break;
    }
    u8g2.setClipWindow(textAreaStart, 24, 128, 36);
    u8g2.drawStr(textAreaStart - (int)artistScrollPx, 33, dArtist);
    u8g2.setMaxClipWindow();
  }

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
// Helper: Medical P-Q-R-S-T ECG Heartbeat waveform generator (Pulse offset by distance d behind head)
int getECGPulseOffset(int d) {
  if (d < 0 || d > 24) return 0;
  if (d >= 0 && d <= 3) {
    return (d == 1 || d == 2) ? -2 : -1; // T-wave recovery bump
  } else if (d == 7) {
    return 3;  // S-wave sharp dip
  } else if (d == 8) {
    return -9; // R-wave sharp peak UP!
  } else if (d == 9) {
    return 1;  // Q-wave dip
  } else if (d >= 13 && d <= 16) {
    return -2; // P-wave small bump
  }
  return 0;
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

  // Status Indicator Text (Dynamic Game Presence / Spotify / Standby)
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(58, 41, dSysLabel);
  u8g2.drawStr(58, 48, dSysDetail);

  // HIGH-PRECISION SWEEPING ECG HEARTBEAT PULSE WITH DASHED TRAIL
  int baselineY = 58;
  int minX = 56;
  int maxX = 126;
  int width = maxX - minX + 1; // 71px

  // Pulse cursor head moving smoothly from left to right (sweeping speed: 20ms per pixel)
  int pulseHeadX = minX + (int)(now / 20) % width;

  // 1. Draw sleek dashed baseline across the screen (- - - -)
  for (int x = minX; x <= maxX; x++) {
    if ((x % 3) != 0) {
      u8g2.drawPixel(x, baselineY);
    }
  }

  // 2. Draw solid active ECG heartbeat waveform trailing behind the pulse head
  int prevY = baselineY + getECGPulseOffset(0);
  for (int d = 0; d <= 24; d++) {
    int x = pulseHeadX - d;
    if (x < minX) x += width; // Seamless wrap-around

    int curY = baselineY + getECGPulseOffset(d);
    
    // Clear dashed pixels underneath the active pulse shape
    u8g2.setDrawColor(0);
    u8g2.drawVLine(x, baselineY - 10, 14);
    u8g2.setDrawColor(1);

    if (d > 0) {
      int prevX = pulseHeadX - (d - 1);
      if (prevX < minX) prevX += width;
      
      // Draw solid connecting line segment (avoiding edge wrap-around line break)
      if (abs(x - prevX) == 1) {
        u8g2.drawLine(x, curY, prevX, prevY);
      }
    }
    prevY = curY;
  }

  // 3. Leading Pulse Blip Dot at the pulse head
  int headY = baselineY + getECGPulseOffset(0);
  u8g2.drawDisc(pulseHeadX, headY, 1);

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 3: XENOMORPH LOGO & CLOCK (IDLE LOGO + CLOCK + STATS)
// -----------------------------------------------------------------------------
void drawXenomorphClockScreen() {
  unsigned long now = millis();
  u8g2.clearBuffer();

  // Top Banner / Header (Y: 0..10)
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, dDate);

  u8g2.setDrawColor(1);
  u8g2.drawRBox(54, 0, 74, 10, 2);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_4x6_tf);
  
  // Heart kedat-kedut pulse in XENOMORPH header
  bool hBeat = ((now / 250) % 2 == 0);
  if (hBeat) {
    u8g2.drawStr(58, 7, "XENOMORPH ❤");
  } else {
    u8g2.drawStr(58, 7, "XENOMORPH ⚡");
  }
  u8g2.setDrawColor(1);

  u8g2.drawHLine(0, 11, 128);

  // Animated Xenomorph Logo (Smooth float Y bobbing + subtle breathing)
  int logoY = 13 + (int)(sinf(now / 350.0f) * 2.0f);
  u8g2.drawXBMP(48, logoY, 32, 32, xenomorph_bits);

  // Corrected side brackets [ logo ] surrounding Logo
  int bracketY = 17 + (int)(sinf(now / 350.0f) * 2.0f);
  
  // Left bracket [ (VLine at 41, arms extending right to 43)
  u8g2.drawVLine(41, bracketY, 24);
  u8g2.drawHLine(41, bracketY, 3);
  u8g2.drawHLine(41, bracketY + 23, 3);

  // Right bracket ] (VLine at 84, arms extending left to 82)
  u8g2.drawVLine(84, bracketY, 24);
  u8g2.drawHLine(82, bracketY, 3);
  u8g2.drawHLine(82, bracketY + 23, 3);

  // --- LEFT GAP: CPU & GPU USAGE (%) ---
  u8g2.setFont(u8g2_font_4x6_tf);

  // CPU Usage
  u8g2.drawStr(2, 23, "CPU");
  char cpuBuf[8];
  snprintf(cpuBuf, sizeof(cpuBuf), "%s%%", dCpu);
  int cpuW = u8g2.getStrWidth(cpuBuf);
  u8g2.drawStr(37 - cpuW, 23, cpuBuf);

  // GPU Usage
  u8g2.drawStr(2, 38, "GPU");
  char gpuBuf[8];
  snprintf(gpuBuf, sizeof(gpuBuf), "%s%%", dGpu);
  int gpuW = u8g2.getStrWidth(gpuBuf);
  u8g2.drawStr(37 - gpuW, 38, gpuBuf);

  // --- RIGHT GAP: CPU & GPU TEMPERATURE (°C) ---
  // CPU Temp
  u8g2.drawStr(87, 23, "CPU");
  char cTempBuf[8];
  if (strcmp(dCpuTemp, "0") != 0 && strlen(dCpuTemp) > 0) {
    snprintf(cTempBuf, sizeof(cTempBuf), "%s", dCpuTemp);
  } else {
    snprintf(cTempBuf, sizeof(cTempBuf), "--");
  }
  int ctW = u8g2.getStrWidth(cTempBuf);
  u8g2.drawStr(118 - ctW, 23, cTempBuf);
  u8g2.drawCircle(120, 19, 1); // Degree symbol °
  u8g2.drawStr(123, 23, "C");

  // GPU Temp
  u8g2.drawStr(87, 38, "GPU");
  char gTempBuf[8];
  if (strcmp(dGpuTemp, "0") != 0 && strlen(dGpuTemp) > 0) {
    snprintf(gTempBuf, sizeof(gTempBuf), "%s", dGpuTemp);
  } else {
    snprintf(gTempBuf, sizeof(gTempBuf), "--");
  }
  int gtW = u8g2.getStrWidth(gTempBuf);
  u8g2.drawStr(118 - gtW, 38, gTempBuf);
  u8g2.drawCircle(120, 34, 1); // Degree symbol °
  u8g2.drawStr(123, 38, "C");

  // Large Clock HH:MM:SS at bottom (Y: 60)
  u8g2.setFont(u8g2_font_7x14B_tf);
  int cw = u8g2.getStrWidth(dTime);
  u8g2.drawStr((128 - cw) / 2, 60, dTime);

  u8g2.sendBuffer();
}

// Typewriter & Smooth Scroll State
char lastAiText[256] = "";
int aiTypingLen      = 0;
unsigned long lastTypewriterMs = 0;
unsigned long typingDoneMs     = 0;
float smoothStartLine = 0.0f;

// -----------------------------------------------------------------------------
// SCREEN 4: GENERATIVE AI COMPANION (KIRA)
// -----------------------------------------------------------------------------
void drawAICompanionScreen() {
  u8g2.clearBuffer();

  unsigned long now = millis();

  // Typewriter & Scroll reset check when new message arrives
  if (strcmp(dAiText, lastAiText) != 0) {
    strncpy(lastAiText, dAiText, sizeof(lastAiText) - 1);
    aiTypingLen = 0;
    lastTypewriterMs = now;
    typingDoneMs = 0;
    smoothStartLine = 0.0f;
  }

  // Smooth time-based character advancement (1 character every 25ms)
  int fullTextLen = strlen(dAiText);
  if (aiTypingLen < fullTextLen) {
    int advance = (now - lastTypewriterMs) / 25;
    if (advance > 0) {
      aiTypingLen += advance;
      if (aiTypingLen >= fullTextLen) {
        aiTypingLen = fullTextLen;
        typingDoneMs = now; // Mark time when typing completed
      }
      lastTypewriterMs += advance * 25;
    }
  }

  bool isTalking = (aiTypingLen < fullTextLen);

  // 1. Pre-wrap full text into line segments (up to 16 lines max)
  int maxCharPerLine = 24;
  int lineCount = 0;
  int lineStart[16];
  int lineLen[16];

  int startIdx = 0;
  while (startIdx < fullTextLen && lineCount < 16) {
    lineStart[lineCount] = startIdx;
    int endIdx = startIdx + maxCharPerLine;
    if (endIdx >= fullTextLen) {
      endIdx = fullTextLen;
    } else {
      int spaceIdx = endIdx;
      while (spaceIdx > startIdx && dAiText[spaceIdx] != ' ') {
        spaceIdx--;
      }
      if (spaceIdx > startIdx) {
        endIdx = spaceIdx;
      }
    }
    lineLen[lineCount] = endIdx - startIdx;
    lineCount++;

    startIdx = endIdx;
    if (startIdx < fullTextLen && dAiText[startIdx] == ' ') {
      startIdx++; // Skip space
    }
  }

  // 2. Determine active line being typed to calculate auto-scroll offset
  int activeLine = 0;
  int charsAcc = 0;
  for (int i = 0; i < lineCount; i++) {
    charsAcc += lineLen[i];
    if (aiTypingLen <= charsAcc) {
      activeLine = i;
      break;
    }
    charsAcc++; // count space
  }
  if (aiTypingLen >= fullTextLen) {
    activeLine = (lineCount > 0) ? lineCount - 1 : 0;
  }

  // Calculate target scroll line (Chat box displays 3 lines at a time)
  int targetStartLine = 0;
  if (isTalking) {
    // While typing: follow currently active line
    if (activeLine >= 2) {
      targetStartLine = activeLine - 2;
    }
  } else {
    // When done typing: scroll to the VERY END of the text (showing the last lines)
    if (lineCount > 3) {
      unsigned long elapsedSinceDone = now - typingDoneMs;
      // First 4 seconds after typing: lock view at the end of the text
      if (elapsedSinceDone < 4000) {
        targetStartLine = lineCount - 3;
      } else {
        // Continuous cycle: toggle between end of text and top of text every 4 seconds
        int cycle = ((elapsedSinceDone - 4000) / 4000) % 2;
        targetStartLine = (cycle == 0) ? (lineCount - 3) : 0;
      }
    } else {
      targetStartLine = 0;
    }
  }

  if (targetStartLine > lineCount - 3 && lineCount >= 3) {
    targetStartLine = lineCount - 3;
  }
  if (targetStartLine < 0) targetStartLine = 0;

  // Smooth scroll interpolation
  smoothStartLine += ((float)targetStartLine - smoothStartLine) * 0.15f;

  // Top Banner / Header (Y: 0..10)
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, dDate);

  u8g2.setDrawColor(1);
  u8g2.drawRBox(58, 0, 70, 10, 2);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(64, 7, "KIRA AI (DESK)");
  u8g2.setDrawColor(1);

  u8g2.drawHLine(0, 11, 128);

  // Avatar Wajah Pixel Art Kira (X: 4, Y: 14)
  int ax = 4;
  int ay = 14;

  u8g2.drawRFrame(ax, ay, 28, 22, 2);

  // Animasi Wajah & Mulut Bicara
  bool blink = (now % 3000 < 150);

  if (isTalking) {
    u8g2.drawDisc(ax + 8, ay + 10, 2);
    u8g2.drawDisc(ax + 18, ay + 10, 2);
    if ((now / 120) % 2 == 0) {
      u8g2.drawCircle(ax + 13, ay + 17, 2); // Mulut terbuka
    } else {
      u8g2.drawHLine(ax + 10, ay + 17, 6);  // Mulut tertutup
    }
  } else if (strcmp(dAiEmotion, "BLUSH") == 0) {
    u8g2.drawStr(ax + 6, ay + 12, ">");
    u8g2.drawStr(ax + 17, ay + 12, "<");
    u8g2.drawStr(ax + 5, ay + 18, "//");
    u8g2.drawStr(ax + 17, ay + 18, "//");
  } else if (strcmp(dAiEmotion, "WINK") == 0) {
    u8g2.drawStr(ax + 6, ay + 12, ">");
    u8g2.drawDisc(ax + 18, ay + 10, 2);
    u8g2.drawPixel(ax + 12, ay + 17);
  } else if (strcmp(dAiEmotion, "TALK") == 0) {
    u8g2.drawDisc(ax + 8, ay + 10, 2);
    u8g2.drawDisc(ax + 18, ay + 10, 2);
    u8g2.drawCircle(ax + 13, ay + 17, 2);
  } else if (strcmp(dAiEmotion, "SURPRISED") == 0) {
    u8g2.drawCircle(ax + 8, ay + 10, 3);
    u8g2.drawCircle(ax + 18, ay + 10, 3);
    u8g2.drawCircle(ax + 13, ay + 17, 2);
  } else { // HAPPY / default
    if (blink) {
      u8g2.drawHLine(ax + 6, ay + 10, 5);
      u8g2.drawHLine(ax + 16, ay + 10, 5);
    } else {
      u8g2.drawStr(ax + 6, ay + 12, "^");
      u8g2.drawStr(ax + 16, ay + 12, "^");
    }
    u8g2.drawPixel(ax + 11, ay + 16);
    u8g2.drawPixel(ax + 12, ay + 17);
    u8g2.drawPixel(ax + 13, ay + 16);
  }

  // Label Status di sebelah Avatar (X: 36, Y: 14)
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(36, 21, "KIRA AI:");
  u8g2.setFont(u8g2_font_4x6_tf);
  char emoBuf[24];
  snprintf(emoBuf, sizeof(emoBuf), isTalking ? "[TALKING...]" : "[%s]", dAiEmotion);
  u8g2.drawStr(36, 32, emoBuf);
  u8g2.drawStr(90, 32, dTime);

  // Chat Box Frame (Y: 36..63, height 28)
  u8g2.drawRFrame(0, 36, 128, 28, 2);
  u8g2.setFont(u8g2_font_5x7_tf);

  // Clip window inside Chat Box frame (Y: 38..61)
  u8g2.setClipWindow(2, 38, 126, 61);

  int charsDrawnSoFar = 0;
  for (int i = 0; i < lineCount; i++) {
    float lineYf = 45.0f + ((float)i - smoothStartLine) * 8.0f;
    int lineY = (int)lineYf;

    if (lineY >= 35 && lineY <= 68) {
      int visibleInThisLine = aiTypingLen - charsDrawnSoFar;
      if (visibleInThisLine < 0) visibleInThisLine = 0;
      if (visibleInThisLine > lineLen[i]) visibleInThisLine = lineLen[i];

      if (visibleInThisLine > 0) {
        char lineBuf[28];
        strncpy(lineBuf, dAiText + lineStart[i], visibleInThisLine);
        lineBuf[visibleInThisLine] = '\0';
        u8g2.drawStr(4, lineY, lineBuf);
      }
    }
    charsDrawnSoFar += lineLen[i] + 1; // count space
  }

  u8g2.setMaxClipWindow();

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 5: SYSTEM STATISTICS & NETWORK SPEED
// -----------------------------------------------------------------------------
void drawMiniBar(int x, int y, int w, int h, int percent) {
  // Draw outline box
  u8g2.drawFrame(x, y, w, h);
  int fillW = (percent * (w - 2)) / 100;
  if (fillW > 0) {
    u8g2.drawBox(x + 1, y + 1, fillW, h - 2);
  }
}

void drawSystemStatsScreen() {
  u8g2.clearBuffer();

  // 1. TOP HEADER: DATE & BADGE "SYS STATS" (Y: 0..10)
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(2, 8, dDate);

  // Badge "SYS STATS"
  u8g2.setDrawColor(1);
  u8g2.drawRBox(64, 0, 62, 10, 2);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(69, 7, "SYS STATS ⚡");
  u8g2.setDrawColor(1);

  // Line separator
  u8g2.drawHLine(0, 11, 128);

  // Parse percentages & temps
  int cpuVal  = atoi(dCpu);
  int ramVal  = atoi(dRam);
  int gpuVal  = atoi(dGpu);
  int cpuTemp = atoi(dCpuTemp);
  int gpuTemp = atoi(dGpuTemp);

  u8g2.setFont(u8g2_font_5x7_tf);

  // --- Row 1: CPU (Bar, %, Temp) (Y: 13..20) ---
  u8g2.drawStr(2, 20, "CPU:");
  drawMiniBar(24, 15, 24, 6, cpuVal);
  char cpuBuf[16];
  if (cpuTemp > 0) {
    snprintf(cpuBuf, sizeof(cpuBuf), "%d%%  %dC", cpuVal, cpuTemp);
  } else {
    snprintf(cpuBuf, sizeof(cpuBuf), "%d%%", cpuVal);
  }
  u8g2.drawStr(52, 20, cpuBuf);

  // --- Row 2: GPU (Bar, %, Temp) (Y: 24..31) ---
  u8g2.drawStr(2, 31, "GPU:");
  drawMiniBar(24, 26, 24, 6, gpuVal);
  char gpuBuf[16];
  if (gpuTemp > 0) {
    snprintf(gpuBuf, sizeof(gpuBuf), "%d%%  %dC", gpuVal, gpuTemp);
  } else {
    snprintf(gpuBuf, sizeof(gpuBuf), "%d%%", gpuVal);
  }
  u8g2.drawStr(52, 31, gpuBuf);

  // --- Row 3: RAM (Bar, %) (Y: 35..42) ---
  u8g2.drawStr(2, 42, "RAM:");
  drawMiniBar(24, 37, 24, 6, ramVal);
  char ramBuf[8];
  snprintf(ramBuf, sizeof(ramBuf), "%d%%", ramVal);
  u8g2.drawStr(52, 42, ramBuf);

  // --- Line separator before NET Speed (Y: 46) ---
  u8g2.drawHLine(0, 46, 128);

  // --- Row 4: NET SPEED & TIME (Y: 48..62) ---
  u8g2.setFont(u8g2_font_4x6_tf);
  
  // Draw speed info
  char netBuf[32];
  snprintf(netBuf, sizeof(netBuf), "DL:%s/s  UL:%s/s", dNetDl, dNetUl);
  u8g2.drawStr(2, 57, netBuf);

  // Live clock on bottom right
  u8g2.setFont(u8g2_font_5x7_tf);
  int timeW = u8g2.getStrWidth(dTime);
  u8g2.drawStr(128 - timeW, 57, dTime);

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 6: LAPTOP SPECS & STORAGE (DISK C & D)
// -----------------------------------------------------------------------------
void drawLaptopSpecsScreen() {
  u8g2.clearBuffer();

  // 1. TOP HEADER BOX (Solid full-width header)
  u8g2.setDrawColor(1);
  u8g2.drawRBox(0, 0, 128, 11, 2);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(4, 8, "LAPTOP SPECS");
  
  // Right side of header: OWNER name
  char ownerBuf[24];
  snprintf(ownerBuf, sizeof(ownerBuf), "OWNER:%s", dOwner);
  u8g2.drawStr(128 - u8g2.getStrWidth(ownerBuf) - 4, 8, ownerBuf);
  u8g2.setDrawColor(1);

  // Divider Line
  u8g2.drawHLine(0, 11, 128);

  // 2. HARDWARE SPECS (Using uniform 4x6 font for 100% clean horizontal spacing)
  u8g2.setFont(u8g2_font_4x6_tf);
  
  // Row 1: CPU & RAM (Y = 19)
  u8g2.drawStr(2, 19, "CPU:");
  u8g2.drawStr(20, 19, dCpuModel);
  u8g2.drawStr(92, 19, "RAM:16G");

  // Row 2: GPU (Y = 27)
  u8g2.drawStr(2, 27, "GPU:");
  u8g2.drawStr(20, 27, dGpuModel);

  // Divider Line (Y = 30)
  u8g2.drawHLine(0, 30, 128);

  // 3. STORAGE DISK C & DISK D (Y: 31..64)
  int cPct = atoi(dDiskCPct);
  int dPct = atoi(dDiskDPct);

  // Disk C Row (Y = 39)
  u8g2.drawStr(2, 39, "C:");
  drawMiniBar(14, 34, 20, 6, cPct);
  char cBuf[24];
  snprintf(cBuf, sizeof(cBuf), "%s %d%%", dDiskC, cPct);
  u8g2.drawStr(38, 39, cBuf);

  // Disk D Row (Y = 48)
  u8g2.drawStr(2, 48, "D:");
  drawMiniBar(14, 43, 20, 6, dPct);
  char dBuf[24];
  snprintf(dBuf, sizeof(dBuf), "%s %d%%", dDiskD, dPct);
  u8g2.drawStr(38, 48, dBuf);

  // Total Storage Row (Y = 57)
  u8g2.drawStr(2, 57, "TOT:");
  u8g2.drawStr(22, 57, dDiskTot);

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 7: STARTUP & BACK TO MENU OLED SCREEN WITH INTERACTIVE CURSOR
// -----------------------------------------------------------------------------
unsigned long menuStartMs = 0;

void drawMenuScreen() {
  unsigned long now = millis();

  // Reset intro animation timer whenever entering Screen 7
  if (lastScreenMode != 7) {
    menuStartMs = now;
    lastScreenMode = 7;
  }

  unsigned long elapsed = now - menuStartMs;
  u8g2.clearBuffer();

  if (elapsed < 1200) {
    // =========================================================================
    // TAHAP 1: LOGO XENO FLICKERING (0 - 1.2 SECONDS)
    // =========================================================================
    bool xenoFlicker = ((now / 70) % 2 == 0); // Fast cool glitch/strobe flicker
    
    // Xenomorph Logo (Centered: X = 48, Y = 10)
    if (xenoFlicker) {
      u8g2.drawXBMP(48, 10, 32, 32, xenomorph_bits);
    } else {
      u8g2.drawRFrame(46, 8, 36, 36, 18);
      u8g2.drawXBMP(48, 10, 32, 32, xenomorph_bits);
    }

    // Side brackets flickering
    if ((now / 100) % 2 == 0) {
      u8g2.drawVLine(38, 12, 28);
      u8g2.drawHLine(36, 12, 3);
      u8g2.drawHLine(36, 39, 3);

      u8g2.drawVLine(89, 12, 28);
      u8g2.drawHLine(89, 12, 3);
      u8g2.drawHLine(89, 39, 3);
    }

    u8g2.setFont(u8g2_font_7x14B_tf);
    int tw = u8g2.getStrWidth("XENOMORPH");
    u8g2.drawStr((128 - tw) / 2, 56, "XENOMORPH");

  } else {
    // =========================================================================
    // MAIN MENU SCREEN WITH INTERACTIVE CURSOR SELECTION (70..75)
    // =========================================================================
    float heartPulse = sinf(now / 100.0f) * 0.25f + 1.0f;

    // Header Box with "MAIN MENU"
    u8g2.setDrawColor(1);
    u8g2.drawRBox(0, 0, 128, 11, 2);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(34, 8, "MAIN MENU ❤");

    // Beating Heart icon on header corners
    int hR = (int)(2.5f * heartPulse);
    if ((now / 150) % 2 == 0) {
      drawHeart(10, 5, hR > 1 ? hR : 2);
      drawHeart(118, 5, hR > 1 ? hR : 2);
    }
    u8g2.setDrawColor(1);

    // Extract highlighted cursor index (0..5)
    int cursorIdx = 0;
    if (forceScreenMode >= 70 && forceScreenMode <= 75) {
      cursorIdx = forceScreenMode - 70;
    }

    u8g2.setFont(u8g2_font_4x6_tf);
    const char* items[6] = {
      "[1] Spotify Player",
      "[2] Standby Clock & Face",
      "[3] Xenomorph Logo & Clock",
      "[4] Kira AI Companion",
      "[5] System Stats & Internet",
      "[6] Laptop Specs & Storage"
    };

    for (int i = 0; i < 6; i++) {
      int itemY = 18 + (i * 8);
      if (i == cursorIdx) {
        // Highlight active item with inverted solid box & cursor
        u8g2.setDrawColor(1);
        u8g2.drawRBox(2, itemY - 6, 124, 8, 2);
        u8g2.setDrawColor(0);
        char curBuf[32];
        snprintf(curBuf, sizeof(curBuf), "> %s", items[i]);
        u8g2.drawStr(4, itemY, curBuf);
        u8g2.setDrawColor(1);
      } else {
        u8g2.setDrawColor(1);
        u8g2.drawStr(6, itemY, items[i]);
      }
    }
  }

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// SCREEN 8: HIDDEN LAPTOP SLEEP MODE (MINIMALIST SLEEPING ROBOT & LIVE CLOCK)
// -----------------------------------------------------------------------------
void drawLaptopSleepScreen() {
  u8g2.clearBuffer();
  unsigned long now = millis();

  // Live clock
  char liveTimeStr[16];
  getLiveTimeString(liveTimeStr, sizeof(liveTimeStr));

  // ---------------------------------------------------------------------------
  // 1. CUTE PIXEL ART SLEEPING ROBOT (Centered, Main Character)
  // ---------------------------------------------------------------------------
  int rx = 40;  // Robot center X
  int ry = 22;  // Robot head top Y

  // Gentle breathing bobbing (soft 1px up/down)
  int breathY = (int)(sinf(now / 600.0f) * 1.0f);
  ry += breathY;

  // --- ANTENNA ---
  u8g2.drawVLine(rx, ry - 6, 6);               // Antenna stick
  u8g2.drawDisc(rx, ry - 8, 2);                 // Antenna ball tip

  // --- HEAD (Rounded Helmet) ---
  u8g2.drawRFrame(rx - 14, ry, 28, 20, 5);      // Outer helmet frame

  // --- VISOR (Dark Screen Face) ---
  u8g2.drawRBox(rx - 11, ry + 3, 22, 14, 3);    // Filled dark visor

  // --- SLEEPING EYES (Carved into dark visor: - -) ---
  u8g2.setDrawColor(0);
  u8g2.drawHLine(rx - 8, ry + 9, 5);             // Left eye  - 
  u8g2.drawHLine(rx + 3, ry + 9, 5);             // Right eye -
  // Tiny smile arc under eyes
  u8g2.drawPixel(rx - 2, ry + 12);
  u8g2.drawPixel(rx + 2, ry + 12);
  u8g2.drawHLine(rx - 1, ry + 13, 3);
  u8g2.setDrawColor(1);

  // --- BODY (Small rounded torso) ---
  u8g2.drawRFrame(rx - 10, ry + 21, 20, 12, 4); // Body frame

  // Chest detail: tiny heart pixel art on chest
  u8g2.drawPixel(rx - 2, ry + 25);
  u8g2.drawPixel(rx + 1, ry + 25);
  u8g2.drawPixel(rx - 3, ry + 26);
  u8g2.drawPixel(rx - 1, ry + 26);
  u8g2.drawPixel(rx, ry + 26);
  u8g2.drawPixel(rx + 2, ry + 26);
  u8g2.drawPixel(rx - 2, ry + 27);
  u8g2.drawPixel(rx + 1, ry + 27);
  u8g2.drawPixel(rx - 1, ry + 28);
  u8g2.drawPixel(rx, ry + 28);

  // --- ARMS (Resting down at sides) ---
  u8g2.drawRFrame(rx - 15, ry + 23, 4, 8, 2);   // Left arm
  u8g2.drawRFrame(rx + 11, ry + 23, 4, 8, 2);   // Right arm

  // --- FEET (Two small rounded blocks) ---
  u8g2.drawRBox(rx - 8, ry + 33, 7, 4, 2);      // Left foot
  u8g2.drawRBox(rx + 1, ry + 33, 7, 4, 2);      // Right foot

  // Ground shadow
  u8g2.drawHLine(rx - 16, ry + 38, 33);

  // ---------------------------------------------------------------------------
  // 2. FLOATING "Zzz" (Peaceful Slow Rise from Robot Head)
  // ---------------------------------------------------------------------------
  int z1Y = (int)((now / 130) % 18);
  int z2Y = (int)(((now + 400) / 130) % 18);
  int z3Y = (int)(((now + 800) / 130) % 18);

  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(rx + 16, ry + 2 - z1Y, "Z");

  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(rx + 26, ry - 4 - z2Y, "z");

  u8g2.setFont(u8g2_font_4x6_tf);
  u8g2.drawStr(rx + 34, ry - 8 - z3Y, "z");

  // ---------------------------------------------------------------------------
  // 3. RIGHT SIDE: LARGE LIVE CLOCK & DATE (Clean Typography)
  // ---------------------------------------------------------------------------
  u8g2.setFont(u8g2_font_7x14B_tf);
  int twClock = u8g2.getStrWidth(liveTimeStr);
  u8g2.drawStr(126 - twClock, 14, liveTimeStr);

  // Thin separator
  u8g2.drawHLine(70, 17, 56);

  // Date underneath
  u8g2.setFont(u8g2_font_5x7_tf);
  int twDate = u8g2.getStrWidth(dDate);
  u8g2.drawStr(126 - twDate, 26, dDate);

  // "Sleeping..." label with soft blink
  u8g2.setFont(u8g2_font_4x6_tf);
  if ((now / 800) % 2 == 0) {
    u8g2.drawStr(84, 38, "Sleeping...");
  } else {
    u8g2.drawStr(84, 38, "Sleeping..");
  }

  u8g2.sendBuffer();
}

// -----------------------------------------------------------------------------
// RENDER FRAME ROUTER WITH CUTE MODE TRANSITION INTRO & FAST THUNDER FLASH
// -----------------------------------------------------------------------------
static int activeSelectMode = -1;
static unsigned long selectTransitionMs = 0;
static int previousScreenMode = 7; // Track previous mode (defaults to 7 / Menu)

void renderFrame() {
  unsigned long now = millis();

  // Hidden Feature: Auto override ANY screen to Sleep Mode when Laptop goes to Sleep (No UDP for > 3.5s)
  if (now - recvTimeMs > 3500) {
    drawLaptopSleepScreen();
    return;
  }

  int targetMode = forceScreenMode;
  if (targetMode >= 70 && targetMode <= 75) targetMode = 7; // Map 70..75 cursor to Menu 7

  // Mode selection splash animation when clicking/selecting mode 1 to 6
  if (targetMode >= 1 && targetMode <= 6) {
    if (targetMode != activeSelectMode) {
      activeSelectMode = targetMode;
      selectTransitionMs = now;
    }

    unsigned long elapsed = now - selectTransitionMs;
    unsigned long maxDuration = (previousScreenMode == 7) ? 2400 : 450; // 2.4s from Menu, fast 450ms for direct swap

    if (elapsed < maxDuration) {
      u8g2.clearBuffer();

      if (previousScreenMode == 7) {
        // =========================================================================
        // FROM MENU (7) -> SCREEN 1..6 (TWO CUTE STAGES, 1.2s EACH)
        // =========================================================================
        if (elapsed < 1200) {
          // Stage 1 (0..1.2s): Pulsing Heart + "Hi My Love <3"
          float pulse = sinf((now / 100.0f)) * 0.25f + 1.0f;
          int cx = 64, cy = 20;
          int r = (int)(9 * pulse);
          u8g2.setDrawColor(1);
          drawHeart(cx, cy, r);

          // Twinkling love sparkles on sides
          if ((now / 150) % 2 == 0) {
            u8g2.drawStr(20, 20, "✦");
            u8g2.drawStr(102, 20, "✦");
          }

          u8g2.setFont(u8g2_font_7x14B_tf);
          int tw = u8g2.getStrWidth("Hi My Love <3");
          u8g2.drawStr((128 - tw) / 2, 48, "Hi My Love <3");
          
          u8g2.setFont(u8g2_font_4x6_tf);
          int subW = u8g2.getStrWidth("❤  ❤  ❤");
          u8g2.drawStr((128 - subW) / 2, 58, "❤  ❤  ❤");

        } else {
          // Stage 2 (1.2s..2.4s): Anime Blush Lines (//  \\) + "Welcome Back ^^"
          u8g2.setFont(u8g2_font_6x10_tf);
          u8g2.drawStr(18, 24, "//");
          u8g2.drawStr(98, 24, "\\\\");

          u8g2.setFont(u8g2_font_7x14B_tf);
          int twFace = u8g2.getStrWidth("( > w < )");
          u8g2.drawStr((128 - twFace) / 2, 24, "( > w < )");

          int tw = u8g2.getStrWidth("Welcome Back ^^");
          u8g2.drawStr((128 - tw) / 2, 48, "Welcome Back ^^");

          u8g2.setFont(u8g2_font_4x6_tf);
          int subW = u8g2.getStrWidth("* Glad to see you! *");
          u8g2.drawStr((128 - subW) / 2, 58, "* Glad to see you! *");
        }
      } else {
        // =========================================================================
        // DIRECT SCREEN SWAP 1..6 -> 1..6: FAST 450ms "* THUNDER *" (LIGHTNING FLASH)
        // =========================================================================
        int cx = 64, cy = 20;
        bool flash = ((now / 70) % 2 == 0);
        if (flash) {
          u8g2.drawBox(0, 0, 128, 64);
          u8g2.setDrawColor(0);
        }

        u8g2.drawTriangle(cx + 4, cy - 12, cx - 6, cy + 2, cx + 2, cy + 2);
        u8g2.drawTriangle(cx + 2, cy - 2, cx - 8, cy + 14, cx + 4, cy - 2);

        u8g2.setFont(u8g2_font_7x14B_tf);
        int tw = u8g2.getStrWidth("* THUNDER *");
        u8g2.drawStr((128 - tw) / 2, 52, "* THUNDER *");

        if (flash) u8g2.setDrawColor(1);
      }

      u8g2.sendBuffer();
      return;
    }
  } else {
    activeSelectMode = targetMode;
  }

  if (targetMode == 1) {
    drawSpotifyScreen();
  } else if (targetMode == 2) {
    drawClockAndFaceScreen();
  } else if (targetMode == 3) {
    drawXenomorphClockScreen();
  } else if (targetMode == 4) {
    drawAICompanionScreen();
  } else if (targetMode == 5) {
    drawSystemStatsScreen();
  } else if (targetMode == 6) {
    drawLaptopSpecsScreen();
  } else if (targetMode == 7) {
    drawMenuScreen();
  } else {
    // Mode Otomatis (Auto)
    if (isPlaying) {
      drawSpotifyScreen();
    } else {
      drawClockAndFaceScreen();
    }
  }

  if (targetMode >= 1 && targetMode <= 7) {
    previousScreenMode = targetMode;
    lastScreenMode = targetMode;
  }
}


void setup() {
  Serial.begin(115200);
  u8g2.begin();
  u8g2.setBusClock(400000); // 400kHz Fast I2C

  // Boot screen Hi-Fi Xenomorph
  u8g2.clearBuffer();
  u8g2.drawXBMP(2, 16, 32, 32, xenomorph_bits);

  u8g2.setDrawColor(1);
  u8g2.drawRBox(38, 2, 88, 16, 3);
  u8g2.setDrawColor(0);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(42, 14, "HI-FI XENOMORPH");
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(38, 36, "Initializing...");
  u8g2.sendBuffer();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);

  int retryCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    retryCount++;
    Serial.print(".");

    // Update OLED with Xenomorph logo, animation & retry status
    u8g2.clearBuffer();
    
    // Xenomorph Logo on Left
    u8g2.drawXBMP(2, 16, 32, 32, xenomorph_bits);

    // Badge "HI-FI XENOMORPH"
    u8g2.setDrawColor(1);
    u8g2.drawRBox(38, 0, 88, 14, 3);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(42, 10, "HI-FI XENOMORPH");
    u8g2.setDrawColor(1);

    u8g2.setFont(u8g2_font_4x6_tf);
    u8g2.drawStr(38, 24, "Connecting Wi-Fi...");

    char ssidBuf[28];
    snprintf(ssidBuf, sizeof(ssidBuf), "SSID: %s", ssid);
    u8g2.drawStr(38, 35, ssidBuf);

    // Animated dots + timer indicator
    int dots = (retryCount % 4);
    char animBuf[24];
    snprintf(animBuf, sizeof(animBuf), "Status: Waiting%s (%ds)", 
             dots == 1 ? "." : (dots == 2 ? ".." : (dots == 3 ? "..." : "")), 
             retryCount / 2);
    u8g2.drawStr(38, 46, animBuf);
    u8g2.sendBuffer();

    // Auto retry WiFi.begin() every 30 attempts (~15 seconds) if stuck
    if (retryCount >= 30) {
      WiFi.disconnect();
      delay(200);
      WiFi.begin(ssid, password);
      retryCount = 0;
    }
  }

  u8g2.clearBuffer();
  u8g2.drawXBMP(2, 16, 32, 32, xenomorph_bits);
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(38, 15, "WI-FI CONNECTED!");
  u8g2.setFont(u8g2_font_5x7_tf);
  u8g2.drawStr(38, 30, "IP:");
  u8g2.drawStr(54, 30, WiFi.localIP().toString().c_str());
  u8g2.drawStr(38, 44, "UDP Receiver: 8888");
  u8g2.drawStr(38, 58, "Waiting Streamer...");
  u8g2.sendBuffer();

  udp.begin(UDP_PORT);
  delay(1200);
  titlePhaseStart = millis();
  recvTimeMs = millis();
  forceScreenMode = 7;
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
    prevFrameMs = lastFrameMs; // Store previous for delta time
    lastFrameMs = now;
    renderFrame();
  }
}
