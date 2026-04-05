#include <driver/i2s.h>
#include <math.h>

// --- 硬體腳位定義 ---
const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; // 接收類比訊號 (接 220 歐姆共享上拉電阻)

// I2S 擴大機腳位
#define I2S_LRC  25
#define I2S_BCLK 26
#define I2S_DOUT 22

// --- 音訊引擎參數 ---
#define SAMPLE_RATE 44100
#define TWO_PI 6.28318530718
#define MAX_POLYPHONY 3  

// 7 個音軌的頻率 (Do, Re, Mi, Fa, Sol, La, Si)
const float freqs[7] = {262.0, 294.0, 330.0, 349.0, 392.0, 440.0, 494.0};

float current_amps[7] = {0, 0, 0, 0, 0, 0, 0};
float phases[7] = {0, 0, 0, 0, 0, 0, 0};
bool is_active[7] = {false, false, false, false, false, false, false};

const int THRESHOLD_H[7] = {1850, 1850, 1850, 1850, 1850, 1850, 1850}; // 突破此線才發聲
const int THRESHOLD_L[7] = {1600, 1600, 1600, 1600, 1600, 1600, 1600}; // 跌破此線才靜音

unsigned long lastPrintTime = 0;

void setup() {
  Serial.begin(115200);
  
  pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
  pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
  pinMode(pin_SIG, INPUT);

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
  
  Serial.println("✅ 單一閾值實驗版啟動！請準備測試邊緣抖動！");
}

void loop() {
  bool printNow = false;
  if (millis() - lastPrintTime > 100) {
    printNow = true;
    lastPrintTime = millis();
  }

  // ---------------------------------------------------------
  // 1. 高速掃描 MUX 陣列 (無遲滯區間版)
  // ---------------------------------------------------------
  int active_count = 0;
  
  //int active_count = 0;
  
  for (int i = 0; i < 7; i++) {
    digitalWrite(pin_S0, bitRead(i, 0)); digitalWrite(pin_S1, bitRead(i, 1));
    digitalWrite(pin_S2, bitRead(i, 2)); digitalWrite(pin_S3, bitRead(i, 3));
    delayMicroseconds(50); 
    
    analogRead(pin_SIG);
    int val = analogRead(pin_SIG);
    
    // 💡 遲滯區間判定 (Debouncing 核心邏輯)
    if (val > THRESHOLD_H[i]) {
      // 突破高標，確定要發聲
      is_active[i] = true;
    } else if (val < THRESHOLD_L[i]) {
      // 跌破低標，確定要靜音
      is_active[i] = false;
    }
    // ⚠️ 如果 val 介於 L 和 H 之間 (例如 1780)，程式不會進任何 if，
    // is_active[i] 就會自然保持著上一輪迴圈的狀態，達成防震盪效果！
    
    // 限制最大發聲數量
    if (is_active[i]) {
      active_count++;
    }

    if (printNow) {
      Serial.print(val);
      if (i < 6) Serial.print(",");
    }
  }

  if (active_count > MAX_POLYPHONY) {
    for (int i = 0 ; i < 7 ;i++) {
      is_active[i] = false;
    }
  }

  if (printNow) Serial.println();

  // ---------------------------------------------------------
  // 2. 即時音訊渲染
  // ---------------------------------------------------------
  int16_t sample_buffer[128];
  size_t bytes_written;
  
  float target_amp = 12000.0;    
  float amp_step = 10.0;        

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
  // 3. I2S 硬體輸出
  // ---------------------------------------------------------
  i2s_write(I2S_NUM_0, sample_buffer, sizeof(sample_buffer), &bytes_written, portMAX_DELAY);
}