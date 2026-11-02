/* ロボっこのクリスマスベル / ATOM Voice (旧 ATOM Echo, ESP32-PICO-D4)
 * 拍手2回: 再生 / 短押し: モード切替 / 0.8秒長押し: 試聴
 * 再生中に押す: 停止。Wi-Fi・追加配線・音声ファイル不要。
 * Arduino ESP32 3.3.8 + Adafruit NeoPixel 1.15.4
 */
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include <Adafruit_NeoPixel.h>
#include <math.h>

// まずここを調整します。数値を変えたら再度書き込んでください。
constexpr float CLAP_MIN_RMS = 700.0f; // 小さくすると小さな拍手にも反応
constexpr float NOISE_RATIO = 4.0f;    // 周囲の音の何倍で反応するか
constexpr float VOLUME = 0.35f;        // 0.0〜0.4程度から調整（最大1.0）
constexpr uint32_t CLAP_MIN_GAP = 180;
constexpr uint32_t CLAP_MAX_GAP = 900;
constexpr bool DEBUG_AUDIO = true;

constexpr int SAMPLE_RATE = 16000;
constexpr int MIC_SAMPLES = 160; // 約10msごとの音量
constexpr int BUTTON_PIN = 39;
constexpr i2s_port_t AUDIO_PORT = I2S_NUM_0;
Adafruit_NeoPixel pixel(1, 27, NEO_GRB + NEO_KHZ800);
i2s_chan_handle_t audio = nullptr;
bool melodyMode = false;
bool playing = false;
float noiseFloor = 100.0f;
uint32_t ignoreUntil = 0;

bool pulse = false, armed = false, waitingSecond = false;
uint32_t pulseStart = 0, quietStart = 0, firstClap = 0;
bool quietTiming = false;
bool buttonRaw = false, buttonStable = false, holdFired = false;
uint32_t buttonChange = 0, buttonDown = 0;

void light(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}
void idleLight() { melodyMode ? light(32, 0, 0) : light(0, 22, 0); }
void fail(const char* what, esp_err_t code) {
  Serial.printf("ERROR: %s (%s)\n", what, esp_err_to_name(code));
  if (audio) { i2s_channel_disable(audio); i2s_del_channel(audio); audio = nullptr; }
  while (true) { light(40, 0, 40); delay(250); light(0, 0, 0); delay(250); }
}
void checked(esp_err_t err, const char* what) { if (err != ESP_OK) fail(what, err); }

// マイクとスピーカーは同じI2S・クロック端子を使うため交互に使います。
void setAudio(bool output) {
  if (audio) {
    checked(i2s_channel_disable(audio), "audio disable");
    checked(i2s_del_channel(audio), "audio delete"); audio = nullptr;
  }
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_PORT, I2S_ROLE_MASTER);
  channel.dma_desc_num = 4;
  channel.dma_frame_num = 256;
  channel.auto_clear = true;
  checked(i2s_new_channel(&channel, output ? &audio : nullptr, output ? nullptr : &audio), "audio create");
  if (output) {
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk = GPIO_NUM_19;
    cfg.gpio_cfg.ws = GPIO_NUM_33;
    cfg.gpio_cfg.dout = GPIO_NUM_22;
    cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
    checked(i2s_channel_init_std_mode(audio, &cfg), "speaker init");
  } else {
    i2s_pdm_rx_config_t cfg = {};
    cfg.clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    // 両スロットを読み、信号がある側の音量を使います。
    cfg.slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.clk = GPIO_NUM_33;
    cfg.gpio_cfg.din = GPIO_NUM_23;
    checked(i2s_channel_init_pdm_rx_mode(audio, &cfg), "mic init");
  }
  checked(i2s_channel_enable(audio), "audio enable");
}

