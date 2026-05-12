// ═══════════════════════════════════════════════════════════════════════════
//  ESP32-S3 Audio Player  ·  Complete Ready-to-Flash Sketch  (v4 fixed)
//  Board  : lonely binary Gold Edition (ESP32-S3-WROOM-1U screw terminal)
//
//  FIXES in this version:
//    1. HTML raw string delimiter R"(...)" → R"HTML(...)HTML"
//       Was terminating early when JavaScript contained )"
//    2. Renamed 'alarm' → 'playerAlarm'  (conflicts with POSIX alarm())
//    3. drawRadio() string concat uses String() casts
//    4. Removed analogSetPin() — does not exist on ESP32
//    5. Added forward declaration for setupWebServer()
//    6. Removed stray label in scanAll()
//
//  PARTITION SCHEME (4MB board):
//    Tools → Partition Scheme → Minimal SPIFFS (1.9MB APP / 190KB SPIFFS)
//
//  LIBRARIES (Library Manager):
//    TFT_eSPI · TJpg_Decoder · ESP32-audioI2S · RTClib
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Audio.h>
#include <RTClib.h>

// ── Pins ──────────────────────────────────────────────────────────────────
#define PIN_SCK      5
#define PIN_MOSI     6
#define PIN_MISO     7
#define PIN_TFT_CS   8
#define PIN_TFT_DC   9
#define PIN_TFT_RST  10
#define PIN_TFT_BL   11
#define PIN_SD_CS    12
#define PIN_BAT      14
#define PIN_I2S_BCLK 38
#define PIN_I2S_LRC  39
#define PIN_I2S_DIN  40
#define PIN_SDA      21
#define PIN_SCL      20

// ── Constants ─────────────────────────────────────────────────────────────
#define KB2_ADDR      0x5F
#define MAX_TRACKS    500
#define MAX_FOLDERS    96
#define MAX_STATIONS   20
#define VIS_BARS       16
#define EQ_BANDS        5
#define SCREEN_W      320
#define SCREEN_H      480
#define ART_X          20
#define ART_Y          28
#define ART_W         280
#define ART_H         180
#define BL_MAX        220
#define BL_DIM         30
#define BL_TIMEOUT  30000UL
#define DEEP_SLEEP  600000UL
#define VOL_MAX        21

// ── Colours (RGB565) ──────────────────────────────────────────────────────
#define C_BG    0x0000
#define C_CARD  0x0841
#define C_ACCT  0x781F
#define C_ACCT2 0xA33F
#define C_TEXT  0xF7BE
#define C_DIM   0x4A49
#define C_RED   0xF800
#define C_GREEN 0x07E0
#define C_FOLD  0x3A9F
#define C_WARN  0xFFE0

// ── Enums ─────────────────────────────────────────────────────────────────
enum Screen   { PLAYER, FOLDERS, FILES, RADIO, SETTINGS };
enum RepMode  { RPT_OFF, RPT_ONE, RPT_ALL };
enum AudioCmd { CMD_PLAY_FILE, CMD_PLAY_URL, CMD_PAUSE, CMD_STOP, CMD_VOL };

// ── Structs ───────────────────────────────────────────────────────────────
struct AudioMsg {
  AudioCmd cmd;
  int param;
  char url[256];
};

struct BiquadFilter {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  float process(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
  }
  void reset() {
    x1 = x2 = y1 = y2 = 0;
  }
};

struct EQBand     {
  float freq;
  float gainDB;
  float Q;
};
struct FolderEntry {
  char path[96];
  char name[48];
  int count;
};
struct Station    {
  char name[48];
  char url[200];
};

// FIX 2: named AlarmClock to avoid clash with POSIX alarm() function
struct AlarmClock {
  uint8_t hour = 7, minute = 0;
  bool active = false;
};

// ── Objects ───────────────────────────────────────────────────────────────
TFT_eSPI    tft;
TFT_eSprite marqSpr(&tft);
Audio       audio;
RTC_DS1307  rtc;
Preferences prefs;
WebServer   web(80);
QueueHandle_t     audioQueue;
SemaphoreHandle_t sdMutex;
SemaphoreHandle_t visMutex;

// ── Data ──────────────────────────────────────────────────────────────────
String      tracks[MAX_TRACKS];
FolderEntry folders[MAX_FOLDERS];
Station     stations[MAX_STATIONS];
int trackCount = 0, trackIdx = 0, folderCount = 0, folderSel = 0;
int stationCount = 0, stationSel = 0;
int playOrder[MAX_TRACKS], playPos = 0;

// ── State ─────────────────────────────────────────────────────────────────
volatile bool     isPlaying = false, isRadio = false;
volatile uint32_t trackStart = 0, trackDur = 0, elapsed = 0;
volatile uint8_t  vol = 12;
bool    shuffleOn = false, crossfadeOn = false, normOn = false;
RepMode rptMode = RPT_ALL;

volatile bool id3Updated = false;
char id3Title[64] = "", id3Artist[64] = "", id3Album[64] = "", radioTitle[80] = "";
float replayGain = 0.0f;

Screen   screen = PLAYER;
bool     blOn = true;
uint32_t lastTouch = 0;
char     lastKey = 0;
uint32_t lastKeyMs = 0;
String   coverPath = "";
uint8_t  battPct = 100;
bool     wifiOk = false;
uint32_t sleepAt = 0;
uint32_t pausedSince = 0;
int      marqX = 0;
uint32_t lastMarqMs = 0;
uint8_t  currentBL = BL_MAX;
AlarmClock playerAlarm;  // FIX 2

// ── EQ ────────────────────────────────────────────────────────────────────
BiquadFilter eqL[EQ_BANDS], eqR[EQ_BANDS];
EQBand eqBands[EQ_BANDS] = {
  {60, 0.0f, 1.0f}, {250, 0.0f, 1.0f}, {1000, 0.0f, 1.0f}, {4000, 0.0f, 1.0f}, {16000, 0.0f, 1.0f}
};
float visBarH[VIS_BARS] = {};  // amplitude per bar

