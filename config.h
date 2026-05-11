#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Shared config, pins, colours, structs, extern declarations
// ─────────────────────────────────────────────────────────────────────────────
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <Audio.h>
#include <RTClib.h>
#include <arduinoFFT.h>    // ESP32 FFT for spectrum analyser

// ── Pins ──────────────────────────────────────────────────────────────────────
#define PIN_SCK      5
#define PIN_MOSI     6
#define PIN_MISO     7
#define PIN_TFT_CS   8
#define PIN_TFT_DC   9
#define PIN_TFT_RST  10
#define PIN_TFT_BL   11
#define PIN_SD_CS    12
#define PIN_BAT      14   // 100K+100K divider; ADC2 — sample before WiFi.begin()
#define PIN_I2S_BCLK 38
#define PIN_I2S_LRC  39
#define PIN_I2S_DIN  40
#define PIN_SDA      21
#define PIN_SCL      20

// ── Sizes & limits ────────────────────────────────────────────────────────────
#define MAX_TRACKS   500
#define MAX_FOLDERS   96
#define MAX_STATIONS  32
#define FFT_SIZE      64       // spectrum analyser points (power of 2)
#define VIS_BARS      16       // display bars
#define EQ_BANDS       5
#define SAMPLE_RATE 44100

// ── Display ───────────────────────────────────────────────────────────────────
#define SCREEN_W  320
#define SCREEN_H  480
#define ART_X  20
#define ART_Y  28
#define ART_W  280
#define ART_H  180
#define BL_MAX    220
#define BL_DIM     30
#define BL_TIMEOUT 30000

// ── Colours (RGB565) ──────────────────────────────────────────────────────────
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

// ── Enums ─────────────────────────────────────────────────────────────────────
enum Screen  { PLAYER, FOLDERS, FILES, RADIO, SETTINGS, SEARCH };
enum RepMode { RPT_OFF, RPT_ONE, RPT_ALL };
enum AudioCmd{ CMD_PLAY_FILE, CMD_PLAY_URL, CMD_PAUSE, CMD_STOP, CMD_VOL, CMD_CROSSFADE };

// ── Structs ───────────────────────────────────────────────────────────────────
struct AudioMsg { AudioCmd cmd; int param; char url[256]; };

struct BiquadFilter {
  float b0=1,b1=0,b2=0,a1=0,a2=0;
  float x1=0,x2=0,y1=0,y2=0;
  float process(float x){
    float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
    x2=x1;x1=x;y2=y1;y1=y;return y;
  }
  void reset(){ x1=x2=y1=y2=0; }
};

struct EQBand { float freq; float gainDB; float Q; };

struct FolderEntry { char path[96]; char name[48]; int count; };
struct Station     { char name[48]; char url[200]; };
struct Alarm       { uint8_t hour, minute; bool active=false; };

// ── Externs (defined in audio_player.ino) ─────────────────────────────────────
extern TFT_eSPI    tft;
extern Audio       audio;
extern RTC_DS1307  rtc;
extern Preferences prefs;
extern WebServer   web;
extern QueueHandle_t     audioQueue;
extern SemaphoreHandle_t sdMutex;
extern SemaphoreHandle_t visMutex;

// Library data
extern String       tracks[MAX_TRACKS];
extern FolderEntry  folders[MAX_FOLDERS];
extern Station      stations[MAX_STATIONS];
extern int trackCount, trackIdx, folderCount, folderSel, stationCount, stationSel;
extern int playOrder[MAX_TRACKS], playPos;
extern int searchResults[MAX_TRACKS], searchCount;
extern String searchQuery;

// Playback state
extern volatile bool     isPlaying, isRadio;
extern volatile uint32_t trackStart, trackDur, elapsed;
extern volatile uint8_t  vol;

// Modes & metadata
extern bool    shuffleOn, crossfadeOn, normOn;
extern RepMode rptMode;
extern volatile bool id3Updated;
extern char id3Title[64], id3Artist[64], id3Album[64], radioTitle[80];
extern float replayGain;     // dB, from ID3 TXXX tag

// UI & power
extern Screen   screen;
extern bool     blOn;
extern uint32_t lastTouch;
extern char     lastKey;
extern uint32_t lastKeyMs;
extern String   coverPath;
extern uint8_t  battPct;
extern bool     wifiOk;
extern uint32_t sleepAt;     // millis() to stop + sleep; 0=disabled
extern Alarm    alarm;
extern float    visBarH[VIS_BARS];   // visualiser heights 0.0-1.0
extern EQBand   eqBands[EQ_BANDS];

// ── Helper macros ─────────────────────────────────────────────────────────────
inline String baseName(const String& p){int sl=p.lastIndexOf('/'),dt=p.lastIndexOf('.');return p.substring(sl+1,dt>sl?dt:p.length());}
inline String folderOf(const String& p){int sl=p.lastIndexOf('/');return sl<=0?"/":p.substring(0,sl);}
inline String fmtTime(uint32_t s){char b[7];sprintf(b,"%lu:%02lu",s/60,s%60);return b;}
inline String dispTitle(){return strlen(id3Title)?String(id3Title):isRadio?String(stations[stationSel].name):baseName(tracks[trackIdx]);}
inline String dispArtist(){return strlen(id3Artist)?String(id3Artist):isRadio?String(radioTitle):String(folders[folderSel].name);}
