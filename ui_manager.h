#pragma once
#include "config.h"
// ─────────────────────────────────────────────────────────────────────────────
// All display screens · sprite marquee · smooth progress · FFT bars
// Sleep/wake fade · auto-brightness · album art JPEG
// ─────────────────────────────────────────────────────────────────────────────

static TFT_eSprite marqSprite = TFT_eSprite(&tft);  // marquee title bar
static int marqX = 0;
static uint32_t lastMarqMs = 0;
static uint8_t  currentBL  = BL_MAX;

// ── Backlight ─────────────────────────────────────────────────────────────────
void setBL(uint8_t val) { currentBL=val; analogWrite(PIN_TFT_BL, val); }
void blOn_()  { setBL(BL_MAX); blOn=true;  lastTouch=millis(); }
void blOff_() { setBL(0);       blOn=false; }
void fadeBL(uint8_t target, int ms=400) {
  int step=(target>currentBL)?1:-1, steps=abs(target-currentBL);
  int dt=max(1,ms/max(1,steps));
  while(currentBL!=target){currentBL+=step;analogWrite(PIN_TFT_BL,currentBL);delay(dt);}
}
// Dim display at night (RTC required)
void autoBrightness() {
  if (!rtc.isrunning()) return;
  int h=rtc.now().hour();
  uint8_t target=(h>=22||h<7)?BL_DIM:BL_MAX;
  if (abs((int)currentBL-(int)target)>10) fadeBL(target,800);
}

// ── Sprites init ──────────────────────────────────────────────────────────────
void initSprites() {
  marqSprite.createSprite(ART_W, 18);
  marqSprite.setTextColor(C_TEXT, C_BG);
}

// ── Sprite marquee ────────────────────────────────────────────────────────────
void updateMarquee(const String& text) {
  if (millis()-lastMarqMs < 38) return;
  lastMarqMs = millis();
  marqSprite.fillSprite(C_BG);
  marqSprite.drawString(text, -marqX, 1, 2);
  marqSprite.pushSprite(ART_X, ART_Y+ART_H+4);
  int tw = marqSprite.textWidth(text, 2);
  if (tw > ART_W) marqX = (marqX+2) % (tw+30); else marqX=0;
}

// ── Album art JPEG ────────────────────────────────────────────────────────────
bool jpegOutput(int16_t x,int16_t y,uint16_t w,uint16_t h,uint16_t* bmp){
  if(y>=ART_Y+ART_H)return 0; tft.pushImage(x,y,w,h,bmp); return 1;
}
String findCover(const String& folder){
  static const char* n[]={"cover.jpg","Cover.jpg","folder.jpg","artwork.jpg",nullptr};
  for(int i=0;n[i];i++){
    String p=(folder=="/")?"/"+String(n[i]):folder+"/"+String(n[i]);
    if(SD.exists(p))return p;
  }
  return "";
}
void drawAlbumArt() {
  if (coverPath.length()==0) {
    static const uint16_t pal[]={0x780F,0x03EF,0xF811,0x07C4,0xFF40};
    uint16_t ac=pal[trackIdx%5];
    tft.fillRect(ART_X,ART_Y,ART_W,ART_H,tft.color565(8,6,18));
    tft.drawRect(ART_X,ART_Y,ART_W,ART_H,ac);
    tft.setTextColor(ac); tft.setTextDatum(MC_DATUM);
    tft.drawString(isRadio?"((o))":"J",SCREEN_W/2,ART_Y+ART_H/2,isRadio?4:8);
    return;
  }
  uint16_t iw=0,ih=0;
  if(xSemaphoreTake(sdMutex,pdMS_TO_TICKS(500))==pdTRUE){
    TJpgDec.getSdJpgSize(&iw,&ih,coverPath.c_str());
    if(iw){
      uint8_t sc=1; while((iw/sc>ART_W)||(ih/sc>ART_H))sc=(sc<8)?sc*2:8;
      TJpgDec.setJpgScale(sc);
      tft.fillRect(ART_X,ART_Y,ART_W,ART_H,C_BG);
      TJpgDec.drawSdJpg(ART_X+(ART_W-iw/sc)/2,ART_Y+(ART_H-ih/sc)/2,coverPath.c_str());
      if(coverPath.length()) tft.fillCircle(ART_X+ART_W-6,ART_Y+6,4,C_GREEN);
    }
    xSemaphoreGive(sdMutex);
  }
  if(!iw) { coverPath=""; drawAlbumArt(); }
}