// ── Forward declarations ──────────────────────────────────────────────────
void nextTrack();
void prevTrack();
void playTrack(int idx);
void playStation(int idx);
void togglePlay();
void sendAudio(AudioCmd cmd, int param = 0, const char* url = "");
void drawPlayer();
void drawFolders();
void drawFiles();
void drawRadio();
void drawSettings();
void drawStatusBar();
void drawNavBar();
void drawProgress();
void drawControls();
void drawVolume();
void blOn_();
void fadeBL(uint8_t target, int ms = 400);
String findCover(const String& folder);
void saveState();
void setupWebServer();   // FIX 5

// ── Utilities ─────────────────────────────────────────────────────────────
String baseName(const String& p) {
  int sl = p.lastIndexOf('/'), dt = p.lastIndexOf('.');
  return p.substring(sl + 1, dt > sl ? dt : p.length());
}
String folderOf(const String& p) {
  int sl = p.lastIndexOf('/'); return sl <= 0 ? "/" : p.substring(0, sl);
}
String fmtTime(uint32_t s) {
  char b[7]; sprintf(b, "%lu:%02lu", s / 60, s % 60); return String(b);
}
String dispTitle() {
  if (strlen(id3Title))  return String(id3Title);
  if (isRadio)           return String(stations[stationSel].name);
  if (trackCount == 0)     return String("No tracks");
  return baseName(tracks[trackIdx]);
}
String dispArtist() {
  if (strlen(id3Artist)) return String(id3Artist);
  if (isRadio)           return String(radioTitle);
  if (folderCount == 0)    return String("");
  return String(folders[folderSel].name);
}

// ── EQ DSP ────────────────────────────────────────────────────────────────
BiquadFilter makePeak(float fc, float gainDB, float Q, float fs = 44100.f) {
  BiquadFilter f;
  float A = powf(10.f, gainDB / 40.f), w0 = 2.f * PI * fc / fs, al = sinf(w0) / (2.f * Q);
  float a0 = 1.f + al / A;
  f.b0 = (1.f + al * A) / a0; f.b1 = (-2.f * cosf(w0)) / a0; f.b2 = (1.f - al * A) / a0;
  f.a1 = (-2.f * cosf(w0)) / a0; f.a2 = (1.f - al / A) / a0;
  return f;
}
void rebuildEQ() {
  for (int i = 0; i < EQ_BANDS; i++) {
    eqL[i] = makePeak(eqBands[i].freq, eqBands[i].gainDB, eqBands[i].Q);
    eqR[i] = makePeak(eqBands[i].freq, eqBands[i].gainDB, eqBands[i].Q);
    eqL[i].reset(); eqR[i].reset();
  }
}

// ── PCM filter callback ───────────────────────────────────────────────────
bool audio_filter_samples(int16_t* buff, uint16_t len, uint8_t bps, uint8_t ch, uint32_t sr) {
  float rgGain = normOn ? powf(10.f, replayGain / 20.f) : 1.f;
  int segLen = max(1, (int)(len / VIS_BARS));
  for (int i = 0; i < len; i += ch) {
    float l = buff[i] / 32768.f * rgGain;
    float r = (ch > 1 ? buff[i + 1] : buff[i]) / 32768.f * rgGain;
    for (int b = 0; b < EQ_BANDS; b++) {
      l = eqL[b].process(l);
      r = eqR[b].process(r);
    }
    l = constrain(l, -1.f, 1.f); r = constrain(r, -1.f, 1.f);
    buff[i] = (int16_t)(l * 32767.f);
    if (ch > 1) buff[i + 1] = (int16_t)(r * 32767.f);
  }
  if (xSemaphoreTake(visMutex, 0) == pdTRUE) {
    for (int b = 0; b < VIS_BARS; b++) {
      float sum = 0; int st = b * segLen;
      for (int i = st; i < st + segLen && i < len; i++) sum += (buff[i] / 32768.f) * (buff[i] / 32768.f);
      float rms = sqrtf(sum / segLen) * 4.f;
      visBarH[b] = visBarH[b] * 0.72f + constrain(rms, 0.f, 1.f) * 0.28f;
    }
    xSemaphoreGive(visMutex);
  }
  return true;
}

// ── Audio callbacks ───────────────────────────────────────────────────────
void audio_info(const char* info) {
  String s(info);
  if (s.startsWith("Duration:")) {
    int c = s.indexOf(':', 10);
    trackDur = s.substring(10, c).toInt() * 60 + s.substring(c + 1).toInt();
  }
}
void audio_id3data(const char* info) {
  String s(info);
  if     (s.startsWith("Title:"))  {
    s.substring(7).toCharArray(id3Title, 63);
    id3Updated = true;
  }
  else if (s.startsWith("Artist:")) {
    s.substring(8).toCharArray(id3Artist, 63);
    id3Updated = true;
  }
  else if (s.startsWith("Album:"))  {
    s.substring(7).toCharArray(id3Album, 63);
    id3Updated = true;
  }
  else if (s.indexOf("replaygain_track_gain") >= 0) {
    int i = s.lastIndexOf(':'); if (i > 0) replayGain = s.substring(i + 1).toFloat();
  }
}
void audio_showstreamtitle(const char* info) {
  strncpy(radioTitle, info, 79);
  id3Updated = true;
}
void audio_eof_mp3(const char*) {
  isPlaying = false;
}
void audio_eof_wav(const char*) {
  isPlaying = false;
}
void audio_eof_aac(const char*) {
  isPlaying = false;
}
void audio_eof_flac(const char*) {
  isPlaying = false;
}

// ── FreeRTOS audio task (Core 0) ──────────────────────────────────────────
void audioTask(void*) {
  AudioMsg msg;
  for (;;) {
    if (xQueueReceive(audioQueue, &msg, 0) == pdTRUE) {
      switch (msg.cmd) {
        case CMD_PLAY_FILE:
          if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            audio.connecttoFS(SD, tracks[msg.param].c_str());
            xSemaphoreGive(sdMutex);
          }
          isPlaying = true; break;
        case CMD_PLAY_URL:  audio.connecttohost(msg.url); isPlaying = true; break;
        case CMD_PAUSE:     audio.pauseResume();  break;
        case CMD_STOP:      audio.stopSong();     break;
        case CMD_VOL:       audio.setVolume(msg.param); break;
      }
    }
    audio.loop(); vTaskDelay(1);
  }
}
void sendAudio(AudioCmd cmd, int param, const char* url) {
  AudioMsg m; m.cmd = cmd; m.param = param; strncpy(m.url, url, 255);
  xQueueSend(audioQueue, &m, pdMS_TO_TICKS(200));
}

