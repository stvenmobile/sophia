#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Audio.h>
#include "eyes.h"
#include "mouth_patterns.h"   // dual-lip frames + moods + talking bank


// ---------------------------- MODE SELECTION ----------------------------
// Comment OUT the one you don't want; leave the desired one enabled.
// #define MODE_DEBUG        // cycles all moods for 7s each (with label)
#define MODE_NORMAL          // random talk/silence as before 
// ------------------------------------------------------------------------
// LovyanGFX preset in platformio.ini: -D LOVYANGFX_BOARD=ESP32_2432S028

static LGFX gfx;


Audio audio;

// Pick the I2S pins we wired:
static constexpr int I2S_BCLK = 26;   // BCLK  -> MAX98357N BCLK
static constexpr int I2S_LRCK = 22;   // LRCLK -> MAX98357N LRC
static constexpr int I2S_DOUT = 27;   // DATA  -> MAX98357N DIN

void audioBegin() {
  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(16);  // 0..21 (start conservative to avoid pops)
}

// SD (SPI slot)
#include <SD.h>
bool sdInit() {
  return SD.begin(/* cs pin */);
}

// ================== Layout / Tuning ==================
static constexpr float MOUTH_WIDTH_FACTOR    = 0.55f * (2.0f/3.0f); // ~2/3 of earlier width
static constexpr int   MOUTH_BASELINE_OFFSET = 18;                  // baseline from bottom
static constexpr int   MOUTH_EXTRA_DOWN      = 20;                  // extra drop vs eyes

// Talking cadence (fast so it reads as speech)
static constexpr uint32_t TALK_SWAP_MS_BASE = 160;  // ~6.25 Hz
static constexpr uint32_t TALK_SWAP_JITTER  = 40;

// ---------- Speech state (Normal mode) ----------
enum class SpeechState : uint8_t { Silent=0, Talking };
static SpeechState g_speech = SpeechState::Silent;

// Allowed durations (seconds) for talk/silence
static const uint8_t DUR_CHOICES_S[] = {5, 10, 15, 20};

static uint32_t g_stateUntilMs = 0;     // next transition time
static MouthMood g_currMood = MouthMood::Neutral;

static uint32_t g_nextMouthSwapMs = 0;  // cadence while talking
static int      g_currTalkIdx     = 0;

static int  g_mouthY = -1;
static int  g_mouthW = -1;

static inline uint32_t nowMs(){ return millis(); }
static inline int randRange(int lo, int hi){ return lo + (int)(random(0x7fffffff) % (uint32_t)(hi - lo + 1)); }
static inline int pickDurationMs() {
  int ix = randRange(0, (int)(sizeof(DUR_CHOICES_S)/sizeof(DUR_CHOICES_S[0]) - 1));
  return DUR_CHOICES_S[ix] * 1000;
}

// ---------- Mood label (top band) ----------
static void drawMoodLabel(const char* txt){
#ifdef MODE_DEBUG
  gfx.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx.setFont(&fonts::Font2);
  gfx.setTextDatum(textdatum_t::middle_center);
  gfx.fillRect(0, 0, gfx.width(), 20, TFT_BLACK);  // clear old text
  gfx.drawString(txt, gfx.width()/2, 10);
#endif
}

static void clearMoodLabel(){
  gfx.fillRect(0, 0, gfx.width(), 20, TFT_BLACK); // clear label band
}

