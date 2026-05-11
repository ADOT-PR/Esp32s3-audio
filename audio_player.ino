// ─────────────────────────────────────────────────────────────────────────────
// ESP32-S3 Audio Player — v4  (production build)
// audio_player.ino — global instances, setup(), loop(), handleKey()
// ─────────────────────────────────────────────────────────────────────────────
#include "config.h"
#include "audio_engine.h"
#include "ui_manager.h"
#include "features.h"
#include "network.h"

// ── Global instances (declared extern in config.h) ────────────────────────────
TFT_eSPI    tft;
Audio       audio;
RTC_DS1307  rtc;
Preferences prefs;
WebServer   web(80);
QueueHandle_t     audioQueue;
SemaphoreHandle_t sdMutex, visMutex;

// Library data
String      tracks[MAX_TRACKS];
FolderEntry folders[MAX_FOLDERS];
Station     stations[MAX_STATIONS];
int trackCount=0,trackIdx=0,folderCount=0,folderSel=0,stationCount=0,stationSel=0;
int playOrder[MAX_TRACKS],playPos=0;

// State
volatile bool     isPlaying=false, isRadio=false;
volatile uint32_t trackStart=0, trackDur=0, elapsed=0;
volatile uint8_t  vol=12;
bool    shuffleOn=false, crossfadeOn=false, normOn=false;
RepMode rptMode=RPT_ALL;
volatile bool id3Updated=false;
char    id3Title[64]="", id3Artist[64]="", id3Album[64]="", radioTitle[80]="";
float   replayGain=0;
Screen  screen=PLAYER;
bool    blOn=true;
uint32_t lastTouch=0;
char    lastKey=0;
uint32_t lastKeyMs=0;
String  coverPath="";
uint8_t battPct=100;
bool    wifiOk=false;
uint32_t sleepAt=0;
float   visBarH[VIS_BARS]={};
Alarm   alarm;

// ── Deep-sleep inactivity timer ───────────────────────────────────────────────
#define DEEP_SLEEP_AFTER 600000UL   // 10 min of paused inactivity
uint32_t pausedSince=0;