// ── Battery (FIX 4: no analogSetPin) ─────────────────────────────────────
uint8_t readBattery() {
  analogSetAttenuation(ADC_11db);
  float v = (analogRead(PIN_BAT) / 4095.f) * 3.3f * 2.f;
  return (uint8_t)constrain((int)((v - 3.3f) / (4.2f - 3.3f) * 100.f), 0, 100);
}

// ── NVS ──────────────────────────────────────────────────────────────────
void saveState() {
  prefs.begin("player", false);
  prefs.putInt("folder", folderSel); prefs.putInt("track", trackIdx);
  prefs.putInt("vol", (int)vol);     prefs.putBool("shuf", shuffleOn);
  prefs.putInt("rpt", (int)rptMode); prefs.putBool("xfade", crossfadeOn);
  prefs.putBool("norm", normOn);
  for (int i = 0; i < EQ_BANDS; i++) prefs.putFloat(("eq" + String(i)).c_str(), eqBands[i].gainDB);
  prefs.end();
}
void loadState() {
  prefs.begin("player", true);
  folderSel  = prefs.getInt("folder", 0); trackIdx = prefs.getInt("track", 0);
  vol        = (uint8_t)prefs.getInt("vol", 12);
  shuffleOn  = prefs.getBool("shuf", false);
  rptMode    = (RepMode)prefs.getInt("rpt", RPT_ALL);
  crossfadeOn = prefs.getBool("xfade", false);
  normOn     = prefs.getBool("norm", false);
  for (int i = 0; i < EQ_BANDS; i++) eqBands[i].gainDB = prefs.getFloat(("eq" + String(i)).c_str(), 0);
  prefs.end();
}

// ── SD scanning ───────────────────────────────────────────────────────────
bool isAudio(const String& n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".mp3") || l.endsWith(".wav") || l.endsWith(".aac") || l.endsWith(".flac");
}
static String lastScanFolder = "";
void scanDir(const String& path, int depth = 0) {
  if (depth > 4 || trackCount >= MAX_TRACKS)return;
  File dir = SD.open(path); if (!dir)return;
  while (true) {
    File f = dir.openNextFile(); if (!f)break;
    String fname = String(f.name());
    if (f.isDirectory()) {
      String fp = (path == "/") ? "/" + fname : path + "/" + fname;
      scanDir(fp, depth + 1);
    } else if (isAudio(fname) && trackCount < MAX_TRACKS) {
      String fp = (path == "/") ? "/" + fname : path + "/" + fname;
      tracks[trackCount++] = fp;
      if (path != lastScanFolder && folderCount < MAX_FOLDERS) {
        lastScanFolder = path;
        strncpy(folders[folderCount].path, path.c_str(), 95);
        String nm = path.substring(path.lastIndexOf('/') + 1);
        strncpy(folders[folderCount].name, nm.length() ? nm.c_str() : "Root", 47);
        folders[folderCount].count = 0; folderCount++;
      }
      if (folderCount > 0)folders[folderCount - 1].count++;
    }
    f.close();
  }
  dir.close();
}
void scanAll() {
  trackCount = 0; folderCount = 0; lastScanFolder = "";
  scanDir("/");   // FIX 6: stray label removed
  Serial.printf("[SD] %d tracks in %d folders\n", trackCount, folderCount);
}

// ── Shuffle & repeat ──────────────────────────────────────────────────────
void buildOrder() {
  for (int i = 0; i < trackCount; i++) playOrder[i] = i;
  if (shuffleOn)
    for (int i = trackCount - 1; i > 0; i--) {
      int j = random(i + 1), t = playOrder[i]; playOrder[i] = playOrder[j]; playOrder[j] = t;
    }
  for (int i = 0; i < trackCount; i++) if (playOrder[i] == trackIdx) {
      playPos = i;
      break;
    }
}
void toggleShuffle() {
  shuffleOn = !shuffleOn;
  buildOrder();
  saveState();
}
void cycleRepeat()  {
  rptMode = (RepMode)((rptMode + 1) % 3);
  saveState();
}

// ── JPEG album art ────────────────────────────────────────────────────────
bool jpegOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bmp) {
  if (y >= ART_Y + ART_H)return 0; tft.pushImage(x, y, w, h, bmp); return 1;
}
String findCover(const String& folder) {
  static const char* n[] = {"cover.jpg", "Cover.jpg", "folder.jpg", "artwork.jpg", nullptr};
  for (int i = 0; n[i]; i++) {
    String p = (folder == "/") ? "/" + String(n[i]) : folder + "/" + String(n[i]);
    if (SD.exists(p))return p;
  }
  return "";
}
void drawArtPlaceholder() {
  static const uint16_t pal[] = {0x780F, 0x03EF, 0xF811, 0x07C4, 0xFF40};
  uint16_t ac = pal[trackIdx % 5];
  tft.fillRect(ART_X, ART_Y, ART_W, ART_H, tft.color565(8, 6, 18));
  tft.drawRect(ART_X, ART_Y, ART_W, ART_H, ac);
  tft.setTextColor(ac); tft.setTextDatum(MC_DATUM);
  tft.drawString(isRadio ? "((o))" : "J", SCREEN_W / 2, ART_Y + ART_H / 2, isRadio ? 4 : 8);
}
void drawAlbumArt() {
  if (coverPath.length() == 0) {
    drawArtPlaceholder();
    return;
  }
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
    drawArtPlaceholder();
    return;
  }
  uint16_t iw = 0, ih = 0;
  TJpgDec.getSdJpgSize(&iw, &ih, coverPath.c_str());
  if (!iw) {
    xSemaphoreGive(sdMutex);
    drawArtPlaceholder();
    return;
  }
  uint8_t sc = 1;
  while ((iw / sc > ART_W) || (ih / sc > ART_H))sc = (sc < 8) ? sc * 2 : 8;
  TJpgDec.setJpgScale(sc);
  tft.fillRect(ART_X, ART_Y, ART_W, ART_H, C_BG);
  TJpgDec.drawSdJpg(ART_X + (ART_W - iw / sc) / 2, ART_Y + (ART_H - ih / sc) / 2, coverPath.c_str());
  xSemaphoreGive(sdMutex);
  tft.fillCircle(ART_X + ART_W - 8, ART_Y + 8, 5, C_GREEN);
}

