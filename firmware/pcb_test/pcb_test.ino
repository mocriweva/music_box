#include <WiFi.h>
#include <driver/i2s.h>
#include <math.h>
#include <Stepper.h>

// ==========================================
// 🌐 區域網路 (Wi-Fi) 設定
// ==========================================
const char* ssid = "ED417C";       
const char* password = "4172417@"; 

// ==========================================
// 🔌 硬體腳位定義 (Pin Definitions)
// ==========================================
#define I2S_LRC  25  
#define I2S_BCLK 26  
#define I2S_DOUT 22  

const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 13, 27, 14, 33);

const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; 

// ==========================================
// 🎵 物理音訊引擎 (單純測試音)
// ==========================================
#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512

volatile bool beepToggle = false; // 控制喇叭發聲的開關

void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, 
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = NUM_SAMPLES,
        .use_apll = false,
        .tx_desc_auto_clear = true
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK, .ws_io_num = I2S_LRC, 
        .data_out_num = I2S_DOUT, .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// 獨立核心：單純播放 440Hz 測試音
void audioTask(void *pvParameters) {
    int16_t sampleBuffer[NUM_SAMPLES];
    size_t bytesWritten;
    float phase = 0.0;
    float targetFreq = 440.0; // 測試音 (La)

    while (true) {
        // 如果 beepToggle 為 true，則輸出音量，否則靜音
        float amp = beepToggle ? 10000.0 : 0.0; 
        for (int i = 0; i < NUM_SAMPLES; i++) {
            sampleBuffer[i] = (int16_t)(sin(phase) * amp);
            phase += (TWO_PI * targetFreq) / SAMPLE_RATE;
            if (phase >= TWO_PI) phase -= TWO_PI;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// ==========================================
// 🚀 系統初始化 (Setup)
// ==========================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=== 🛠️ ESP32 系統硬體測試程式 (PCB 檢測專用) ===");

    // 1. 測試馬達初始化
    myStepper.setSpeed(10);
    Serial.println("✅ 馬達引腳初始化完成 (將在 Loop 中持續轉動)");

    // 2. 測試多工器與感測器初始化
    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);
    Serial.println("✅ 感測器引腳初始化完成");

    // 3. 測試 Wi-Fi 功能
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("📶 測試 Wi-Fi 連線中.");
    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 20) { 
        delay(500); 
        Serial.print("."); 
        retryCount++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n✅ Wi-Fi 連線成功！IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n❌ Wi-Fi 連線失敗！請檢查 ESP32 射頻天線或基地台設定。");
    }

    // 4. 測試音訊初始化
    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
    Serial.println("✅ I2S 音訊引擎啟動 (喇叭應開始發出間隔嗶聲)");
    Serial.println("====================================================\n");
}

// ==========================================
// 🔄 主迴圈 (單純執行硬體動作)
// ==========================================
unsigned long lastPrintTime = 0;
unsigned long lastBeepTime = 0;

void loop() {
    // ⚙️ [測試 1] 馬達：無視任何條件，瘋狂且無情地轉動
    myStepper.step(-5); 

    // 🎵 [測試 2] 喇叭：每 500 毫秒切換一次聲音開關 (嗶-靜音-嗶-靜音)
    if (millis() - lastBeepTime > 500) {
        beepToggle = !beepToggle;
        lastBeepTime = millis();
    }

    // 🔍 [測試 3] 感測器：每 300 毫秒讀取並印出一次 7 個通道的 ADC 數值
    if (millis() - lastPrintTime > 300) {
        lastPrintTime = millis();
        Serial.print("📡 原始感測值 | ");
        
        for (int i = 0; i < 8; i++) {
            // 切換多工器通道
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); // 等待電位穩定
            
            int val = analogRead(pin_SIG); // 讀取類比值
            Serial.printf("S%d: %4d  ", i, val);
        }
        Serial.println();
    }
}