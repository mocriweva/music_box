#include <WiFi.h>
#include <driver/i2s.h>
#include <math.h>
#include <Stepper.h>

// ==========================================
// 📶 Wi-Fi 熱點設定
// ==========================================
const char* ssid = "Park0421"; 
const char* password = "20070724"; 

// ==========================================
// 🔌 硬體腳位定義
// ==========================================
#define I2S_LRC  25  
#define I2S_BCLK 26  
#define I2S_DOUT 22  

const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 13, 27, 14, 33);

const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; 

// ==========================================
// 🎵 物理音訊引擎
// ==========================================
#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512

volatile bool beepToggle = false; 
volatile bool audioEnabled = false; // 音訊總開關

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
        // 只有在測試開啟，且 beep 狀態為 true 時才有聲音
        float amp = (audioEnabled && beepToggle) ? 10000.0 : 0.0; 
        for (int i = 0; i < NUM_SAMPLES; i++) {
            sampleBuffer[i] = (int16_t)(sin(phase) * amp);
            phase += (TWO_PI * targetFreq) / SAMPLE_RATE;
            if (phase >= TWO_PI) phase -= TWO_PI;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// ==========================================
// 🛠️ 測試輔助與安全防護函數
// ==========================================
void disableStepper() {
    digitalWrite(13, LOW); digitalWrite(27, LOW);
    digitalWrite(14, LOW); digitalWrite(33, LOW);
}

void flushSerial() {
    delay(10);
    while (Serial.available()) Serial.read();
}

void waitForNextTest() {
    Serial.println("\n✅ 測試完成！請在上方輸入框按下 [Enter] 進入下一個測試單元...");
    flushSerial();
    while (!Serial.available()) { delay(50); }
    flushSerial();
    Serial.println("====================================================");
}

// ==========================================
// 🚀 互動式系統初始化與單步測試
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(10000);
    Serial.println("\n\n=== 🛠️ 專案 PCB 裸板安全分段檢測程式 ===");
    
    // 初始化所有引腳並保持斷電 (Safe State)
    myStepper.setSpeed(10);
    disableStepper();
    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);
    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
    
    Serial.println("✅ 所有腳位已初始化為安全斷電狀態。");
    waitForNextTest();

    // --------------------------------------------------
    // [關卡 1] Wi-Fi 連線測試
    // --------------------------------------------------
    Serial.println("🔍 [關卡 1/5] 測試 Wi-Fi 連線...");
    WiFi.mode(WIFI_STA); 
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n✅ Wi-Fi 連線成功！IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n❌ Wi-Fi 連線失敗！(不影響後續硬體測試)");
    }
    waitForNextTest();

    // --------------------------------------------------
    // [關卡 2] 8 通道多工器與感測器測試
    // --------------------------------------------------
    Serial.println("🔍 [關卡 2/5] 測試感測器 (將持續印出數值)");
    Serial.println("👉 [操作] 請用手遮擋感測器確認數值變化。檢查完畢請按 Enter 結束。");
    flushSerial();
    
    while (!Serial.available()) {
        Serial.print("📡 感測值 | ");
        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0)); digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2)); digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            analogRead(pin_SIG); // ⚠️ 幫你補回了空讀機制，避免 MUX 殘影干擾測試
            int val = analogRead(pin_SIG); 
            Serial.printf("S%d:%4d ", i, val);
        }
        Serial.println();
        delay(300);
    }
    waitForNextTest();

    // --------------------------------------------------
    // [關卡 3] I2S 喇叭測試
    // --------------------------------------------------
    Serial.println("🔍 [關卡 3/5] 測試 I2S 音訊引擎");
    Serial.println("👉 [操作] 喇叭應發出「嗶-嗶-嗶」的間斷聲。檢查完畢請按 Enter 結束。");
    flushSerial();
    
    audioEnabled = true;
    while (!Serial.available()) {
        beepToggle = !beepToggle;
        delay(500);
    }
    audioEnabled = false; // 強制靜音
    waitForNextTest();

    
    // --------------------------------------------------
    // [關卡 4] 步進馬達運轉測試 (連續單向旋轉與方向確認)
    // --------------------------------------------------
    Serial.println("🔍 [關卡 4/5] 測試步進馬達 (方向確認)");
    Serial.println("👉 [操作] 馬達將持續以 -10 步運轉。");
    Serial.println("請觀察並記錄馬達軸心是「順時針」還是「逆時針」轉動！");
    Serial.println("檢查完畢請按 Enter 結束。");
    flushSerial();
    
    while (!Serial.available()) {
        myStepper.step(-10);
        // Stepper 函式內部會根據 setSpeed 自動計算等待時間，
        // 所以這裡不需要加 delay，馬達會平順地連續轉動。
    }
    disableStepper(); // 強制斷電防燒
    //waitForNextTest();
    
    //disableStepper(); // 測試完畢再次徹底斷電

    // ==========================================
    Serial.println("\n🎉 PCB 裸板所有單元檢測完畢！");
    Serial.println("硬體安全驗證通過，現在可以放心燒錄主程式了。");
}

void loop() {
    // 測試腳本執行完畢後進入休眠
    delay(10000); 
}