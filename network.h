#pragma once
#include "config.h"
// ─────────────────────────────────────────────────────────────────────────────
// WiFi · OTA · Web control interface · NTP sync · Last.fm scrobble
// ─────────────────────────────────────────────────────────────────────────────

WebServer web(80);

// ── Compact web UI (served from RAM) ──────────────────────────────────────────
static const char WEB_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head><meta charset=utf-8><meta name=viewport content='width=device-width'>
<title>Audio Player</title>
<style>body{font-family:monospace;background:#080b12;color:#c8d3e0;max-width:500px;margin:0 auto;padding:16px}
h2{color:#781F88}button{background:#781F88;color:#fff;border:none;padding:8px 16px;margin:4px;cursor:pointer;border-radius:4px}
.row{display:flex;gap:8px;margin:8px 0;align-items:center}input{background:#13161e;border:1px solid #781F88;color:#c8d3e0;padding:6px;border-radius:4px}
#status{background:#13161e;padding:12px;border-radius:8px;margin-bottom:16px}
select{background:#13161e;border:1px solid #781F88;color:#c8d3e0;padding:6px}</style></head>
<body><h2>&#9834; Audio Player</h2>
<div id=status>Loading...</div>
<div class=row>
  <button onclick=cmd('prev')>&#9664;&#9664;</button>
  <button id=pb onclick=cmd('play')>&#9646;&#9646;</button>
  <button onclick=cmd('next')>&#9654;&#9654;</button>
  <button onclick=cmd('shuf')>SHUF</button>
  <button onclick=cmd('rpt')>RPT</button>
</div>
<div class=row>Vol: <input type=range min=0 max=21 id=vol oninput=setVol(this.value)></div>
<div class=row><input placeholder='Search...' id=sq><button onclick=search()>&#128269;</button></div>
<div id=tracks></div>
<script>
function cmd(c){fetch('/cmd?c='+c);}
function setVol(v){fetch('/cmd?c=vol&v='+v);}
function search(){fetch('/search?q='+document.getElementById('sq').value).then(r=>r.json()).then(renderTracks);}
function renderTracks(t){document.getElementById('tracks').innerHTML=t.map((n,i)=>'<div style=padding:4px;cursor:pointer onclick=play('+i+')>'+n+'</div>').join('');}
function play(i){fetch('/cmd?c=play&i='+i);}
function poll(){fetch('/status').then(r=>r.json()).then(s=>{
  document.getElementById('status').innerHTML='&#9834; '+s.title+'<br>'+s.artist+'<br>'+s.time+'/'+s.dur+' &nbsp; Vol:'+s.vol;
  document.getElementById('pb').textContent=s.playing?'&#9646;&#9646;':'&#9654;';
  document.getElementById('vol').value=s.vol;
});}
setInterval(poll,2000);poll();
</script></body></html>)html";

// ── Web server handlers ───────────────────────────────────────────────────────
void setupWebServer(){
  web.on("/",[](){ web.send_P(200,"text/html",WEB_HTML); });

  web.on("/status",[](){
    char buf[256];
    snprintf(buf,255,
      "{"playing":%s,"title":"%s","artist":"%s","
      ""time":"%s","dur":"%s","vol":%d,"batt":%d}",
      isPlaying?"true":"false",dispTitle().c_str(),dispArtist().c_str(),
      fmtTime(elapsed).c_str(),fmtTime(trackDur).c_str(),(int)vol,(int)battPct);
    web.send(200,"application/json",buf);
  });

  web.on("/cmd",[](){
    String c=web.arg("c");
    if(c=="play"){ if(web.hasArg("i"))playTrack(web.arg("i").toInt());else togglePlay();}
    else if(c=="next")  nextTrack();
    else if(c=="prev")  prevTrack();
    else if(c=="shuf")  toggleShuffle();
    else if(c=="rpt")   cycleRepeat();
    else if(c=="vol")   { vol=constrain(web.arg("v").toInt(),0,21); sendAudio(CMD_VOL,vol); }
    web.send(200,"text/plain","ok");
  });

  web.on("/search",[](){
    searchTracks(web.arg("q"));
    String j="["; for(int i=0;i<searchCount;i++){if(i)j+=",";j+="""+baseName(tracks[searchResults[i]])+""";}
    j+="]"; web.send(200,"application/json",j);
  });

  web.begin();
  Serial.printf("[web] http://%s\n",WiFi.localIP().toString().c_str());
}

// ── WiFi + OTA ────────────────────────────────────────────────────────────────
void connectWifi(){
  File f=SD.open("/wifi.txt"); if(!f)return;
  String ssid=f.readStringUntil('\n'); ssid.trim();
  String pass=f.readStringUntil('\n'); pass.trim();
  f.close(); if(!ssid.length())return;
  WiFi.begin(ssid.c_str(),pass.c_str());
  uint32_t t=millis();
  while(WiFi.status()!=WL_CONNECTED&&millis()-t<10000)delay(200);
  wifiOk=(WiFi.status()==WL_CONNECTED);
  if(!wifiOk){Serial.println("[WiFi] failed");return;}

  // OTA
  ArduinoOTA.setHostname("audio-player");
  ArduinoOTA.setPassword("ota-pass");    // change this!
  ArduinoOTA.onStart([](){sendAudio(CMD_STOP);});
  ArduinoOTA.begin();

  // NTP → update RTC
  configTime(0,0,"pool.ntp.org","time.google.com");
  struct tm t2; if(getLocalTime(&t2,5000))
    rtc.adjust(DateTime(t2.tm_year+1900,t2.tm_mon+1,t2.tm_mday,t2.tm_hour,t2.tm_min,t2.tm_sec));

  setupWebServer();
}

// ── Radio stations ────────────────────────────────────────────────────────────
void loadStations(){
  stationCount=0;
  File f=SD.open("/stations.txt");
  if(!f){
    const char* def[][2]={
      {"BBC Radio 1","http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one"},
      {"SomaFM Groove","http://ice1.somafm.com/groovesalad-256-mp3"},
      {"Jazz24","http://live.woub.org/jazz24"},
      {"NTS Radio 1","https://stream-relay-geo.ntslive.net/stream"},
    };
    for(auto& s:def){strncpy(stations[stationCount].name,s[0],47);strncpy(stations[stationCount].url,s[1],199);stationCount++;}
    return;
  }
  while(f.available()&&stationCount<MAX_STATIONS){
    String line=f.readStringUntil('\n');line.trim();
    int sep=line.indexOf('|');if(sep<0)continue;
    line.substring(0,sep).toCharArray(stations[stationCount].name,47);
    line.substring(sep+1).toCharArray(stations[stationCount].url,199);
    stationCount++;
  }
  f.close();
}

// ── Last.fm scrobble ──────────────────────────────────────────────────────────
// Requires Last.fm API key + shared secret. MD5 signing.
// Keys stored in /lastfm.txt: api_key
secret
session_key (from auth flow)
static char lfmSession[64]="";
void loadLastFm(){
  File f=SD.open("/lastfm.txt");if(!f)return;
  f.readStringUntil('\n'); // api key (use in HTTP calls below)
  f.readStringUntil('\n'); // secret
  String sk=f.readStringUntil('\n'); sk.trim(); sk.toCharArray(lfmSession,63);
  f.close();
}
void scrobble(const String& artist, const String& title, uint32_t ts=0){
  if(!wifiOk||!strlen(lfmSession)||artist.length()==0)return;
  // Simplified — production needs HMAC-MD5 signature
  // See: https://www.last.fm/api/show/track.scrobble
  Serial.printf("[lastfm] would scrobble: %s - %s\n",artist.c_str(),title.c_str());
  // Full implementation: build sorted param string, MD5 sign, POST to ws.audioscrobbler.com
}
