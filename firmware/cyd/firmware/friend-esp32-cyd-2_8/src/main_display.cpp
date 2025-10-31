#include <Arduino.h>
#include <WiFi.h>     // keep this include to avoid ESP8266 headers
#include <Audio.h>

Audio audio;

static constexpr int I2S_BCLK = 26;  // MAX98357N BCLK
static constexpr int I2S_LRCK = 22;  // MAX98357N LRC/WS
static constexpr int I2S_DOUT = 27;  // MAX98357N DIN

void setup() {
  Serial.begin(115200);

  audio.setPinout(I2S_BCLK, I2S_LRCK, I2S_DOUT);
  audio.setVolume(16);                // 0..21

  // 🔊 Speak a short phrase to prove I²S is alive
  audio.connecttospeech("Hello there, this is a test.");   // default English
}

void loop() {
  audio.loop();
}
