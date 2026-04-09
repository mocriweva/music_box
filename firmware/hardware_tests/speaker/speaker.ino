#include <driver/i2s.h>
#include <math.h>

// --- I2S 腳位設定 ---
#define I2S_LRC  25
#define I2S_BCLK 26
#define I2S_DOUT 22

#define SAMPLE_RATE 44100
#define TWO_PI 6.28318530718

// --- 音符頻率表 (C 大調) ---
const float Do = 262.0, Re = 294.0, Mi = 330.0, Fa = 349.0, Sol = 392.0;

void setup() {
  Serial.begin(115200);
  Serial.println("🎶 ESP32 數位音響系統啟動中...");

  // 1. 設定 I2S 底層參數
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  // 2. 啟動 I2S 驅動
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  Serial.println("✅ I2S 初始化完成，準備播放音樂！");
  delay(1000); // 給電容一點時間充飽電
}

// --- 核心播放函數 ---
void playNote(float freq, int duration_ms) {
  // 計算這段時間總共需要幾個音訊樣本
  int total_samples = (SAMPLE_RATE * duration_ms) / 1000;
  float phase = 0.0;
  float phase_increment = TWO_PI * freq / SAMPLE_RATE;
  
  int16_t sample_buffer[256];
  size_t bytes_written;

  // 切塊產生波形並送出 (每次送 256 個樣本)
  for (int i = 0; i < total_samples; i += 256) {
    int chunk_size = min(256, total_samples - i);
    
    for (int j = 0; j < chunk_size; j++) {
      // 振幅設為 4000.0，保護 100uF 電容不被抽乾
      sample_buffer[j] = (int16_t)(sin(phase) * 15000.0); 
      
      phase += phase_increment;
      if (phase >= TWO_PI) phase -= TWO_PI;
    }
    // 透過 I2S 傳輸給 MAX98357A
    i2s_write(I2S_NUM_0, sample_buffer, chunk_size * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  }
  
  // 音符與音符之間加上 20 毫秒的靜音斷點，讓音樂有顆粒感
  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(20); 
}

void loop() {
  Serial.println("▶️ 播放《歡樂頌》...");
  
  // 第一句：Mi Mi Fa Sol, Sol Fa Mi Re
  playNote(Mi, 500); playNote(Mi, 500); playNote(Fa, 500); playNote(Sol, 500);
  playNote(Sol, 500); playNote(Fa, 500); playNote(Mi, 500); playNote(Re, 500);
  
  // 第二句：Do Do Re Mi, Mi(長) Re(短) Re(極長)
  playNote(Do, 500); playNote(Do, 500); playNote(Re, 500); playNote(Mi, 500);
  playNote(Mi, 750); playNote(Re, 250); playNote(Re, 1000);
  
  Serial.println("⏸️ 休息 3 秒...");
  delay(3000); 
}