// ── Visualiser bars ───────────────────────────────────────────────────────────
void drawVisualiser() {
  int bw=ART_W/VIS_BARS-1, bh=40, y0=ART_Y+ART_H-bh-2;
  // dim overlay so art shows through
  tft.fillRect(ART_X,y0-2,ART_W,bh+4,tft.color565(0,0,4));
  if(xSemaphoreTake(visMutex,0)!=pdTRUE)return;
  for(int b=0;b<VIS_BARS;b++){
    int h=(int)(visBarH[b]*bh);
    int x=ART_X+b*(bw+1);
    tft.fillRect(x,y0+bh-h,bw,h,
      tft.color565(40+(b*200/VIS_BARS),100-(b*50/VIS_BARS),200-(b*150/VIS_BARS)));
  }
  xSemaphoreGive(visMutex);
}

// ── Shared UI elements ────────────────────────────────────────────────────────
void drawStatusBar() {
  tft.fillRect(0,0,SCREEN_W,22,C_BG);
  tft.drawFastHLine(0,22,SCREEN_W,C_ACCT);
  tft.setTextColor(C_ACCT); tft.setTextDatum(TL_DATUM);
  tft.drawString(isRadio?"((o))":"AUDIO",8,4,1);
  if(rtc.isrunning()){DateTime n=rtc.now();char b[6];sprintf(b,"%02d:%02d",n.hour(),n.minute());
    tft.setTextDatum(TC_DATUM);tft.drawString(b,SCREEN_W/2,4,1);}
  // Battery bar
  uint16_t bc=battPct>50?C_GREEN:battPct>20?C_WARN:C_RED;
  tft.drawRect(SCREEN_W-32,4,24,12,C_DIM); tft.fillRect(SCREEN_W-32,4,3,12,C_DIM);
  tft.fillRect(SCREEN_W-31,5,(22*battPct)/100,10,bc);
  if(wifiOk){tft.fillCircle(SCREEN_W-40,10,3,C_ACCT2);}
  // Sleep timer countdown
  if(sleepAt){uint32_t rem=(sleepAt-millis())/60000;
    tft.setTextColor(C_WARN);tft.setTextDatum(TR_DATUM);
    tft.drawString("z"+String(rem)+"m",SCREEN_W-38,4,1);}
}
void drawNavBar() {
  int y=SCREEN_H-28;
  tft.fillRect(0,y,SCREEN_W,28,C_CARD); tft.drawFastHLine(0,y,SCREEN_W,C_ACCT);
  const char* tabs[]={"NOW","FOLD","RADIO","SET"};
  Screen sc[]={PLAYER,FOLDERS,RADIO,SETTINGS}; int tw=SCREEN_W/4;
  for(int i=0;i<4;i++){int x=tw*i+tw/2;bool a=(screen==sc[i]);
    bool ok=(sc[i]!=RADIO||wifiOk);
    tft.setTextColor(a?C_ACCT:ok?C_DIM:C_CARD);
    tft.setTextDatum(TC_DATUM);tft.drawString(tabs[i],x,y+8,1);
    if(a)tft.drawFastHLine(tw*i,y,tw,C_ACCT);}
}