// ---------- Mouth drawing (dual-lip with fixed 2px anchors; SIGNED offsets) ----------
static void drawMouthFrame(int baseY, int mouthW, const MouthFrame& mf) {
  const int W = gfx.width();
  const int mouthX = (W - mouthW) / 2;

  // Clear a band around the mouth so larger amplitudes don't ghost
  const int clearY0 = baseY - MOUTH_MAX_DY - MOUTH_CLEAR_PAD;
  const int clearY1 = baseY + MOUTH_MAX_DY + MOUTH_CLEAR_PAD;
  gfx.fillRect(mouthX, clearY0, mouthW, clearY1 - clearY0 + 1, TFT_BLACK);

  // 2-pixel anchors at baseline (ALWAYS on centerline)
  gfx.drawFastHLine(mouthX, baseY, ANCHOR_PX, TFT_WHITE);
  gfx.drawFastHLine(mouthX + mouthW - ANCHOR_PX, baseY, ANCHOR_PX, TFT_WHITE);

  // Inner segmented region (symmetric widths via accumulator)
const int innerW = mouthW - 2*ANCHOR_PX;
if (innerW <= 0) return;

float step = innerW / (float)MOUTH_SEGMENTS;
int x = mouthX + ANCHOR_PX;

for (int i = 0; i < MOUTH_SEGMENTS; ++i) {
  // ensure last segment ends exactly at the right anchor
  int nextX = (i == MOUTH_SEGMENTS - 1)
                ? (mouthX + mouthW - ANCHOR_PX)
                : (mouthX + ANCHOR_PX + (int)lroundf(step * (i + 1)));
  int w = nextX - x;
  if (w < 1) w = 1;

  // SIGNED offsets (clamped)
  int uy = mf.upper[i];
  if (uy < -MOUTH_MAX_DY) uy = -MOUTH_MAX_DY;
  if (uy >  MOUTH_MAX_DY) uy =  MOUTH_MAX_DY;

  int ly = mf.lower[i];
  if (ly < -MOUTH_MAX_DY) ly = -MOUTH_MAX_DY;
  if (ly >  MOUTH_MAX_DY) ly =  MOUTH_MAX_DY;

  gfx.drawFastHLine(x, baseY - uy, w, TFT_WHITE);
  gfx.drawFastHLine(x, baseY - ly, w, TFT_WHITE);

  x = nextX;
}
}

static void drawMouthMood(MouthMood mood) {
  const MouthFrame& f = moodToFrame(mood);
  drawMouthFrame(g_mouthY, g_mouthW, f);
}
static void drawMouthTalkIdx(int idx) {
  idx = (idx % NUM_TALK_FRAMES + NUM_TALK_FRAMES) % NUM_TALK_FRAMES;
  drawMouthFrame(g_mouthY, g_mouthW, TALK_FRAMES[idx]);
}


// ---------- Speech transitions (Normal mode) ----------
static void enterSilent(){
  g_speech = SpeechState::Silent;
  g_stateUntilMs = nowMs() + pickDurationMs();

  // pick a mood to hold during silence
  const int pick = randRange(0, 3); // Smile/Frown/Puzzled/Oooh
  g_currMood = (pick==0) ? MouthMood::Smile :
               (pick==1) ? MouthMood::Frown :
               (pick==2) ? MouthMood::Puzzled :
                           MouthMood::Oooh;

  gfx.startWrite();
  clearMoodLabel(); 
  drawMouthMood(g_currMood);
  drawMoodLabel(
    g_currMood==MouthMood::Smile   ? " Smile   "   :
    g_currMood==MouthMood::Frown   ? " Frown   "   :
    g_currMood==MouthMood::Puzzled ? " Puzzled "   :
    g_currMood==MouthMood::Oooh    ? " Oooh    "   :
                                     " Neutral "
  );
  gfx.endWrite();
}

static void enterTalking(){
  g_speech = SpeechState::Talking;
  g_stateUntilMs = nowMs() + pickDurationMs();
  g_currTalkIdx = randRange(0, NUM_TALK_FRAMES-1);
  g_nextMouthSwapMs = nowMs() + TALK_SWAP_MS_BASE + randRange(-(int)TALK_SWAP_JITTER,(int)TALK_SWAP_JITTER);

  gfx.startWrite();
  clearMoodLabel();               // no label while talking
  drawMouthTalkIdx(g_currTalkIdx);
  gfx.endWrite();
}

// ===== Eyes module instance =====
static Eyes::State  EYES;
static Eyes::Layout E_LAYOUT; // defaults (your tuned cx/cy/radii)

