#include <WiFi.h>
#include <driver/i2s.h>
#include <math.h>
#include <Stepper.h>

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

volatile bool beepToggle = false; 

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

void audioTask(void *pvParameters) {
    int16_t sampleBuffer[NUM_SAMPLES];
    size_t bytesWritten;
    float phase = 0.0;
    float targetFreq = 440.0; 

    while (true) {
        float amp = beepToggle ? 10000.0 : 0.0; 
        for (int i = 0; i < NUM_SAMPLES; i++) {
            sampleBuffer[i] = (int16_t)(sin(phase) * amp);
            phase += (TWO_PI * targetFreq) / SAMPLE_RATE;
            if (phase >= TWO_PI) phase -= TWO_PI;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// ⚡ 斷電釋放測試
void disableStepper() {
    digitalWrite(13, LOW);
    digitalWrite(27, LOW);
    digitalWrite(14, LOW);
    digitalWrite(33, LOW);
}

// ==========================================
// 🚀 系統初始化 (Setup)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== 🛠️ 專案 PCB 裸板檢測專用程式 ===");

    // 1. 測試馬達初始化
    myStepper.setSpeed(10);
    disableStepper(); // 初始先斷電
    Serial.println("✅ 馬達引腳初始化完成");

    // 2. 測試多工器與感測器初始化
    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);
    Serial.println("✅ 8通道感測器引腳初始化完成");

    // 3. 測試 Wi-Fi AP 廣播與 MAC 命名
    WiFi.mode(WIFI_AP);
    String mac = WiFi.macAddress(); 
    String macSuffix = mac.substring(9, 11) + mac.substring(12, 14) + mac.substring(15, 17); 
    String uniqueSSID = "MusicBox_" + macSuffix; 
    
    if (WiFi.softAP(uniqueSSID.c_str())) {
        Serial.printf("✅ Wi-Fi 射頻正常！請用手機尋找 SSID: %s\n", uniqueSSID.c_str());
    } else {
        Serial.println("❌ Wi-Fi 啟動失敗！請檢查供電或晶片是否損壞。");
    }

    // 4. 測試音訊初始化
    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
    Serial.println("✅ I2S 音訊引擎啟動 (喇叭應開始發出嗶聲)");
    Serial.println("====================================================\n");
}

// ==========================================
// 🔄 主迴圈 (硬體壓力與動作測試)
// ==========================================
unsigned long lastPrintTime = 0;
unsigned long lastBeepTime = 0;

void loop() {
    unsigned long currentMillis = millis();

    // ⚙️ [測試 1] 馬達：週期性轉動與斷電 (轉 2 秒，停 1 秒)
    if (currentMillis % 3000 < 2000) {
        myStepper.step(-5); 
    } else {
        disableStepper();
    }

    // 🎵 [測試 2] 喇叭：每 500 毫秒切換一次聲音開關 (嗶-靜音)
    if (currentMillis - lastBeepTime > 500) {
        beepToggle = !beepToggle;
        lastBeepTime = currentMillis;
    }

    // 🔍 [測試 3] 感測器：每 300 毫秒讀取並印出一次所有通道
    if (currentMillis - lastPrintTime > 300) {
        lastPrintTime = currentMillis;
        Serial.print("📡 感測值 | ");
        
        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            
            int val = analogRead(pin_SIG); 
            Serial.printf("S%d:%4d ", i, val);
        }
        Serial.println();
    }
}