// ── Backlight & power ─────────────────────────────────────────────────────
void setBL(uint8_t v) {
  currentBL = v;
  analogWrite(PIN_TFT_BL, v);
}
void blOn_() {
  setBL(BL_MAX);
  blOn = true;
  lastTouch = millis();
}
void blOff_() {
  setBL(0);
  blOn = false;
}
void fadeBL(uint8_t target, int ms) {
  int step = (target > currentBL) ? 1 : -1, steps = abs((int)target - (int)currentBL);
  int dt = max(1, ms / max(1, steps));
  while (currentBL != target) {
    currentBL += step;
    analogWrite(PIN_TFT_BL, currentBL);
    delay(dt);
  }
}
void autoBrightness() {
  if (!rtc.isrunning())return;
  int h = rtc.now().hour();
  uint8_t t = (h >= 22 || h < 7) ? BL_DIM : BL_MAX;
  if (abs((int)currentBL - (int)t) > 20)fadeBL(t, 1000);
}
void enterDeepSleep() {
  saveState(); sendAudio(CMD_STOP); fadeBL(0, 500); delay(200); SD.end();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 0);
  esp_deep_sleep_start();
}

// ── Sleep timer & alarm ───────────────────────────────────────────────────
void setSleepTimer(int minutes) {
  sleepAt = millis() + (uint32_t)minutes * 60000UL;
}
void checkSleepTimer() {
  if (sleepAt && millis() > sleepAt) {
    sendAudio(CMD_STOP);
    isPlaying = false;
    fadeBL(0, 1000);
    sleepAt = 0;
  }
}
void checkAlarm() {
  if (!playerAlarm.active || !rtc.isrunning())return; // FIX 2
  DateTime n = rtc.now();
  if (n.hour() == playerAlarm.hour && n.minute() == playerAlarm.minute) {
    blOn_(); playTrack(0); playerAlarm.active = false;
  }
}

// ── Playback ──────────────────────────────────────────────────────────────
void playTrack(int idx) {
  if (trackCount == 0)return;
  isRadio = false; trackIdx = idx; elapsed = 0; trackDur = 0; replayGain = 0;
  id3Title[0] = id3Artist[0] = id3Album[0] = 0;
  trackStart = millis();
  if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    coverPath = findCover(folderOf(tracks[idx])); xSemaphoreGive(sdMutex);
  }
  if (crossfadeOn) {
    for (int v = vol; v >= 0; v--) {
      audio.setVolume(v);
      delay(18);
    }
    sendAudio(CMD_PLAY_FILE, idx);
    for (int v = 0; v <= (int)vol; v++) {
      audio.setVolume(v);
      delay(18);
    }
  } else sendAudio(CMD_PLAY_FILE, idx);
  isPlaying = true; blOn_(); saveState();
}
void nextTrack() {
  playPos = (playPos + 1) % trackCount;
  playTrack(playOrder[playPos]);
}
void prevTrack() {
  if (elapsed > 3) {
    elapsed = 0;
    trackStart = millis();
    sendAudio(CMD_PLAY_FILE, trackIdx);
    return;
  }
  playPos = (playPos - 1 + trackCount) % trackCount; playTrack(playOrder[playPos]);
}
void playStation(int idx) {
  isRadio = true; stationSel = idx; radioTitle[0] = id3Title[0] = id3Artist[0] = 0;
  sendAudio(CMD_PLAY_URL, 0, stations[idx].url); isPlaying = true; blOn_();
}
void togglePlay() {
  sendAudio(CMD_PAUSE); isPlaying = !isPlaying;
  if (isPlaying)trackStart = millis() - elapsed * 1000UL;
  if (screen == PLAYER)drawControls();
}

// ── Status bar ────────────────────────────────────────────────────────────
void drawStatusBar() {
  tft.fillRect(0, 0, SCREEN_W, 22, C_BG);
  tft.drawFastHLine(0, 22, SCREEN_W, C_ACCT);
  tft.setTextColor(C_ACCT); tft.setTextDatum(TL_DATUM);
  tft.drawString(isRadio ? "((o))" : "AUDIO", 8, 4, 1);
  if (rtc.isrunning()) {
    DateTime n = rtc.now(); char b[6]; sprintf(b, "%02d:%02d", n.hour(), n.minute());
    tft.setTextDatum(TC_DATUM); tft.drawString(b, SCREEN_W / 2, 4, 1);
  }
  uint16_t bc = battPct > 50 ? C_GREEN : battPct > 20 ? C_WARN : C_RED;
  tft.drawRect(SCREEN_W - 32, 4, 24, 12, C_DIM);
  tft.fillRect(SCREEN_W - 32, 4, 3, 12, C_DIM);
  tft.fillRect(SCREEN_W - 31, 5, (22 * battPct) / 100, 10, bc);
  if (wifiOk)tft.fillCircle(SCREEN_W - 42, 10, 3, C_ACCT2);
  if (sleepAt) {
    uint32_t rem = (sleepAt - millis()) / 60000;
    tft.setTextColor(C_WARN); tft.setTextDatum(TR_DATUM);
    tft.drawString("z" + String(rem) + "m", SCREEN_W - 40, 4, 1);
  }
}

// ── Nav bar ───────────────────────────────────────────────────────────────
void drawNavBar() {
  int y = SCREEN_H - 28;
  tft.fillRect(0, y, SCREEN_W, 28, C_CARD); tft.drawFastHLine(0, y, SCREEN_W, C_ACCT);
  const char* tabs[] = {"NOW", "FOLD", "RADIO", "SET"};
  Screen sc[] = {PLAYER, FOLDERS, RADIO, SETTINGS}; int tw = SCREEN_W / 4;
  for (int i = 0; i < 4; i++) {
    int x = tw * i + tw / 2; bool a = (screen == sc[i]), ok = (sc[i] != RADIO || wifiOk);
    tft.setTextColor(a ? C_ACCT : ok ? C_DIM : C_CARD);
    tft.setTextDatum(TC_DATUM); tft.drawString(tabs[i], x, y + 8, 1);
    if (a)tft.drawFastHLine(tw * i, y, tw, C_ACCT);
  }
}

