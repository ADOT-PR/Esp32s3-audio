# v4 Build Guide

## New libraries (install via Library Manager)

| Library         | Author          | Notes                            |
|-----------------|-----------------|----------------------------------|
| arduinoFFT      | kosme           | Spectrum analyser FFT            |
| ArduinoOTA      | Arduino         | Built into ESP32 core            |
| WebServer       | Arduino         | Built into ESP32 core            |

Previous (already installed): TFT_eSPI · TJpg_Decoder · ESP32-audioI2S · RTClib

---

## Arduino IDE board settings

| Setting          | Value                              |
|------------------|------------------------------------|
| Board            | ESP32S3 Dev Module                 |
| PSRAM            | OPI PSRAM (if module has it)       |
| Flash Size       | 8MB                                |
| Partition Scheme | Huge APP (3MB + 1MB SPIFFS)        |
| USB Mode         | Hardware CDC and JTAG              |
| CPU Frequency    | 240MHz (code manages scaling)      |

---

## SD card files

```
/wifi.txt        Network name on line 1, password on line 2
/stations.txt    Name|URL per line
/lastfm.txt      api_key, secret, session_key (one per line)

/Music/
  Artist - Album/
    cover.jpg    ← square JPEG for album art
    01-Track.mp3
    02-Track.flac   ← FLAC supported in v4
    playlist.m3u    ← M3U playlist (paths relative to SD root)
```

---

## Battery wiring (GPIO 14)

```
LiPo B+ ── 100KΩ ──┬── 100KΩ ── GND
                   └── GPIO 14 (ADC input)
```
Use 1% resistors. Sampled once at startup before WiFi; cached thereafter.
For live reading with WiFi: add MAX17043 I2C fuel gauge to the I2C bus (GPIO 20/21).

---

## OTA firmware update

1. Player must be on same WiFi as your computer
2. In Arduino IDE: Tools → Port → (network port "audio-player")
3. Upload as normal — player receives update over WiFi
4. Default password: `ota-pass` (change in network.h before first flash)

---

## Web interface

Browse to http://[player-ip]/ for:
- Play/pause/next/prev controls
- Volume slider
- Track search
- Live status (title, artist, progress)

---

## Key shortcuts

| Key   | Action                     | Key   | Action             |
|-------|----------------------------|-------|--------------------|
| Space | Play / Pause               | H     | Toggle shuffle     |
| N     | Next track                 | L     | Cycle repeat       |
| B     | Prev / Back                | X     | Toggle crossfade   |
| + / - | Volume                     | Z     | 30-min sleep timer |
| 1     | Player screen              | /     | Search             |
| F     | Folder browser             | R     | Radio              |
| J / K | Scroll                     | Enter | Select / play      |
| Bksp  | Back / delete search char  | S     | Settings           |

---

## Architecture overview

```
Core 0 (audio task)           Core 1 (Arduino loop)
  audio.loop()         ←←←     xQueueSend(audioQueue)
  audio_filter_samples()  →→→   visBarH[], id3Updated
  EQ biquad filters               KB2 keyboard poll
  FFT visualiser data             TFT screen draws
  audio_id3data()                 Web + OTA service
  audio_eof_*()                   Sleep timer, alarm
```