// ── Progress bar (smooth interpolation) ──────────────────────────────────────
static float smoothPct = 0;
void drawProgress() {
  int y=ART_Y+ART_H+26, bw=SCREEN_W-20;
  tft.fillRect(0,y,SCREEN_W,26,C_BG);
  tft.setTextColor(C_DIM);
  tft.setTextDatum(TL_DATUM); tft.drawString(fmtTime(elapsed),10,y,1);
  tft.setTextDatum(TR_DATUM); tft.drawString(trackDur?fmtTime(trackDur):"--:--",SCREEN_W-10,y,1);
  // Smooth progress: interpolate between known elapsed and real-time
  if(trackDur>0){
    float target=(float)elapsed/trackDur;
    smoothPct+=((target+((isPlaying?(millis()-trackStart-elapsed*1000)/1000.0f:0.0f)/trackDur))-smoothPct)*0.15f;
    int fill=constrain((int)(smoothPct*bw),0,bw);
    tft.fillRect(10,y+14,bw,4,C_CARD);
    tft.fillRect(10,y+14,fill,4,C_ACCT);
    tft.fillCircle(10+fill,y+16,5,C_ACCT2);
  }
}
void drawControls(){
  int y=ART_Y+ART_H+54,cx=SCREEN_W/2;
  tft.fillRect(0,y,SCREEN_W,54,C_BG);
  tft.setTextColor(C_ACCT);tft.setTextDatum(MC_DATUM);
  tft.drawString("|<",cx-90,y+26,2); tft.drawString(">|",cx+90,y+26,2);
  tft.fillCircle(cx,y+26,23,C_ACCT);
  tft.setTextColor(C_BG); tft.drawString(isPlaying?"||":" >",cx+(isPlaying?0:2),y+26,2);
  // Shuffle / repeat indicators
  tft.setTextColor(shuffleOn?C_ACCT2:C_DIM); tft.setTextDatum(ML_DATUM);
  tft.drawString("SHUF",10,y+48,1);
  const char* rl[]={"RPT:OFF","RPT:ONE","RPT:ALL"};
  tft.setTextColor(rptMode!=RPT_OFF?C_ACCT2:C_DIM); tft.setTextDatum(MR_DATUM);
  tft.drawString(rl[rptMode],SCREEN_W-10,y+48,1);
}
void drawVolume(){
  int y=ART_Y+ART_H+114;
  tft.fillRect(0,y,SCREEN_W,16,C_BG);
  tft.setTextColor(C_DIM);tft.setTextDatum(TL_DATUM);tft.drawString("VOL",8,y,1);
  int bx=38,bw=SCREEN_W-80;
  tft.fillRect(bx,y+4,bw,6,C_CARD);
  tft.fillRect(bx,y+4,(vol*bw)/21,6,C_ACCT);
  tft.setTextDatum(TR_DATUM);tft.drawString(String(vol),SCREEN_W-8,y,1);
}