// ── Progress bar ──────────────────────────────────────────────────────────
void drawProgress() {
  int y = ART_Y + ART_H + 26, bw = SCREEN_W - 20;
  tft.fillRect(0, y, SCREEN_W, 26, C_BG);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(TL_DATUM); tft.drawString(fmtTime(elapsed), 10, y, 1);
  tft.setTextDatum(TR_DATUM); tft.drawString(trackDur ? fmtTime(trackDur) : "--:--", SCREEN_W - 10, y, 1);
  tft.fillRect(10, y + 14, bw, 4, C_CARD);
  if (trackDur > 0) {
    float extra = isPlaying ? (millis() - trackStart) / 1000.f - (float)elapsed : 0;
    float pct = ((float)elapsed + extra) / (float)trackDur;
    int fill = constrain((int)(pct * bw), 0, bw);
    tft.fillRect(10, y + 14, fill, 4, C_ACCT);
    tft.fillCircle(10 + fill, y + 16, 5, C_ACCT2);
  }
}

// ── Controls ──────────────────────────────────────────────────────────────
void drawControls() {
  int y = ART_Y + ART_H + 54, cx = SCREEN_W / 2;
  tft.fillRect(0, y, SCREEN_W, 54, C_BG);
  tft.setTextColor(C_ACCT); tft.setTextDatum(MC_DATUM);
  tft.drawString("|<", cx - 90, y + 26, 2); tft.drawString(">|", cx + 90, y + 26, 2);
  tft.fillCircle(cx, y + 26, 23, C_ACCT);
  tft.setTextColor(C_BG); tft.drawString(isPlaying ? "||" : " >", cx + (isPlaying ? 0 : 2), y + 26, 2);
  const char* rl[] = {"RPT:OFF", "RPT:ONE", "RPT:ALL"};
  tft.setTextColor(shuffleOn ? C_ACCT2 : C_DIM); tft.setTextDatum(TL_DATUM); tft.drawString("SHUF", 10, y + 48, 1);
  tft.setTextColor(rptMode != RPT_OFF ? C_ACCT2 : C_DIM); tft.setTextDatum(TR_DATUM); tft.drawString(rl[rptMode], SCREEN_W - 10, y + 48, 1);
}

// ── Volume ────────────────────────────────────────────────────────────────
void drawVolume() {
  int y = ART_Y + ART_H + 114;
  tft.fillRect(0, y, SCREEN_W, 16, C_BG);
  tft.setTextColor(C_DIM); tft.setTextDatum(TL_DATUM); tft.drawString("VOL", 8, y, 1);
  int bx = 38, bw = SCREEN_W - 80;
  tft.fillRect(bx, y + 4, bw, 6, C_CARD);
  tft.fillRect(bx, y + 4, (vol * bw) / VOL_MAX, 6, C_ACCT);
  tft.setTextDatum(TR_DATUM); tft.drawString(String(vol), SCREEN_W - 8, y, 1);
}

// ── Visualiser bars ───────────────────────────────────────────────────────
void drawVisualiser() {
  int bh = 38, y0 = ART_Y + ART_H - bh - 2, bw = ART_W / VIS_BARS - 1;
  if (xSemaphoreTake(visMutex, 0) != pdTRUE)return;
  for (int b = 0; b < VIS_BARS; b++) {
    int h = max(1, (int)(visBarH[b] * bh)), x = ART_X + b * (bw + 1);
    tft.fillRect(x, y0, bw, bh - h, tft.color565(0, 0, 4));
    tft.fillRect(x, y0 + bh - h, bw, h, tft.color565(40 + (b * 180 / VIS_BARS), 100 - (b * 40 / VIS_BARS), 200 - (b * 120 / VIS_BARS)));
  }
  xSemaphoreGive(visMutex);
}

// ── Marquee ───────────────────────────────────────────────────────────────
void updateMarquee(const String& text) {
  if (!marqSpr.created() || millis() - lastMarqMs < 40)return;
  lastMarqMs = millis();
  marqSpr.fillSprite(C_BG); marqSpr.setTextColor(C_TEXT, C_BG);
  marqSpr.drawString(text, -marqX, 1, 2); marqSpr.pushSprite(ART_X, ART_Y + ART_H + 4);
  int tw = marqSpr.textWidth(text, 2);
  if (tw > ART_W)marqX = (marqX + 2) % (tw + 30); else marqX = 0;
}

