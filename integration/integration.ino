#include <driver/i2s.h>
#include <math.h>

// --- 硬體腳位定義 ---
// MUX 控制腳位
const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; // 接收類比訊號 (已接 220 歐姆共享上拉電阻至 3.3V)

// I2S 擴大機腳位
#define I2S_LRC  25
#define I2S_BCLK 26
#define I2S_DOUT 22

// --- 音訊引擎參數 ---
#define SAMPLE_RATE 44100
#define TWO_PI 6.28318530718
#define MAX_POLYPHONY 3  // 保護電容與擴大機：最多 3 音疊加

// 7 個音軌的頻率 (Do, Re, Mi, Fa, Sol, La, Si)
const float freqs[7] = {262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0};

// 音軌狀態與虛擬推桿
float current_amps[7] = {0, 0, 0, 0, 0, 0, 0};
float phases[7] = {0, 0, 0, 0, 0, 0, 0};
bool is_active[7] = {false, false, false, false, false, false, false};

// 🌟 動態白紙背景值 (預設為最大值，讓程式開機後自動向下學習真實數值)
float baselines[7] = {4095.0, 4095.0, 4095.0, 4095.0, 4095.0, 4095.0, 4095.0};

// 監控輸出計時器
unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  
  // 設定 MUX 腳位
  pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
  pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
  pinMode(pin_SIG, INPUT);

  // 設定 I2S 傳統驅動 (相容 v2.0.17)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK, .ws_io_num = I2S_LRC, 
    .data_out_num = I2S_DOUT, .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  Serial.println("✅ 系統啟動！請先將紙帶的「空白處」停留在感測器上方 3 秒鐘進行環境校正！");
}

void loop() {
  // 計時器：每 100 毫秒允許印出一次資料
  bool printNow = false;
  if (millis() - lastPrintTime > 100) {
    printNow = true;
    lastPrintTime = millis();
  }

  // ---------------------------------------------------------
  // 1. 高速掃描 MUX 陣列與動態閾值判定
  // ---------------------------------------------------------
  int active_count = 0;
  
  for (int i = 0; i < 7; i++) {
    // 切換 MUX 通道
    digitalWrite(pin_S0, bitRead(i, 0)); digitalWrite(pin_S1, bitRead(i, 1));
    digitalWrite(pin_S2, bitRead(i, 2)); digitalWrite(pin_S3, bitRead(i, 3));
    delayMicroseconds(50); // 給電壓穩定時間

    int val = analogRead(pin_SIG);
    
    // 💡 自我學習：動態更新白紙背景值
    if (val < baselines[i]) {
      baselines[i] = val; // 遇到更白的紙，立刻記下來
    } else if (!is_active[i]) {
      baselines[i] += 0.5; // 平時緩慢回彈，適應室內光線變化
    }

    // 💡 動態閾值設定：1.5 倍觸發，1.35 倍釋放
    float threshold_on = baselines[i] * 1.5;
    float threshold_off = baselines[i] * 1.35;

    // 觸發判定
    if (val > threshold_on) {
      is_active[i] = true;
    } else if (val < threshold_off) {
      is_active[i] = false;
    }
    
    // 嚴格限制最大發聲數量 (超過直接靜音)
    if (is_active[i]) {
      active_count++;
      if (active_count > MAX_POLYPHONY) {
         is_active[i] = false;
         active_count--;
      }
    }

    // 將數值餵給序列繪圖家
    if (printNow) {
      Serial.print(val);
      if (i < 6) Serial.print(",");
    }
  }

  if (printNow) {
    Serial.println();
  }

  // ---------------------------------------------------------
  // 2. 即時音訊渲染 (運算數學波形)
  // ---------------------------------------------------------
  int16_t sample_buffer[128];
  size_t bytes_written;
  
  float target_amp = 6000.0;    // 單音最大音量
  float amp_step = 10.0;        // 調整這個數字可改變聲音的「殘響長度」(越小越平滑)

  for (int j = 0; j < 128; j++) {
    float mixed_wave = 0.0;

    for (int ch = 0; ch < 7; ch++) {
      if (is_active[ch]) {
        current_amps[ch] += amp_step;
        if (current_amps[ch] > target_amp) current_amps[ch] = target_amp;
      } else {
        current_amps[ch] -= amp_step;
        if (current_amps[ch] < 0) current_amps[ch] = 0;
      }

      if (current_amps[ch] > 0) {
        mixed_wave += sin(phases[ch]) * current_amps[ch];
        phases[ch] += TWO_PI * freqs[ch] / SAMPLE_RATE;
        if (phases[ch] >= TWO_PI) phases[ch] -= TWO_PI;
      }
    }
    
    sample_buffer[j] = (int16_t)mixed_wave;
  }

  // ---------------------------------------------------------
  // 3. I2S 硬體輸出 (發聲)
  // ---------------------------------------------------------
  i2s_write(I2S_NUM_0, sample_buffer, sizeof(sample_buffer), &bytes_written, portMAX_DELAY);
}