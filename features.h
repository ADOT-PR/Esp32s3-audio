#pragma once
#include "config.h"
// ─────────────────────────────────────────────────────────────────────────────
// NVS · recursive SD scan · M3U playlists · search
// Shuffle · repeat · sleep timer · alarm · playback control
// ─────────────────────────────────────────────────────────────────────────────

// ── NVS ───────────────────────────────────────────────────────────────────────
void saveState(){
  prefs.begin("player",false);
  prefs.putInt("folder",folderSel); prefs.putInt("track",trackIdx);
  prefs.putInt("vol",(int)vol);     prefs.putBool("shuf",shuffleOn);
  prefs.putInt("rpt",(int)rptMode); prefs.putBool("xfade",crossfadeOn);
  prefs.putBool("norm",normOn);
  // Save EQ band gains
  for(int i=0;i<EQ_BANDS;i++) prefs.putFloat("eq"+String(i),eqBands[i].gainDB);
  prefs.end();
}
void loadState(){
  prefs.begin("player",true);
  folderSel  =prefs.getInt("folder",0); trackIdx =prefs.getInt("track",0);
  vol        =prefs.getInt("vol",12);   shuffleOn=prefs.getBool("shuf",false);
  rptMode    =(RepMode)prefs.getInt("rpt",RPT_ALL);
  crossfadeOn=prefs.getBool("xfade",false); normOn=prefs.getBool("norm",false);
  for(int i=0;i<EQ_BANDS;i++) eqBands[i].gainDB=prefs.getFloat("eq"+String(i),0);
  prefs.end();
}

// ── Recursive SD scan ─────────────────────────────────────────────────────────
bool isAudio(const String& n){String l=n;l.toLowerCase();
  return l.endsWith(".mp3")||l.endsWith(".wav")||l.endsWith(".aac")||l.endsWith(".flac");}

static String lastFolderPath="";
void scanDir(const String& path, int depth=0){
  if(depth>4) return;
  File dir=SD.open(path);
  while(File f=dir.openNextFile()){
    if(f.isDirectory()){
      scanDir(path=="/"?"/"+String(f.name()):path+"/"+String(f.name()), depth+1);
    } else if(isAudio(f.name())&&trackCount<MAX_TRACKS){
      String fp=(path=="/")?"/"+String(f.name()):path+"/"+String(f.name());
      tracks[trackCount++]=fp;
      // Register folder if new
      if(path!=lastFolderPath&&folderCount<MAX_FOLDERS){
        lastFolderPath=path;
        strncpy(folders[folderCount].path,path.c_str(),95);
        String nm=path.substring(path.lastIndexOf('/')+1);
        strncpy(folders[folderCount].name,nm.length()?nm.c_str():"Root",47);
        folders[folderCount].count=0; folderCount++;
      }
      // Increment current folder count
      if(folderCount>0) folders[folderCount-1].count++;
    }
    f.close();
  }
  dir.close();
}
void scanAll(){
  trackCount=0; folderCount=0; lastFolderPath="";
  scanDir("/");
  Serial.printf("[SD] %d tracks in %d folders\n",trackCount,folderCount);
}

// ── M3U playlist loader ───────────────────────────────────────────────────────
bool loadPlaylist(const String& path){
  File f=SD.open(path); if(!f)return false;
  trackCount=0;
  while(f.available()&&trackCount<MAX_TRACKS){
    String line=f.readStringUntil('\n'); line.trim();
    if(line.startsWith("#")||!line.length())continue;
    if(!line.startsWith("/"))line="/"+line;
    if(SD.exists(line)) tracks[trackCount++]=line;
  }
  f.close();
  Serial.printf("[M3U] %d tracks\n",trackCount);
  return trackCount>0;
}