void resetClaps() {
  pulse = false; armed = false; waitingSecond = false; quietTiming = false;
}
float readRms() {
  int16_t samples[MIC_SAMPLES * 2];
  size_t bytes = 0;
  checked(i2s_channel_read(audio, samples, sizeof(samples), &bytes, 100), "mic read");
  if (bytes != sizeof(samples)) fail("mic incomplete buffer", ESP_ERR_TIMEOUT);
  double sum[2] = {}, energy[2] = {};
  for (int i = 0; i < MIC_SAMPLES; ++i) {
    for (int ch = 0; ch < 2; ++ch) {
      double v = samples[i * 2 + ch]; sum[ch] += v; energy[ch] += v * v;
    }
  }
  double variance[2];
  for (int ch = 0; ch < 2; ++ch) {
    double mean = sum[ch] / MIC_SAMPLES;
    variance[ch] = fmax(0.0, energy[ch] / MIC_SAMPLES - mean * mean);
  }
  return sqrt(fmax(variance[0], variance[1]));
}

bool detectDoubleClap(float rms, uint32_t now) {
  float threshold = fmaxf(CLAP_MIN_RMS, noiseFloor * NOISE_RATIO);
  bool loud = rms > threshold;
  bool quiet = rms < threshold * 0.45f;
  if (waitingSecond && now - firstClap > CLAP_MAX_GAP + 150) waitingSecond = false;
  if (!pulse && !loud) noiseFloor = noiseFloor * 0.995f + rms * 0.005f;
  if (pulse) {
    if (quiet) {
      pulse = false;
      uint32_t width = now - pulseStart;
      // 長い声や音楽は拍手として数えません。静かに戻った時点で確定。
      if (width <= 140) {
        Serial.println("Clap");
        if (waitingSecond) {
          uint32_t gap = pulseStart - firstClap;
          if (gap >= CLAP_MIN_GAP && gap <= CLAP_MAX_GAP) {
            resetClaps(); return true;
          }
          if (gap >= CLAP_MIN_GAP) firstClap = pulseStart;
        } else { waitingSecond = true; firstClap = pulseStart; }
      }
    }
  } else if (armed && loud) {
    pulse = true; pulseStart = now; armed = false; quietTiming = false;
  }
  if (!pulse && quiet) {
    if (!quietTiming) { quietStart = now; quietTiming = true; }
    if (now - quietStart >= 70) armed = true;
  } else { quietTiming = false; }
  return false;
}

void sendAudio(const int16_t* data, size_t bytes) {
  const uint8_t* p = (const uint8_t*)data;
  while (bytes) {
    size_t written = 0;
    checked(i2s_channel_write(audio, p, bytes, &written, 200), "speaker write");
    if (!written) fail("speaker timeout", ESP_ERR_TIMEOUT);
    p += written; bytes -= written;
  }
}

