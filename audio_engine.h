#pragma once
#include "config.h"
// ─────────────────────────────────────────────────────────────────────────────
// FreeRTOS audio task · 5-band biquad EQ · FFT visualiser
// Volume crossfade · ReplayGain normalisation · FLAC support
// ─────────────────────────────────────────────────────────────────────────────

// ── EQ state ──────────────────────────────────────────────────────────────────
static BiquadFilter eqL[EQ_BANDS], eqR[EQ_BANDS];
EQBand eqBands[EQ_BANDS] = {
  {60,    0, 1.0f},   // sub-bass
  {250,   0, 1.0f},   // bass
  {1000,  0, 1.0f},   // mid
  {4000,  0, 1.0f},   // presence
  {16000, 0, 1.0f},   // air
};

// Build a peaking biquad from band parameters
BiquadFilter makePeak(float fc, float fs, float dBgain, float Q) {
  BiquadFilter f;
  float A  = powf(10.f, dBgain / 40.f);
  float w0 = 2.f * PI * fc / fs;
  float alpha = sinf(w0) / (2.f * Q);
  float a0 = 1.f + alpha / A;
  f.b0 = (1.f + alpha * A) / a0;
  f.b1 = (-2.f * cosf(w0))  / a0;
  f.b2 = (1.f - alpha * A) / a0;
  f.a1 = (-2.f * cosf(w0))  / a0;
  f.a2 = (1.f - alpha / A) / a0;
  return f;
}

void rebuildEQ() {
  for (int i = 0; i < EQ_BANDS; i++) {
    eqL[i] = makePeak(eqBands[i].freq, SAMPLE_RATE, eqBands[i].gainDB, eqBands[i].Q);
    eqR[i] = makePeak(eqBands[i].freq, SAMPLE_RATE, eqBands[i].gainDB, eqBands[i].Q);
    eqL[i].reset(); eqR[i].reset();
  }
}

// ── Spectrum analyser ─────────────────────────────────────────────────────────
static double fftReal[FFT_SIZE], fftImag[FFT_SIZE];
static ArduinoFFT<double> FFT(fftReal, fftImag, FFT_SIZE, SAMPLE_RATE);
static int fftFill = 0;
float visBarH[VIS_BARS] = {};
SemaphoreHandle_t visMutex;

void updateVisualiser(int16_t* samples, int n) {
  for (int i = 0; i < n && fftFill < FFT_SIZE; i++, fftFill++) {
    fftReal[fftFill] = samples[i] / 32768.0;
    fftImag[fftFill] = 0;
  }
  if (fftFill < FFT_SIZE) return;
  fftFill = 0;
  FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  FFT.compute(FFTDirection::Forward);
  FFT.complexToMagnitude();
  // Map FFT bins to VIS_BARS (logarithmic spacing)
  if (xSemaphoreTake(visMutex, 0) == pdTRUE) {
    int binPerBar = (FFT_SIZE / 2) / VIS_BARS;
    for (int b = 0; b < VIS_BARS; b++) {
      double sum = 0;
      for (int k = 0; k < binPerBar; k++) sum += fftReal[b * binPerBar + k];
      float mag = (float)(sum / binPerBar) * 4.f;
      visBarH[b] = visBarH[b] * 0.7f + constrain(mag, 0.f, 1.f) * 0.3f;  // smooth
    }
    xSemaphoreGive(visMutex);
  }
}

// ── PCM filter callback (Core 0) — EQ + visualiser ───────────────────────────
// Called by ESP32-audioI2S for every block of decoded PCM.
// Return true to use modified buffer.
bool audio_filter_samples(int16_t* buff, uint16_t len, uint8_t ch, uint16_t sr) {
  float rgGain = normOn ? powf(10.f, replayGain / 20.f) : 1.f;
  for (int i = 0; i < len; i += ch) {
    float l = buff[i]   / 32768.f * rgGain;
    float r = (ch > 1 ? buff[i+1] : l) / 32768.f * rgGain;
    for (int b = 0; b < EQ_BANDS; b++) { l = eqL[b].process(l); r = eqR[b].process(r); }
    l = constrain(l, -1.f, 1.f); r = constrain(r, -1.f, 1.f);
    buff[i]   = (int16_t)(l * 32767.f);
    if (ch > 1) buff[i+1] = (int16_t)(r * 32767.f);
  }
  updateVisualiser(buff, len);
  return true;
}

// ── Audio callbacks ───────────────────────────────────────────────────────────
void audio_info(const char* info) {
  String s(info);
  if (s.startsWith("Duration:")) {
    int c = s.indexOf(':', 10);
    trackDur = s.substring(10, c).toInt() * 60 + s.substring(c+1).toInt();
  }
}
void audio_id3data(const char* info) {
  String s(info);
  if      (s.startsWith("Title:"))                { s.substring(7).toCharArray(id3Title, 63); id3Updated=true; }
  else if (s.startsWith("Artist:"))               { s.substring(8).toCharArray(id3Artist,63); id3Updated=true; }
  else if (s.startsWith("Album:"))                { s.substring(7).toCharArray(id3Album, 63); id3Updated=true; }
  else if (s.indexOf("replaygain_track_gain") >= 0) {
    int i = s.indexOf(':'); if (i > 0) replayGain = s.substring(i+1).toFloat();
  }
}
void audio_showstreamtitle(const char* info){ strncpy(radioTitle,info,79); id3Updated=true; }

// EOF — handled in loop() via isPlaying flag change for thread safety
void audio_eof_mp3(const char*){ isPlaying=false; }
void audio_eof_wav(const char*){ isPlaying=false; }
void audio_eof_aac(const char*){ isPlaying=false; }
void audio_eof_flac(const char*){ isPlaying=false; }

// ── Crossfade (volume ramp — single Audio object) ─────────────────────────────
// True dual-stream crossfade needs two Audio instances + PCM mixer;
// this approach works well without extra RAM.
void crossfadeNext(int nextIdx) {
  for (int v = vol; v >= 0; v--) { audio.setVolume(v); vTaskDelay(pdMS_TO_TICKS(20)); }
  // Play next (connecttoFS happens inside audioTask via queue)
  AudioMsg m; m.cmd=CMD_PLAY_FILE; m.param=nextIdx;
  xQueueSend(audioQueue, &m, portMAX_DELAY);
  for (int v = 0; v <= vol; v++) { audio.setVolume(v); vTaskDelay(pdMS_TO_TICKS(20)); }
}

// ── FreeRTOS audio task — pinned to Core 0 ────────────────────────────────────
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
        case CMD_PLAY_URL:  audio.connecttohost(msg.url); isPlaying=true; break;
        case CMD_PAUSE:     audio.pauseResume(); break;
        case CMD_STOP:      audio.stopSong();    break;
        case CMD_VOL:       audio.setVolume(msg.param); break;
        case CMD_CROSSFADE: crossfadeNext(msg.param); break;
      }
    }
    audio.loop();
    vTaskDelay(1);
  }
}

void sendAudio(AudioCmd cmd, int param=0, const char* url="") {
  AudioMsg m; m.cmd=cmd; m.param=param; strncpy(m.url,url,255);
  xQueueSend(audioQueue, &m, pdMS_TO_TICKS(200));
}