// ── Search / filter ───────────────────────────────────────────────────────────
int searchResults[MAX_TRACKS]; int searchCount=0; String searchQuery="";
void searchTracks(const String& q){
  searchQuery=q; searchCount=0;
  String ql=q; ql.toLowerCase();
  for(int i=0;i<trackCount&&searchCount<MAX_TRACKS;i++){
    String n=baseName(tracks[i]); n.toLowerCase();
    if(n.indexOf(ql)>=0) searchResults[searchCount++]=i;
  }
}

// ── Shuffle ───────────────────────────────────────────────────────────────────
void buildOrder(){
  for(int i=0;i<trackCount;i++) playOrder[i]=i;
  if(shuffleOn) for(int i=trackCount-1;i>0;i--){
    int j=random(i+1),t=playOrder[i];playOrder[i]=playOrder[j];playOrder[j]=t;}
  for(int i=0;i<trackCount;i++) if(playOrder[i]==trackIdx){playPos=i;break;}
}
void toggleShuffle(){shuffleOn=!shuffleOn;buildOrder();saveState();}
void cycleRepeat(){rptMode=(RepMode)((rptMode+1)%3);saveState();}

// ── Playback ──────────────────────────────────────────────────────────────────
void playTrack(int idx){
  if(trackCount==0)return;
  isRadio=false; trackIdx=idx; elapsed=0; trackDur=0; replayGain=0;
  id3Title[0]=id3Artist[0]=id3Album[0]=0;
  trackStart=millis();
  if(xSemaphoreTake(sdMutex,pdMS_TO_TICKS(500))==pdTRUE){
    coverPath=findCover(folderOf(tracks[idx])); xSemaphoreGive(sdMutex);}
  if(crossfadeOn) sendAudio(CMD_CROSSFADE,idx);
  else            sendAudio(CMD_PLAY_FILE,idx);
  isPlaying=true; blOn_(); saveState();
}
void nextTrack(){playPos=(playPos+1)%trackCount;playTrack(playOrder[playPos]);}
void prevTrack(){
  if(elapsed>3){sendAudio(CMD_PLAY_FILE,trackIdx);elapsed=0;trackStart=millis();return;}
  playPos=(playPos-1+trackCount)%trackCount;playTrack(playOrder[playPos]);
}
void playStation(int idx){
  isRadio=true; stationSel=idx; radioTitle[0]=id3Title[0]=0;
  sendAudio(CMD_PLAY_URL,0,stations[idx].url); isPlaying=true; blOn_();
}
void togglePlay(){
  sendAudio(CMD_PAUSE); isPlaying=!isPlaying;
  if(isPlaying) trackStart=millis()-elapsed*1000UL;
}

// ── Sleep timer ───────────────────────────────────────────────────────────────
void setSleepTimer(int minutes){ sleepAt=millis()+minutes*60000UL; }
void clearSleepTimer(){ sleepAt=0; }
void checkSleepTimer(){
  if(sleepAt&&millis()>sleepAt){
    sendAudio(CMD_STOP); isPlaying=false; fadeBL(0,1000); sleepAt=0;
  }
}

// ── Alarm clock ───────────────────────────────────────────────────────────────
Alarm alarm;
void checkAlarm(){
  if(!alarm.active||!rtc.isrunning())return;
  DateTime n=rtc.now();
  if(n.hour()==alarm.hour&&n.minute()==alarm.minute){
    blOn_(); playTrack(0); alarm.active=false;
  }
}

// ── Battery ───────────────────────────────────────────────────────────────────
uint8_t readBattery(){
  analogSetAttenuation(ADC_11db);
  float v=(analogRead(PIN_BAT)/4095.f)*3.3f*2.f;
  return (uint8_t)constrain((int)((v-3.3f)/(4.2f-3.3f)*100),0,100);
}

// ── Deep sleep ────────────────────────────────────────────────────────────────
void enterDeepSleep(){
  saveState(); sendAudio(CMD_STOP);
  fadeBL(0,500); delay(300); SD.end();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_21,0); // SDA low = KB2 keypress
  Serial.println("[pwr] deep sleep"); delay(100);
  esp_deep_sleep_start();
}