// 各音を正弦波と倍音から合成。立ち上がり・終わりを滑らかにします。
bool playBell(float hz, uint32_t durationMs, uint8_t noteIndex) {
  uint32_t count = SAMPLE_RATE * durationMs / 1000;
  int16_t pcm[256 * 2];
  noteIndex % 2 ? light(0, 35, 8) : light(45, 2, 0);
  for (uint32_t start = 0; start < count; start += 256) {
    if (digitalRead(BUTTON_PIN) == LOW) return false;
    uint32_t n = min((uint32_t)256, count - start);
    for (uint32_t j = 0; j < n; ++j) {
      uint32_t k = start + j;
      float t = (float)k / SAMPLE_RATE;
      float attack = fminf(1.0f, t / 0.006f);
      float release = fminf(1.0f, (float)(count - 1 - k) / (SAMPLE_RATE * 0.025f));
      float w = 6.28318530718f * hz * t;
      float wave = sinf(w) * expf(-3.8f * t)
                 + 0.30f * sinf(2.76f * w) * expf(-8.0f * t)
                 + 0.12f * sinf(4.07f * w) * expf(-13.0f * t);
      int16_t sample = (int16_t)(wave * attack * release * 21000.0f * VOLUME);
      pcm[j * 2] = sample; pcm[j * 2 + 1] = sample;
    }
    sendAudio(pcm, n * 2 * sizeof(int16_t));
  }
  return true;
}
bool audioSilence(uint32_t ms) {
  int16_t zeros[256 * 2] = {};
  uint32_t remaining = SAMPLE_RATE * ms / 1000;
  while (remaining) {
    if (digitalRead(BUTTON_PIN) == LOW) return false;
    uint32_t n = min(remaining, (uint32_t)256);
    sendAudio(zeros, n * 2 * sizeof(int16_t)); remaining -= n;
  }
  return true;
}
void waitRelease() {
  while (digitalRead(BUTTON_PIN) == LOW) delay(10);
  delay(35);
  buttonRaw = buttonStable = holdFired = false;
  buttonChange = millis();
}
void perform() {
  playing = true; resetClaps();
  setAudio(true);
  bool ok = true;
  if (melodyMode) {
    // Jingle Bells / James Lord Pierpont (1857): サビの短いメロディー。
    const float notes[] = {659.25,659.25,659.25,659.25,659.25,659.25,
      659.25,783.99,523.25,587.33,659.25,
      698.46,698.46,698.46,698.46,698.46,659.25,659.25,659.25,659.25,
      659.25,587.33,587.33,659.25,587.33,783.99};
    const uint8_t beats[] = {1,1,2,1,1,2,1,1,1,1,4,1,1,1,1,1,1,1,1,1,1,1,1,1,2,2};
    static_assert(sizeof(notes) / sizeof(notes[0]) == sizeof(beats), "Notes/beats mismatch");
    for (size_t i = 0; i < sizeof(beats) && ok; ++i) {
      ok = playBell(notes[i], beats[i] * 240 - 25, i) && audioSilence(25);
    }
  } else {
    const float bells[] = {1046.50f, 1318.51f, 1567.98f};
    for (uint8_t i = 0; i < 3 && ok; ++i)
      ok = playBell(bells[i], 520, i) && audioSilence(60);
  }
  if (ok) audioSilence(100); // DMA内の最後の音を送り出す
  setAudio(false);
  playing = false;
  waitRelease();
  ignoreUntil = millis() + 700; // 余韻とマイク切替の過渡音を捨てる
  idleLight();
}

void handleButton(uint32_t now) {
  bool down = digitalRead(BUTTON_PIN) == LOW;
  if (down != buttonRaw) { buttonRaw = down; buttonChange = now; }
  if (now - buttonChange >= 30 && buttonStable != buttonRaw) {
    buttonStable = buttonRaw;
    if (buttonStable) { buttonDown = now; holdFired = false; resetClaps(); }
    else {
      if (!holdFired) {
        melodyMode = !melodyMode;
        Serial.println(melodyMode ? "Mode: Jingle Bells" : "Mode: Bell");
        idleLight();
      }
      resetClaps(); ignoreUntil = now + 350;
    }
  }
  if (buttonStable && !holdFired && now - buttonDown >= 800) {
    holdFired = true;
    light(25, 25, 25); // 白になったら離すと試聴
    waitRelease(); perform();
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT); // GPIO39は外部（本体内）プルアップを使用
  pixel.begin(); light(15, 15, 0);
  Serial.println("Robot Christmas Bell: keep quiet for 2 seconds");
  setAudio(false);
  for (int i = 0; i < 30; ++i) readRms(); // マイク起動直後を捨てる
  float total = 0;
  for (int i = 0; i < 150; ++i) total += readRms();
  noiseFloor = fmaxf(30.0f, total / 150);
  Serial.printf("Ready. Noise=%.0f. Short press: mode; hold then release: test\n", noiseFloor);
  ignoreUntil = millis() + 200; resetClaps(); idleLight();
}
void loop() {
  float rms = readRms();
  uint32_t now = millis();
  handleButton(now);
  now = millis();
  if (buttonRaw || buttonStable || (int32_t)(now - ignoreUntil) < 0) return;
  if (detectDoubleClap(rms, now)) { perform(); return; }
  if (waitingSecond) light(0, 0, 28); else idleLight();
  static uint32_t lastPrint = 0;
  if (DEBUG_AUDIO && now - lastPrint >= 250) {
    lastPrint = now;
    Serial.printf("rms=%.0f noise=%.0f threshold=%.0f\n", rms, noiseFloor,
                  fmaxf(CLAP_MIN_RMS, noiseFloor * NOISE_RATIO));
  }
}