// ── Screen draws ──────────────────────────────────────────────────────────
void drawPlayer() {
  tft.fillScreen(C_BG); drawStatusBar();
  setCpuFrequencyMhz(240); drawAlbumArt(); setCpuFrequencyMhz(80);
  marqX = 0; drawProgress(); drawControls(); drawVolume(); drawNavBar();
}
void drawFolders() {
  tft.fillScreen(C_BG); drawStatusBar();
  tft.setTextColor(C_ACCT); tft.setTextDatum(TL_DATUM); tft.drawString("FOLDERS", 10, 28, 2);
  tft.setTextColor(C_DIM); tft.drawString(String(folderCount) + " found", 200, 32, 1);
  tft.drawFastHLine(0, 48, SCREEN_W, C_ACCT);
  int rowH = 34, vis = (SCREEN_H - 80) / rowH, start = max(0, folderSel - vis / 2), y = 52;
  for (int i = start; i < min(folderCount, start + vis); i++) {
    bool sel = (i == folderSel); if (sel)tft.fillRect(0, y, SCREEN_W, rowH, 0x1082);
    tft.fillRect(10, y + 9, 14, 10, C_FOLD); tft.fillRect(10, y + 6, 8, 5, C_FOLD);
    String nm = String(folders[i].name); if (nm.length() > 19)nm = nm.substring(0, 19) + "..";
    tft.setTextColor(sel ? C_ACCT : C_TEXT); tft.setTextDatum(TL_DATUM);
    tft.drawString((sel ? "> " : "  ") + nm, 32, y + 10, 1);
    tft.setTextColor(C_DIM); tft.setTextDatum(TR_DATUM);
    tft.drawString(String(folders[i].count) + " trk", SCREEN_W - 8, y + 10, 1);
    tft.drawFastHLine(0, y + rowH - 1, SCREEN_W, C_CARD); y += rowH;
  }
  drawNavBar();
}
void drawFiles() {
  tft.fillScreen(C_BG); drawStatusBar();
  tft.setTextColor(C_FOLD); tft.setTextDatum(TL_DATUM);
  tft.drawString(String(folders[folderSel].name), 8, 28, 1);
  tft.setTextColor(C_DIM); tft.drawString(String(trackCount) + " tracks", 200, 28, 1);
  tft.drawFastHLine(0, 40, SCREEN_W, C_ACCT);
  int rowH = 28, vis = (SCREEN_H - 70) / rowH, start = max(0, trackIdx - vis / 2), y = 44;
  for (int i = start; i < min(trackCount, start + vis); i++) {
    bool a = (i == trackIdx); if (a)tft.fillRect(0, y, SCREEN_W, rowH, 0x1082);
    String n = baseName(tracks[i]); if (n.length() > 20)n = n.substring(0, 20) + "..";
    tft.setTextColor(a ? C_ACCT : C_TEXT); tft.setTextDatum(TL_DATUM);
    tft.drawString((a ? "> " : "  ") + n, 8, y + 7, 1);
    tft.setTextColor(C_DIM); tft.setTextDatum(TR_DATUM); tft.drawString(String(i + 1), SCREEN_W - 8, y + 7, 1);
    tft.drawFastHLine(0, y + rowH - 1, SCREEN_W, C_CARD); y += rowH;
  }
  drawNavBar();
}
void drawRadio() {
  tft.fillScreen(C_BG); drawStatusBar();
  if (!wifiOk) {
    tft.setTextColor(C_WARN); tft.setTextDatum(MC_DATUM);
    tft.drawString("WiFi not connected", SCREEN_W / 2, SCREEN_H / 2, 2);
    tft.drawString("Add /wifi.txt to SD", SCREEN_W / 2, SCREEN_H / 2 + 30, 1);
    drawNavBar(); return;
  }
  tft.setTextColor(C_ACCT); tft.setTextDatum(TL_DATUM); tft.drawString("RADIO", 10, 28, 2);
  if (isRadio) {
    tft.setTextColor(C_GREEN);
    tft.setTextDatum(TR_DATUM);
    tft.drawString("LIVE", SCREEN_W - 10, 32, 1);
  }
  tft.drawFastHLine(0, 48, SCREEN_W, C_ACCT);
  if (isRadio) {
    tft.fillRect(0, 50, SCREEN_W, 24, 0x0842);
    tft.setTextColor(C_ACCT2); tft.setTextDatum(TC_DATUM);
    String rt = String(radioTitle); if (rt.length() > 26)rt = rt.substring(0, 26) + "..";
    tft.drawString(rt, SCREEN_W / 2, 58, 1);
  }
  int rowH = 32, vis = (SCREEN_H - 106) / rowH, start = max(0, stationSel - vis / 2), y = isRadio ? 76 : 52;
  for (int i = start; i < min(stationCount, start + vis); i++) {
    bool sel = (i == stationSel), live = (isRadio && i == stationSel);
    if (sel)tft.fillRect(0, y, SCREEN_W, rowH, 0x1082);
    // FIX 3: String() casts allow + operator to work with const char*
    String label = String(sel ? "> " : "  ") + String(live ? "((o)) " : "") + String(stations[i].name);
    if (label.length() > 28)label = label.substring(0, 28) + "..";
    tft.setTextColor(live ? C_GREEN : sel ? C_ACCT : C_TEXT); tft.setTextDatum(TL_DATUM);
    tft.drawString(label, 8, y + 9, 1);
    tft.drawFastHLine(0, y + rowH - 1, SCREEN_W, C_CARD); y += rowH;
  }
  drawNavBar();
}
void drawSettings() {
  tft.fillScreen(C_BG); drawStatusBar();
  tft.setTextColor(C_ACCT); tft.setTextDatum(TL_DATUM); tft.drawString("SETTINGS", 10, 28, 2);
  tft.drawFastHLine(0, 48, SCREEN_W, C_ACCT);
  const char* keys[] = {"Volume", "Repeat", "Shuffle", "Crossfade", "Normalise", "Battery", "WiFi", "EQ"};
  String vals[] = {
    String(vol) + "/" + String(VOL_MAX),
    rptMode == RPT_OFF ? "Off" : rptMode == RPT_ONE ? "One" : "All",
    shuffleOn ? "On" : "Off", crossfadeOn ? "On" : "Off", normOn ? "On" : "Off",
    String(battPct) + "%", wifiOk ? WiFi.localIP().toString() : "Off", "J/K bands"
  };
  uint16_t vc[] = {C_ACCT, C_ACCT2, shuffleOn ? C_ACCT2 : C_DIM, crossfadeOn ? C_ACCT2 : C_DIM,
                   normOn ? C_ACCT2 : C_DIM, battPct > 20 ? C_GREEN : C_RED, wifiOk ? C_GREEN : C_DIM, C_DIM
                  };
  for (int i = 0; i < 8; i++) {
    int y = 56 + i * 32;
    tft.setTextColor(C_DIM); tft.setTextDatum(TL_DATUM); tft.drawString(keys[i], 12, y + 8, 1);
    tft.setTextColor(vc[i]); tft.setTextDatum(TR_DATUM); tft.drawString(vals[i], SCREEN_W - 12, y + 8, 2);
    tft.drawFastHLine(0, y + 30, SCREEN_W, C_CARD);
  }
  drawNavBar();
}

// ── Network ───────────────────────────────────────────────────────────────
void loadStations() {
  stationCount = 0;
  File f = SD.open("/stations.txt");
  if (!f) {
    const char* def[][2] = {
      {"BBC Radio 1",  "http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one"},
      {"SomaFM Groove", "http://ice1.somafm.com/groovesalad-256-mp3"},
      {"Jazz24",       "http://live.woub.org/jazz24"},
      {"NTS Radio 1",  "https://stream-relay-geo.ntslive.net/stream"},
    };
    for (auto& s : def) {
      strncpy(stations[stationCount].name, s[0], 47);
      strncpy(stations[stationCount].url, s[1], 199);
      stationCount++;
    }
    return;
  }
  while (f.available() && stationCount < MAX_STATIONS) {
    String line = f.readStringUntil('\n'); line.trim();
    int sep = line.indexOf('|'); if (sep < 0)continue;
    line.substring(0, sep).toCharArray(stations[stationCount].name, 47);
    line.substring(sep + 1).toCharArray(stations[stationCount].url, 199);
    stationCount++;
  }
  f.close();
}

