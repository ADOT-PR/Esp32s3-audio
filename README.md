# 🎵 ESP32-S3 Audio Player

A full-featured portable audio player built on the **ESP32-S3** with a 3.5″ TFT display, stereo I²S amplifiers, SD card storage, physical keyboard, and internet radio streaming.

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-teal)
![Audio](https://img.shields.io/badge/audio-MP3%20%7C%20WAV%20%7C%20AAC%20%7C%20FLAC-purple)
![License](https://img.shields.io/badge/license-MIT-green)

-----

## 🚀 Quick Start — First Flash

Follow these steps in order. The whole process takes about 10 minutes.

### Step 1 — Create the sketch folder

Create a folder named exactly `audio_player`. Put `audio_player.ino` inside it.
The folder name and file name **must match**.

```
audio_player/
└── audio_player.ino
```

### Step 2 — Fix the TFT library

Copy `User_Setup.h` from this repo into your TFT_eSPI library folder,
replacing the file that’s already there:

```
Arduino/libraries/TFT_eSPI/User_Setup.h   ← replace this
```

> **Why?** TFT_eSPI needs to know your exact pin wiring at compile time.
> Without this step the display will stay blank.

### Step 3 — Install ESP32 board support

Open Arduino IDE → **File → Preferences** → paste into *Additional Boards Manager URLs*:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then: **Tools → Board Manager** → search **esp32** → install **esp32 by Espressif Systems**

### Step 4 — Install libraries

Open **Sketch → Include Library → Manage Libraries** and install all four:

|Library       |Author      |Search for      |
|--------------|------------|----------------|
|TFT_eSPI      |Bodmer      |`TFT_eSPI`      |
|TJpg_Decoder  |Bodmer      |`TJpg_Decoder`  |
|ESP32-audioI2S|schreibfaul1|`ESP32-audioI2S`|
|RTClib        |Adafruit    |`RTClib`        |

These are already included and need no install: `SD` `SPI` `Wire` `WiFi` `Preferences` `ArduinoOTA` `WebServer`

### Step 5 — Set board settings

**Tools** menu — set every option exactly as shown:

|Setting         |Value                                              |
|----------------|---------------------------------------------------|
|Board           |**ESP32S3 Dev Module**                             |
|USB Mode        |**Hardware CDC and JTAG**                          |
|Upload Speed    |**921600**                                         |
|Flash Size      |**8MB (64Mb)**                                     |
|Partition Scheme|**Huge APP (3MB no OTA)**                          |
|CPU Frequency   |**240MHz**                                         |
|PSRAM           |**Disabled** *(or OPI PSRAM if your module has it)*|

### Step 6 — Flash

1. Plug the board in via USB
1. Select the correct port under **Tools → Port**
1. Click **Upload** (the arrow button)
1. Wait for *Done uploading*

> If upload fails with a timeout error, hold the **BOOT** button on the board
> while clicking Upload, then release it once uploading starts.

### Step 7 — Prepare the SD card

Format as **FAT32**. Drop your music in folders and add these optional config files:

```
/wifi.txt        →  line 1: network name,  line 2: password
/stations.txt    →  internet radio  (Name|URL per line)
```

Insert the SD card and power on — the player will scan all folders then auto-play.

### What happens on first boot

```
Splash screen
  ↓
Mounting SD...       (red text + halt if SD fails — check wiring)
  ↓
Scanning tracks...   (recursive scan of all folders)
  ↓
Connecting WiFi...   (skipped silently if no wifi.txt on SD)
  ↓
Now Playing screen   (auto-plays last track from NVS, or track 1)
```

### After first flash — OTA wireless updates

Once WiFi is connected you never need the USB cable again:

1. Arduino IDE → **Tools → Port** → select the network port named **audio-player**
1. Click Upload as normal — firmware uploads over WiFi
1. Default OTA password: `ota1234` *(change in the sketch before first flash)*

-----

## ✨ Features

### Audio

- MP3, WAV, AAC, and FLAC playback from SD card
- Stereo I²S output via dual MAX98357A amplifiers
- 5-band software EQ (biquad DSP, runs on Core 0)
- ReplayGain volume normalisation (reads ID3 TXXX tag)
- Crossfade between tracks (volume ramp)
- Proper audio buffering — FreeRTOS audio task pinned to Core 0

### Display & UI

- 3.5″ ST7796 TFT (320×480) with JPEG album art
- Real-time FFT spectrum analyser (16 bars)
- Sprite-based scrolling marquee for long titles
- Smooth interpolated progress bar
- Sleep/wake backlight fade animation
- Auto-brightness via RTC (dim at night)
- Battery percentage display

### Playback

- Shuffle (Fisher-Yates) and Repeat (Off / One / All)
- M3U playlist support
- Full-text track search
- Sleep timer (30-minute countdown)
- RTC alarm clock — starts playing at set time
- Last position memory — resumes after power off (NVS)
- Recursive SD card folder scanner

### Networking

- Internet radio streaming (Shoutcast / Icecast)
- Web control interface — play, search, volume from any browser
- OTA firmware updates over WiFi
- NTP time sync → RTC auto-set on boot
- Last.fm scrobbling

### Power

- Deep sleep after 10 min inactivity (wakes on keypress)
- CPU frequency scaling (80 MHz playback, 240 MHz for JPEG/OTA)
- Battery ADC with voltage divider

-----

## 🛠️ Hardware

|Component      |Part             |Notes                                               |
|---------------|-----------------|----------------------------------------------------|
|Microcontroller|ESP32-S3-WROOM-1U|lonely binary Gold Edition (screw terminal breakout)|
|Display        |3.5″ TFT ST7796  |SPI mode, 320×480, 3.3V                             |
|Amplifiers     |MAX98357A × 2    |I²S stereo, 3W each                                 |
|Speakers       |4Ω 3W × 2        |                                                    |
|SD card        |Deek-Robot module|5V logic — needs level converter                    |
|Keyboard       |M5Stack Card KB2 |I²C, address 0x5F                                   |
|RTC            |DS1307           |I²C, CR2032 backup                                  |
|Battery        |LiPo 3.7V 5000mAh|Meshnology or equivalent                            |
|Charger/Boost  |LX-LCBST         |Charges LiPo + boosts to 5V                         |
|Logic Converter|4CH LLC          |lonely binary — 3.3V ↔ 5V for SD SPI                |

-----

## 📌 Wiring

### Pin Assignments (ESP32-S3 lonely binary Gold)

**Left terminal strip**

|Terminal|GPIO   |Signal         |Connect to                  |
|--------|-------|---------------|----------------------------|
|3V3     |—      |3.3V power     |TFT VCC, RTC, KB2           |
|GND     |—      |Ground         |All modules                 |
|5       |GPIO 5 |SPI SCL (clock)|TFT SCL, SD SCK (via LLC)   |
|6       |GPIO 6 |SPI SDA (data) |TFT SDA, SD MOSI (via LLC)  |
|7       |GPIO 7 |SPI MISO       |SD MISO (via LLC)           |
|8       |GPIO 8 |TFT CS         |TFT CS                      |
|9       |GPIO 9 |TFT DC         |TFT DC                      |
|10      |GPIO 10|TFT RST        |TFT RST                     |
|11      |GPIO 11|TFT BL         |TFT BL (PWM backlight)      |
|12      |GPIO 12|SD CS          |SD CS (via LLC)             |
|14      |GPIO 14|Battery ADC    |Voltage divider from LiPo B+|

**Right terminal strip**

|Terminal|GPIO   |Signal    |Connect to            |
|--------|-------|----------|----------------------|
|3V3     |—      |3.3V power|MAX98357A VIN × 2     |
|GND     |—      |Ground    |All modules           |
|40      |GPIO 40|I²S DIN   |Both MAX98357A DIN    |
|39      |GPIO 39|I²S LRC   |Both MAX98357A LRC    |
|38      |GPIO 38|I²S BCLK  |Both MAX98357A BCLK   |
|21      |GPIO 21|I²C SDA   |RTC SDA, KB2 SDA (G25)|
|20      |GPIO 20|I²C SCL   |RTC SCL, KB2 SCL (G26)|

**Bottom pads (solder directly)**

|Pad|Connect to  |
|---|------------|
|GND|LX-LCBST VO−|
|5V |LX-LCBST VO+|

### Stereo Channel Selection (MAX98357A)

- **Left amp:** SD_MODE pin → GND
- **Right amp:** SD_MODE pin → floating (or 3V3)

### Battery Voltage Divider (GPIO 14)

```
LiPo B+ ── 100KΩ ──┬── 100KΩ ── GND
                   └── GPIO 14
```

Use 1% tolerance resistors. GPIO 14 is ADC2 — sampled once before WiFi starts, then cached.

### I²C Pull-ups

Add **4.7KΩ** resistors from SDA and SCL to 3V3.

### Logic Level Converter

Put the 4CH LLC on the SD SPI lines (SCL, SDA, MISO, CS):

- **LV side** → 3.3V + ESP32 GPIO signals
- **HV side** → 5V + SD card module signals

-----

## 💻 Software Setup

> **First time?** Follow the [🚀 Quick Start](#-quick-start--first-flash) section at the top —
> it walks through every step with exact settings.

### TFT_eSPI pin configuration (User_Setup.h)

The `User_Setup.h` in this repo is pre-configured for this project’s wiring.
Copy it to `Arduino/libraries/TFT_eSPI/User_Setup.h` before compiling.

```cpp
#define ST7796_DRIVER      // 3.5" ST7796S controller
#define TFT_MOSI  6        // SDA on TFT board label
#define TFT_SCLK  5        // SCL on TFT board label
#define TFT_CS    8
#define TFT_DC    9
#define TFT_RST   10
// BL (GPIO 11) controlled via analogWrite() — not defined here
// No MISO — TFT is write-only
#define SPI_FREQUENCY 40000000
```

### Required libraries

|Library       |Author      |Install via    |
|--------------|------------|---------------|
|TFT_eSPI      |Bodmer      |Library Manager|
|TJpg_Decoder  |Bodmer      |Library Manager|
|ESP32-audioI2S|schreibfaul1|Library Manager|
|RTClib        |Adafruit    |Library Manager|

Built-in (no install needed): `SD` `SPI` `Wire` `WiFi` `Preferences` `ArduinoOTA` `WebServer`

### Board settings summary

|Setting         |Value                                 |
|----------------|--------------------------------------|
|Board           |ESP32S3 Dev Module                    |
|USB Mode        |Hardware CDC and JTAG                 |
|Upload Speed    |921600                                |
|Flash Size      |8MB (64Mb)                            |
|Partition Scheme|**Huge APP (3MB no OTA)**             |
|CPU Frequency   |240MHz                                |
|PSRAM           |Disabled *(or OPI PSRAM if available)*|

-----

## 📂 File Structure

```
audio_player/              ← folder name must match the .ino filename
├── audio_player.ino       Complete single-file sketch (ready to flash)
└── User_Setup.h           Copy this to Arduino/libraries/TFT_eSPI/
```

-----

## 💾 SD Card Layout

Format as **FAT32**. Place files in the root:

```
/SD root
├── wifi.txt          Line 1: network name  Line 2: password
├── stations.txt      Name|URL per line (internet radio)
├── lastfm.txt        api_key / secret / session_key (one per line)
│
├── Artist - Album/
│   ├── cover.jpg     Album art (any JPEG, square preferred, <200KB)
│   ├── 01-Track.mp3
│   ├── 02-Track.flac
│   └── playlist.m3u  Optional playlist (paths relative to SD root)
└── ...
```

**Recognised cover filenames:** `cover.jpg`, `Cover.jpg`, `folder.jpg`, `artwork.jpg`

### wifi.txt example

```
MyNetworkName
MyPassword123
```

### stations.txt example

```
BBC Radio 1|http://stream.live.vc.bbcmedia.co.uk/bbc_radio_one
SomaFM Groove Salad|http://ice1.somafm.com/groovesalad-256-mp3
Jazz24|http://live.woub.org/jazz24
NTS Radio 1|https://stream-relay-geo.ntslive.net/stream
```

-----

## ⌨️ Keyboard Shortcuts (Card KB2)

|Key      |Action            |Key      |Action                    |
|---------|------------------|---------|--------------------------|
|`Space`  |Play / Pause      |`H`      |Toggle shuffle            |
|`N`      |Next track        |`L`      |Cycle repeat (Off→One→All)|
|`B`      |Prev / Back       |`X`      |Toggle crossfade          |
|`+` / `-`|Volume up / down  |`Z`      |Set 30-min sleep timer    |
|`1`      |Now Playing screen|`F`      |Folder browser            |
|`R`      |Radio screen      |`S`      |Settings                  |
|`/`      |Search            |`J` / `K`|Scroll down / up          |
|`Enter`  |Select / play     |`Bksp`   |Back / delete search char |

-----

## 🌐 Web Interface

Once connected to WiFi, browse to `http://[player-ip]/` for:

- Play / Pause / Next / Prev controls
- Volume slider
- Track search
- Live status — title, artist, elapsed time

API endpoints (JSON):

```
GET /status          → current playback state
GET /cmd?c=play      → play/pause
GET /cmd?c=next      → next track
GET /cmd?c=vol&v=15  → set volume (0–21)
GET /search?q=jazz   → search tracks
```

-----

## 📡 OTA Updates

1. Connect player to WiFi (needs `wifi.txt` on SD card)
1. In Arduino IDE: Tools → Port → select the network port **audio-player**
1. Upload as normal — no USB required
1. Default OTA password: `ota1234` *(change in the sketch before first flash)*

-----

## 🏗️ Architecture

```
┌─────────────────────────────────────────────────────────────┐
│  Core 1 (Arduino loop — UI & input)                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ KB2 poll │  │ TFT draw │  │ Web/OTA  │  │ Timers   │   │
│  └────┬─────┘  └──────────┘  └──────────┘  └──────────┘   │
│       │ xQueueSend()                  ↑ volatile globals    │
├───────┼───────────────────────────────┼─────────────────────┤
│  Core 0 (audio task)                  │                     │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  audio.loop()  →  audio_filter_samples()             │   │
│  │                    ├── 5-band biquad EQ (per sample) │   │
│  │                    └── FFT → visBarH[] (per block)   │   │
│  │  audio_id3data()  → id3Title / id3Artist / replayGain│   │
│  │  audio_eof_*()    → isPlaying = false                │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘

SD card access protected by sdMutex (both cores use SD)
visBarH[] protected by visMutex (written Core 0, read Core 1)
```

-----

## 🔧 Troubleshooting

|Problem            |Fix                                                                                                               |
|-------------------|------------------------------------------------------------------------------------------------------------------|
|No sound           |Check I²S pin order (BCLK 38, LRC 39, DIN 40). Verify MAX98357A VIN is 3.3V.                                      |
|TFT blank          |Check SPI wiring. Confirm `User_Setup.h` is replaced in library folder. Verify IM0 jumper is soldered on TFT back.|
|SD not found       |Check level converter wiring (LLC HV = 5V, LV = 3.3V). Reformat SD as FAT32.                                      |
|Audio dropouts     |Verify FreeRTOS task is on Core 0. Increase `audioQueue` size in `config.h`.                                      |
|KB2 not responding |Add 4.7KΩ pull-ups on SDA/SCL to 3V3. Verify I²C address is 0x5F.                                                 |
|WiFi not connecting|Check `wifi.txt` has no blank lines or trailing spaces.                                                           |
|Compile fails      |Select `Huge APP` partition scheme. Ensure all 5 libraries are installed.                                         |

-----

## 📝 License

MIT License — free to use, modify, and distribute. Attribution appreciated.

-----

## 👤 Creator

<img width="633" height="686" alt="image" src="https://github.com/user-attachments/assets/a2035524-adb0-4616-8687-f021f7a68a25" />

**ADOTPR** — concept, hardware design, wiring, and project direction.

<br clear="left"/>

-----

## 🙏 Credits

- [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) — audio engine
- [Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) — display driver
- [Bodmer/TJpg_Decoder](https://github.com/Bodmer/TJpg_Decoder) — JPEG decoding
- [lonely binary](https://lonelybinary.com) — ESP32-S3 Gold Edition board
- [M5Stack](https://m5stack.com) — Card KB2 keyboard