// ── Key handler ───────────────────────────────────────────────────────────────
// KB2 key  │  All screens      │  Context-specific
// ─────────┼───────────────────┼───────────────────────────────────────────────
// Space    │  Play/Pause       │
// N / B    │  Next / Prev      │
// + / -    │  Volume           │
// H        │  Shuffle          │
// L        │  Repeat cycle     │
// X        │  Crossfade toggle │
// Z        │  Sleep timer 30m  │
// 1        │  → Player         │
// F        │  → Folders        │
// R        │  → Radio          │
// S        │  → Settings       │
// /        │  → Search         │
// J / K    │                   │  Scroll down / up
// Enter    │                   │  Select (open / play)
// Bksp     │                   │  Back (search: delete char)
void handleKey(char key){
  blOn_();
  // Global
  if(key==' ')  { togglePlay(); return; }
  if(key=='+')  { vol=min(21u,(uint8_t)(vol+1)); sendAudio(CMD_VOL,vol); if(screen==PLAYER)drawVolume(); return; }
  if(key=='-')  { vol=max(0u,(uint8_t)(vol-1)); sendAudio(CMD_VOL,vol);  if(screen==PLAYER)drawVolume(); return; }
  if(key=='H')  { toggleShuffle();  if(screen==PLAYER)drawControls();  return; }
  if(key=='L')  { cycleRepeat();    if(screen==PLAYER)drawControls();  return; }
  if(key=='X')  { crossfadeOn=!crossfadeOn; saveState(); return; }
  if(key=='Z')  { setSleepTimer(30); if(screen==PLAYER)drawStatusBar(); return; }
  if(key=='1')  { screen=PLAYER;   drawPlayer();   return; }
  if(key=='F')  { screen=FOLDERS;  drawFolders();  return; }
  if(key=='R')  { if(wifiOk){screen=RADIO;drawRadio();} return; }
  if(key=='S')  { screen=SETTINGS; drawSettings(); return; }
  if(key=='/')  { screen=SEARCH; searchQuery=""; searchTracks(""); drawFiles(); return; }

  // Search — each char refines results
  if(screen==SEARCH){
    if(key==8||key==127){ if(searchQuery.length())searchQuery.remove(searchQuery.length()-1); }
    else if(isPrint(key)) searchQuery+=key;
    if(key=='
'||key==13){ if(searchCount){ trackIdx=searchResults[0]; screen=PLAYER; playTrack(trackIdx); drawPlayer(); }}
    else { searchTracks(searchQuery); drawFiles(); }
    return;
  }
  if(screen==FOLDERS){
    if(key=='J'){folderSel=min(folderCount-1,folderSel+1);drawFolders();}
    if(key=='K'){folderSel=max(0,folderSel-1);drawFolders();}
    if(key=='
'||key==13){
      // Find tracks belonging to this folder
      trackCount=0;
      for(int i=0;i<trackCount;i++) { /* filtered in drawFiles via folder path */ }
      // Simple: filter global tracks[] by folder path
      String fp=String(folders[folderSel].path);
      int cnt=0; static String filtered[MAX_TRACKS];
      for(int i=0;i<trackCount;i++) if(folderOf(tracks[i])==fp) filtered[cnt++]=tracks[i];
      for(int i=0;i<cnt;i++) tracks[i]=filtered[i]; trackCount=cnt;
      trackIdx=0; buildOrder(); screen=FILES; drawFiles();
    }
    return;
  }
  if(screen==FILES){
    if(key=='J'){trackIdx=min(trackCount-1,trackIdx+1);drawFiles();}
    if(key=='K'){trackIdx=max(0,trackIdx-1);drawFiles();}
    if(key=='
'||key==13){screen=PLAYER;playTrack(trackIdx);drawPlayer();}
    if(key==8)  {screen=FOLDERS;drawFolders();}
    return;
  }
  if(screen==RADIO&&wifiOk){
    if(key=='J'){stationSel=min(stationCount-1,stationSel+1);drawRadio();}
    if(key=='K'){stationSel=max(0,stationSel-1);drawRadio();}
    if(key=='
'||key==13){playStation(stationSel);drawRadio();}
    if(key=='N'&&isRadio){stationSel=(stationSel+1)%stationCount;playStation(stationSel);drawRadio();}
    return;
  }
  if(screen==PLAYER){
    if(key=='N'){nextTrack();drawPlayer();}
    if(key=='B'){prevTrack();drawPlayer();}
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup(){
  Serial.begin(115200);

  // Power — read battery BEFORE WiFi (ADC2 limitation)
  battPct=readBattery();

  // CPU — boost for init, drop for playback
  setCpuFrequencyMhz(240);

  pinMode(PIN_TFT_BL,OUTPUT); analogWrite(PIN_TFT_BL,BL_MAX);
  Wire.begin(PIN_SDA,PIN_SCL);
  SPI.begin(PIN_SCK,PIN_MISO,PIN_MOSI);

  // TFT
  tft.init(); tft.setRotation(0); tft.fillScreen(C_BG);
  tft.setTextColor(C_ACCT); tft.setTextDatum(MC_DATUM);
  tft.drawString("AUDIO PLAYER v4",SCREEN_W/2,190,4);

  // JPEG + sprites
  TJpgDec.setSwapBytes(true); TJpgDec.setCallback(jpegOutput);
  initSprites();

  // FreeRTOS primitives
  audioQueue=xQueueCreate(10,sizeof(AudioMsg));
  sdMutex   =xSemaphoreCreateMutex();
  visMutex  =xSemaphoreCreateMutex();

  // SD
  tft.setTextColor(C_DIM); tft.drawString("mounting SD...",SCREEN_W/2,230,1);
  if(!SD.begin(PIN_SD_CS,SPI)){
    tft.setTextColor(C_RED); tft.drawString("SD FAILED",SCREEN_W/2,260,4); while(1)delay(1000);}

  // PSRAM report
  if(psramFound()) Serial.printf("[PSRAM] %lu KB\n",ESP.getPsramSize()/1024);

  // Load NVS, scan SD, load stations
  loadState();
  tft.setTextColor(C_DIM); tft.drawString("scanning...",SCREEN_W/2,245,1);
  scanAll();
  folderSel=constrain(folderSel,0,folderCount-1);
  trackIdx =constrain(trackIdx, 0,trackCount-1);
  buildOrder(); rebuildEQ(); loadStations(); loadLastFm();

  // Audio task on Core 0
  xTaskCreatePinnedToCore(audioTask,"audio",8192,nullptr,2,nullptr,0);
  audio.setPinout(PIN_I2S_BCLK,PIN_I2S_LRC,PIN_I2S_DIN);
  sendAudio(CMD_VOL,vol);

  // RTC
  if(!rtc.begin()) Serial.println("[RTC] not found");

  // WiFi (non-blocking display update)
  tft.setTextColor(C_DIM); tft.drawString("wifi...",SCREEN_W/2,260,1);
  connectWifi();

  // Drop CPU for playback
  setCpuFrequencyMhz(80);

  // Auto-play
  coverPath=findCover(folderOf(tracks[trackIdx]));
  playTrack(trackIdx);
  delay(300); drawPlayer(); lastTouch=millis();
}

// ── Loop (Core 1 — UI + input) ────────────────────────────────────────────────
void loop(){
  // KB2 keyboard poll
  if(Wire.requestFrom((uint8_t)0x5F,(uint8_t)1)==1){
    char k=Wire.read();
    if(k&&k!=lastKey&&(millis()-lastKeyMs>150)){lastKey=k;lastKeyMs=millis();handleKey(k);}
    else if(!k) lastKey=0;
  }

  // Track ended → apply repeat mode
  static bool wasPlaying=false;
  if(!isRadio&&wasPlaying&&!isPlaying){
    if(rptMode==RPT_ONE) sendAudio(CMD_PLAY_FILE,trackIdx);
    else if(rptMode==RPT_ALL){ nextTrack(); drawPlayer(); }
    // RPT_OFF: remain stopped
    if(rptMode!=RPT_OFF&&strlen(id3Artist)) scrobble(String(id3Artist),String(id3Title));
  }
  wasPlaying=isPlaying;

  // Progress + marquee update
  if(isPlaying&&!isRadio){
    uint32_t e=(millis()-trackStart)/1000;
    if(e!=elapsed){elapsed=e;if(screen==PLAYER){drawProgress();drawStatusBar();}}
  }
  if(screen==PLAYER&&isPlaying) updateMarquee(dispTitle());
  if(screen==PLAYER&&isPlaying) drawVisualiser();

  // ID3 arrived
  if(id3Updated){ id3Updated=false; if(screen==PLAYER){marqX=0;drawStatusBar();} }

  // Network services
  if(wifiOk){ web.handleClient(); ArduinoOTA.handle(); }

  // Timers
  checkSleepTimer();
  checkAlarm();
  autoBrightness();   // dim at night via RTC

  // Backlight timeout
  if(blOn&&millis()-lastTouch>BL_TIMEOUT) fadeBL(BL_DIM,400);

  // Inactivity deep sleep
  if(!isPlaying){
    if(!pausedSince) pausedSince=millis();
    if(millis()-pausedSince>DEEP_SLEEP_AFTER) enterDeepSleep();
  } else pausedSince=0;
}