// FIX 1: Raw string delimiter changed from R"(...)" to R"HTML(...)HTML"
// The old delimiter was being closed early by )" appearing inside the JavaScript.
void setupWebServer() {
  static const char WEB_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>
<title>Audio Player</title><style>
*{box-sizing:border-box}body{font-family:monospace;background:#080b12;color:#c8d3e0;margin:0;padding:12px}
h2{color:#a78bfa;margin:0 0 12px}.box{background:#13161e;border:1px solid #1e2330;border-radius:8px;padding:12px;margin-bottom:12px}
button{background:#781F88;color:#fff;border:none;padding:8px 14px;margin:3px;cursor:pointer;border-radius:4px}
input[type=range]{width:100%;accent-color:#781F88}.trk{padding:6px;cursor:pointer;border-bottom:1px solid #0d1117}
.trk:hover{background:#1e2330}
</style></head><body>
<h2>&#9834; Audio Player</h2>
<div class=box id=status>Connecting...</div>
<div class=box>
  <button onclick="cmd('prev')">|&lt;</button>
  <button id=pb onclick="cmd('play')">&#9654;</button>
  <button onclick="cmd('next')">&gt;|</button>
  &nbsp;
  <button onclick="cmd('shuf')">SHUF</button>
  <button onclick="cmd('rpt')">RPT</button>
  <button onclick="cmd('xfade')">XFADE</button>
</div>
<div class=box>Volume: <input type=range min=0 max=21 id=vol oninput="cmd('vol&v='+this.value)"></div>
<div class=box id=tracks></div>
<script>
function cmd(c){fetch('/cmd?c='+c);}
function poll(){
  fetch('/status').then(function(r){return r.json();}).then(function(s){
    document.getElementById('status').innerHTML='&#9834; '+s.title+'<br>'+s.artist+'<br>'+s.time+'/'+s.dur+' Vol:'+s.vol+' Bat:'+s.batt+'%25';
    document.getElementById('pb').textContent=s.playing?'||':'&#9654;';
    document.getElementById('vol').value=s.vol;
  });
  fetch('/tracks').then(function(r){return r.json();}).then(function(t){
    var h='';for(var i=0;i<t.length;i++)h+='<div class=trk onclick="cmd(\'play&i='+i+'\')">'+(i+1)+'. '+t[i]+'</div>';
    document.getElementById('tracks').innerHTML=h;
  });
}
setInterval(poll,2500);poll();
</script></body></html>)HTML";

  web.on("/", []() {
    web.send_P(200, "text/html", WEB_HTML);
  });
  web.on("/status", []() {
    char buf[320];
    snprintf(buf, 319, "{\"playing\":%s,\"title\":\"%s\",\"artist\":\"%s\",\"time\":\"%s\",\"dur\":\"%s\",\"vol\":%d,\"batt\":%d}",
             isPlaying ? "true" : "false", dispTitle().c_str(), dispArtist().c_str(),
             fmtTime(elapsed).c_str(), fmtTime(trackDur).c_str(), (int)vol, (int)battPct);
    web.send(200, "application/json", buf);
  });
  web.on("/tracks", []() {
    String j = "[";
    for (int i = 0; i < trackCount; i++) {
      if (i)j += ",";
      j += "\"" + baseName(tracks[i]) + "\"";
    }
    j += "]"; web.send(200, "application/json", j);
  });
  web.on("/cmd", []() {
    String c = web.arg("c");
    if     (c == "play") {
      if (web.hasArg("i"))playTrack(web.arg("i").toInt());
      else togglePlay();
    }
    else if (c == "next") nextTrack();
    else if (c == "prev") prevTrack();
    else if (c == "shuf") toggleShuffle();
    else if (c == "rpt")  cycleRepeat();
    else if (c == "xfade") {
      crossfadeOn = !crossfadeOn;
      saveState();
    }
    else if (c == "vol")  {
      vol = constrain(web.arg("v").toInt(), 0, VOL_MAX);
      sendAudio(CMD_VOL, vol);
    }
    web.send(200, "text/plain", "ok");
  });
  web.begin();
  Serial.printf("[web] http://%s\n", WiFi.localIP().toString().c_str());
}

void connectWifi() {
  File f = SD.open("/wifi.txt"); if (!f)return;
  String ssid = f.readStringUntil('\n'); ssid.trim();
  String pass = f.readStringUntil('\n'); pass.trim();
  f.close(); if (!ssid.length())return;
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 10000)delay(200);
  wifiOk = (WiFi.status() == WL_CONNECTED);
  if (!wifiOk) {
    Serial.println("[WiFi] failed");
    return;
  }
  ArduinoOTA.setHostname("audio-player");
  ArduinoOTA.setPassword("ota1234");
  ArduinoOTA.onStart([] {sendAudio(CMD_STOP);});
  ArduinoOTA.begin();
  configTime(0, 0, "pool.ntp.org");
  struct tm tm; if (getLocalTime(&tm, 5000))
    rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
  setupWebServer();  // FIX 5: compiles because forward declaration is above
}

// ── Key handler ───────────────────────────────────────────────────────────
void handleKey(char key) {
  blOn_();
  if (key == ' ') {
    togglePlay();
    return;
  }
  if (key == '+') {
    vol = min((int)VOL_MAX, (int)vol + 1);
    sendAudio(CMD_VOL, vol);
    if (screen == PLAYER)drawVolume();
    return;
  }
  if (key == '-') {
    vol = max(0, (int)vol - 1);
    sendAudio(CMD_VOL, vol);
    if (screen == PLAYER)drawVolume();
    return;
  }
  if (key == 'H') {
    toggleShuffle();
    if (screen == PLAYER)drawControls();
    return;
  }
  if (key == 'L') {
    cycleRepeat();
    if (screen == PLAYER)drawControls();
    return;
  }
  if (key == 'X') {
    crossfadeOn = !crossfadeOn;
    saveState();
    return;
  }
  if (key == 'Z') {
    setSleepTimer(30);
    if (screen == PLAYER)drawStatusBar();
    return;
  }
  if (key == '1') {
    screen = PLAYER;
    drawPlayer();
    return;
  }
  if (key == 'F') {
    screen = FOLDERS;
    drawFolders();
    return;
  }
  if (key == 'R') {
    screen = RADIO;
    drawRadio();
    return;
  }
  if (key == 'S') {
    screen = SETTINGS;
    drawSettings();
    return;
  }
  if (screen == FOLDERS) {
    if (key == 'J') {
      folderSel = min(folderCount - 1, folderSel + 1);
      drawFolders();
    }
    if (key == 'K') {
      folderSel = max(0, folderSel - 1);
      drawFolders();
    }
    if (key == '\n' || key == 13) {
      String fp = String(folders[folderSel].path);
      int cnt = 0; static String buf[MAX_TRACKS];
      for (int i = 0; i < trackCount; i++)if (folderOf(tracks[i]) == fp)buf[cnt++] = tracks[i];
      if (cnt > 0) {
        for (int i = 0; i < cnt; i++)tracks[i] = buf[i];
        trackCount = cnt;
      }
      trackIdx = 0; buildOrder(); screen = FILES; drawFiles();
    }
    return;
  }
  if (screen == FILES) {
    if (key == 'J') {
      trackIdx = min(trackCount - 1, trackIdx + 1);
      drawFiles();
    }
    if (key == 'K') {
      trackIdx = max(0, trackIdx - 1);
      drawFiles();
    }
    if (key == '\n' || key == 13) {
      screen = PLAYER;
      playTrack(trackIdx);
      drawPlayer();
    }
    if (key == 'B' || key == 8) {
      screen = FOLDERS;
      drawFolders();
    }
    return;
  }
  if (screen == RADIO) {
    if (key == 'J') {
      stationSel = min(stationCount - 1, stationSel + 1);
      drawRadio();
    }
    if (key == 'K') {
      stationSel = max(0, stationSel - 1);
      drawRadio();
    }
    if (key == '\n' || key == 13) {
      playStation(stationSel);
      drawRadio();
    }
    if (key == 'N' && isRadio) {
      stationSel = (stationSel + 1) % stationCount;
      playStation(stationSel);
      drawRadio();
    }
    return;
  }
  if (screen == PLAYER) {
    if (key == 'N') {
      nextTrack();
      drawPlayer();
    }
    if (key == 'B')prevTrack();
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  // FIX 4: readBattery() called directly — analogSetPin() does not exist
  battPct = readBattery();

  pinMode(PIN_TFT_BL, OUTPUT); analogWrite(PIN_TFT_BL, BL_MAX);
  Wire.begin(PIN_SDA, PIN_SCL);
  SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI);

  tft.init(); tft.setRotation(1); tft.fillScreen(C_BG);
  tft.setTextColor(C_ACCT); tft.setTextDatum(MC_DATUM);
  tft.drawString("AUDIO PLAYER", SCREEN_W / 2, 180, 4);
  tft.setTextColor(C_DIM); tft.drawString("ESP32-S3  v4", SCREEN_W / 2, 215, 2);

  TJpgDec.setSwapBytes(true); TJpgDec.setCallback(jpegOutput);
  marqSpr.createSprite(ART_W, 18);

  audioQueue = xQueueCreate(10, sizeof(AudioMsg));
  sdMutex = xSemaphoreCreateMutex();
  visMutex = xSemaphoreCreateMutex();

  tft.setTextColor(C_DIM); tft.drawString("Mounting SD...", SCREEN_W / 2, 250, 1);
  if (!SD.begin(PIN_SD_CS, SPI)) {
    tft.setTextColor(C_RED);
    tft.drawString("SD CARD FAILED", SCREEN_W / 2, 270, 4);
    tft.drawString("Check wiring + FAT32", SCREEN_W / 2, 310, 1);
    while (true)delay(1000);
  }

  if (psramFound())Serial.printf("[PSRAM] %lu KB\n", ESP.getPsramSize() / 1024);

  loadState();
  tft.setTextColor(C_DIM); tft.drawString("Scanning...", SCREEN_W / 2, 265, 1);
  scanAll();
  folderSel = constrain(folderSel, 0, max(0, folderCount - 1));
  trackIdx = constrain(trackIdx, 0, max(0, trackCount - 1));
  buildOrder(); rebuildEQ(); loadStations();

  xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 2, nullptr, 0);
  audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
  sendAudio(CMD_VOL, vol);

  if (!rtc.begin())Serial.println("[RTC] not found");

  tft.setTextColor(C_DIM); tft.drawString("WiFi...", SCREEN_W / 2, 280, 1);
  connectWifi();
  setCpuFrequencyMhz(80);

  if (trackCount > 0) {
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
      coverPath = findCover(folderOf(tracks[trackIdx])); xSemaphoreGive(sdMutex);
    }
    playTrack(trackIdx);
  }
  delay(300); drawPlayer(); lastTouch = millis();
}

// ── Loop ──────────────────────────────────────────────────────────────────
void loop() {
  if (Wire.requestFrom((uint8_t)KB2_ADDR, (uint8_t)1) == 1) {
    char k = Wire.read();
    if (k && k != lastKey && (millis() - lastKeyMs > 150)) {
      lastKey = k;
      lastKeyMs = millis();
      handleKey(k);
    }
    else if (!k)lastKey = 0;
  }

  static bool wasPlaying = false;
  if (!isRadio && wasPlaying && !isPlaying) {
    if     (rptMode == RPT_ONE)sendAudio(CMD_PLAY_FILE, trackIdx);
    else if (rptMode == RPT_ALL) {
      nextTrack();
      if (screen == PLAYER)drawPlayer();
    }
  }
  wasPlaying = isPlaying;

  if (isPlaying && !isRadio) {
    uint32_t e = (millis() - trackStart) / 1000;
    if (e != elapsed) {
      elapsed = e;
      if (screen == PLAYER) {
        drawProgress();
        drawStatusBar();
      }
    }
  }

  if (screen == PLAYER && isPlaying) {
    updateMarquee(dispTitle());
    drawVisualiser();
  }
  if (id3Updated) {
    id3Updated = false;
    if (screen == PLAYER)drawStatusBar();
    marqX = 0;
  }

  if (wifiOk) {
    web.handleClient();
    ArduinoOTA.handle();
  }

  checkSleepTimer(); checkAlarm(); autoBrightness();

  if (blOn && (millis() - lastTouch > BL_TIMEOUT))fadeBL(BL_DIM, 600);

  if (!isPlaying) {
    if (!pausedSince)pausedSince = millis();
    if (millis() - pausedSince > DEEP_SLEEP)enterDeepSleep();
  } else pausedSince = 0;
}