// ===== DEBUG MODE state =====
#ifdef MODE_DEBUG
static constexpr uint32_t DEBUG_MOOD_HOLD_MS = 5000;  // show each mood for 5 sec
static const MouthMood DEBUG_MOODS[] = {
  MouthMood::Neutral, MouthMood::Smile, MouthMood::Frown, MouthMood::Puzzled, MouthMood::Oooh
};
static int       dbg_idx = 0;
static uint32_t  dbg_nextSwitch = 0;

static const char* moodName(MouthMood m){
  switch(m){
    case MouthMood::Neutral: return "Neutral";
    case MouthMood::Smile:   return "Smile";
    case MouthMood::Frown:   return "Frown";
    case MouthMood::Puzzled: return "Puzzled";
    case MouthMood::Oooh:    return "Oooh";
    default:                 return "Unknown";
  }
}
#endif

void setup(){
  randomSeed((uint32_t)esp_random() ^ (uint32_t)micros());
  Serial.begin(115200);

  gfx.init();
  gfx.setRotation(1);
  gfx.fillScreen(TFT_BLACK);

  // Initialize eyes (draw rims, pupils, baseline lids)
  Eyes::init(gfx, EYES, E_LAYOUT);

  // Lay out mouth relative to current eye position
  const int H = gfx.height();
  const int defaultMouthY = H - MOUTH_BASELINE_OFFSET;
  const int deltaY = (EYES.oldCy - EYES.L.cy); // positive if eyes moved up; negative if moved down
  g_mouthY = constrain(defaultMouthY - deltaY + MOUTH_EXTRA_DOWN, EYES.L.cy + EYES.L.rWhite + 8, H - 4);
  g_mouthW = (int)roundf(gfx.width() * MOUTH_WIDTH_FACTOR);

#ifdef MODE_DEBUG
  // Start at first mood
  dbg_idx = 0;
  const MouthMood m = DEBUG_MOODS[dbg_idx];
  gfx.startWrite();
  drawMouthMood(m);
  clearMoodLabel(); 
  drawMoodLabel(moodName(m));
  gfx.endWrite();
  dbg_nextSwitch = nowMs() + DEBUG_MOOD_HOLD_MS;
#else
  // Start silent with a mood
  enterSilent();
#endif
}

void loop(){
  // Fixed cadence using FreeRTOS tick
  static TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / Eyes::FPS_DEFAULT);
  vTaskDelayUntil(&last, period);
  const float dt = (float)period / 1000.f;

  // Always update eyes (blink, gaze, lids, pupils)
  Eyes::update(gfx, EYES, dt);

#ifdef MODE_DEBUG
  // Cycle moods every 5s, always show label
  const uint32_t tNow = nowMs();
  if (tNow >= dbg_nextSwitch){
    dbg_idx = (dbg_idx + 1) % (int)(sizeof(DEBUG_MOODS)/sizeof(DEBUG_MOODS[0]));
    const MouthMood m = DEBUG_MOODS[dbg_idx];
    gfx.startWrite();
    drawMouthMood(m);
    clearMoodLabel(); 
    drawMoodLabel(moodName(m));
    gfx.endWrite();
    dbg_nextSwitch = tNow + DEBUG_MOOD_HOLD_MS;
  }
#else
  // ------- Normal mode: random talk/silence -------
  const uint32_t tNow = nowMs();

  // State transition when time is up
  if (tNow >= g_stateUntilMs) {
    if (g_speech == SpeechState::Silent) enterTalking();
    else                                  enterSilent();
  }

  // Talking: swap mouth frames at cadence
  if (g_speech == SpeechState::Talking && tNow >= g_nextMouthSwapMs) {
    int nextIdx = g_currTalkIdx;
    while (nextIdx == g_currTalkIdx) nextIdx = randRange(0, NUM_TALK_FRAMES-1);
    g_currTalkIdx = nextIdx;

    gfx.startWrite();
    drawMouthTalkIdx(g_currTalkIdx);
    gfx.endWrite();

    g_nextMouthSwapMs = tNow + TALK_SWAP_MS_BASE + randRange(-(int)TALK_SWAP_JITTER,(int)TALK_SWAP_JITTER);
  }
#endif
}