// ── Full screen draws ─────────────────────────────────────────────────────────
void drawPlayer(){
  tft.fillScreen(C_BG);
  drawStatusBar();
  if(isPlaying)setCpuFrequencyMhz(80); else setCpuFrequencyMhz(240); // scale for JPEG
  setCpuFrequencyMhz(240);
  drawAlbumArt();
  setCpuFrequencyMhz(80);
  marqX=0;
  drawProgress(); drawControls(); drawVolume(); drawNavBar();
}
void drawFolders(){
  tft.fillScreen(C_BG);drawStatusBar();
  tft.setTextColor(C_ACCT);tft.setTextDatum(TL_DATUM);tft.drawString("FOLDERS",10,28,2);
  tft.setTextColor(C_DIM);tft.drawString(String(folderCount)+" found",200,32,1);
  tft.drawFastHLine(0,48,SCREEN_W,C_ACCT);
  int rowH=34,vis=(SCREEN_H-80)/rowH,start=max(0,folderSel-vis/2),y=52;
  for(int i=start;i<min(folderCount,start+vis);i++){
    bool sel=(i==folderSel);if(sel)tft.fillRect(0,y,SCREEN_W,rowH,0x1082);
    tft.fillRect(10,y+9,14,10,C_FOLD);tft.fillRect(10,y+6,8,5,C_FOLD);
    String nm=String(folders[i].name);if(nm.length()>19)nm=nm.substring(0,19)+"..";
    tft.setTextColor(sel?C_ACCT:C_TEXT);tft.setTextDatum(TL_DATUM);
    tft.drawString((sel?"> ":"  ")+nm,32,y+10,1);
    tft.setTextColor(C_DIM);tft.setTextDatum(TR_DATUM);
    tft.drawString(String(folders[i].count)+" trk",SCREEN_W-8,y+10,1);
    tft.drawFastHLine(0,y+rowH-1,SCREEN_W,C_CARD);y+=rowH;
  }
  drawNavBar();
}
void drawFiles(){
  tft.fillScreen(C_BG);drawStatusBar();
  String title=(screen==SEARCH)?"SEARCH: "+searchQuery:String(folders[folderSel].name);
  tft.setTextColor(C_FOLD);tft.setTextDatum(TL_DATUM);tft.drawString(title,8,28,1);
  int* list=(screen==SEARCH)?searchResults:playOrder;
  int  cnt =(screen==SEARCH)?searchCount:trackCount;
  tft.drawFastHLine(0,40,SCREEN_W,C_ACCT);
  int rowH=28,vis=(SCREEN_H-70)/rowH,start=max(0,trackIdx-vis/2),y=44;
  for(int i=start;i<min(cnt,start+vis);i++){
    int ti=(screen==SEARCH)?list[i]:list[i];
    bool a=(ti==trackIdx);if(a)tft.fillRect(0,y,SCREEN_W,rowH,0x1082);
    String n=baseName(tracks[ti]);if(n.length()>20)n=n.substring(0,20)+"..";
    tft.setTextColor(a?C_ACCT:C_TEXT);tft.setTextDatum(TL_DATUM);
    tft.drawString((a?"> ":"  ")+n,8,y+7,1);
    tft.setTextColor(C_DIM);tft.setTextDatum(TR_DATUM);
    tft.drawString(String(i+1),SCREEN_W-8,y+7,1);
    tft.drawFastHLine(0,y+rowH-1,SCREEN_W,C_CARD);y+=rowH;
  }
  drawNavBar();
}
void drawRadio(){
  tft.fillScreen(C_BG);drawStatusBar();
  tft.setTextColor(C_ACCT);tft.setTextDatum(TL_DATUM);tft.drawString("RADIO",10,28,2);
  if(isRadio){tft.setTextColor(C_GREEN);tft.setTextDatum(TR_DATUM);tft.drawString("LIVE",SCREEN_W-10,32,1);}
  tft.drawFastHLine(0,48,SCREEN_W,C_ACCT);
  if(isRadio){
    tft.fillRect(0,50,SCREEN_W,24,0x0842);
    tft.setTextColor(C_ACCT2);tft.setTextDatum(TC_DATUM);
    String rt=String(radioTitle);if(rt.length()>26)rt=rt.substring(0,26)+"..";
    tft.drawString(rt,SCREEN_W/2,58,1);
  }
  int rowH=32,vis=(SCREEN_H-106)/rowH,start=max(0,stationSel-vis/2),y=isRadio?76:52;
  for(int i=start;i<min(stationCount,start+vis);i++){
    bool sel=(i==stationSel),live=(isRadio&&i==stationSel);
    if(sel)tft.fillRect(0,y,SCREEN_W,rowH,0x1082);
    tft.setTextColor(live?C_GREEN:sel?C_ACCT:C_TEXT);tft.setTextDatum(TL_DATUM);
    String n=String(stations[i].name);if(n.length()>21)n=n.substring(0,21)+"..";
    tft.drawString((sel?"> ":"  ")+(live?"((o)) ":"")+n,8,y+9,1);
    tft.drawFastHLine(0,y+rowH-1,SCREEN_W,C_CARD);y+=rowH;
  }
  drawNavBar();
}
void drawSettings(){
  tft.fillScreen(C_BG);drawStatusBar();
  tft.setTextColor(C_ACCT);tft.setTextDatum(TL_DATUM);tft.drawString("SETTINGS",10,28,2);
  tft.drawFastHLine(0,48,SCREEN_W,C_ACCT);
  const char* keys[]={"Volume","Repeat","Shuffle","Crossfade","Normalise","Battery","WiFi","OTA"};
  String vals[]={String(vol)+"/21",rptMode==RPT_OFF?"Off":rptMode==RPT_ONE?"One":"All",
    shuffleOn?"On":"Off",crossfadeOn?"On":"Off",normOn?"On":"Off",
    String(battPct)+"%",wifiOk?WiFi.localIP().toString():"Off","Enabled"};
  uint16_t vc[]={C_ACCT,C_ACCT2,shuffleOn?C_ACCT2:C_DIM,crossfadeOn?C_ACCT2:C_DIM,
    normOn?C_ACCT2:C_DIM,battPct>20?C_GREEN:C_RED,wifiOk?C_GREEN:C_DIM,C_DIM};
  for(int i=0;i<8;i++){int y=56+i*32;
    tft.setTextColor(C_DIM);tft.setTextDatum(TL_DATUM);tft.drawString(keys[i],12,y+8,1);
    tft.setTextColor(vc[i]);tft.setTextDatum(TR_DATUM);tft.drawString(vals[i],SCREEN_W-12,y+8,2);
    tft.drawFastHLine(0,y+30,SCREEN_W,C_CARD);}
  drawNavBar();
}
