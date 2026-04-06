#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <vector>

// --- 🌐 區域網路 (Wi-Fi) 設定區 ---
// ⚠️ 請填寫你家裡或手機分享的 Wi-Fi 資訊
const char* ssid = "ED417C";       
const char* password = "4172417@"; 

WebServer server(80);

// --- 🔌 I2S 腳位設定 (MAX98357A) ---
#define I2S_BCLK 26  
#define I2S_LRC  25  
#define I2S_DOUT 22  

#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512

// --- 🎵 播放器與記憶體狀態 ---
std::vector<int> currentScore;
bool isPlaying = false;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

// 15 實體白鍵頻率 (C4 ~ C6)
const float defaultFreqs[15] = {
    261.63, 293.66, 329.63, 349.23, 392.00, 
    440.00, 493.88, 523.25, 587.33, 659.25, 
    698.46, 783.99, 880.00, 987.77, 1046.50
};

// 🌟 物理引擎狀態變數 (跨核心共用需加 volatile)
volatile float targetFreq1 = 0.0;
volatile float targetFreq2 = 0.0;
volatile float phase1 = 0.0;
volatile float phase2 = 0.0;
volatile float amp1 = 0.0;
volatile float amp2 = 0.0;

// --- 1. I2S 硬體初始化 ---
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
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// --- 2. 真實音樂盒物理模擬 (獨立運行於核心 1) ---
void audioTask(void *pvParameters) {
    int16_t sampleBuffer[NUM_SAMPLES];
    size_t bytesWritten;

    while (true) {
        if (!isPlaying && amp1 < 1.0 && amp2 < 1.0) {
            memset(sampleBuffer, 0, sizeof(sampleBuffer));
            i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
            vTaskDelay(10 / portTICK_PERIOD_MS); 
            continue;
        }

        for (int i = 0; i < NUM_SAMPLES; i++) {
            float sample = 0;
            // 只要振幅大於 1，就繼續發聲 (模擬金屬簧片的延音)
            if (amp1 > 1.0) {
                sample += sin(phase1) * amp1;
                phase1 += (TWO_PI * targetFreq1) / SAMPLE_RATE;
                if (phase1 >= TWO_PI) phase1 -= TWO_PI;
                amp1 *= 0.99985; // 🌟 指數衰減包絡線
            }
            if (amp2 > 1.0) {
                sample += sin(phase2) * amp2;
                phase2 += (TWO_PI * targetFreq2) / SAMPLE_RATE;
                if (phase2 >= TWO_PI) phase2 -= TWO_PI;
                amp2 *= 0.99985; 
            }
            sampleBuffer[i] = (int16_t)sample;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// --- 3. 實體打孔網格解碼器 ---
void decodeState(int stateID) {
    if (stateID == 0) return; // 空白網格：不強制消音，讓殘響自然延續
    
    if (stateID >= 1 && stateID <= 15) {
        targetFreq1 = defaultFreqs[stateID - 1];
        phase1 = 0;      
        amp1 = 8000.0;   // 單重撥弦
    } 
    else if (stateID >= 16 && stateID <= 120) {
        int index = 16;
        for (int i = 0; i < 15; i++) {
            for (int j = i + 1; j < 15; j++) {
                if (index == stateID) {
                    targetFreq1 = defaultFreqs[i];
                    targetFreq2 = defaultFreqs[j];
                    phase1 = 0; phase2 = 0;
                    amp1 = 8000.0; // 雙重撥弦疊加
                    amp2 = 8000.0;
                    return;
                }
                index++;
            }
        }
    }
}

// --- 4. 系統初始化 ---
void setup() {
    Serial.begin(115200);
    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);

    // 🌟 連線至區域網路 (STA 模式)
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("\n正在尋找並連線至 Wi-Fi: ");
    Serial.println(ssid);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\n✅ Wi-Fi 連線成功！");
    Serial.print("📡 請將網頁的 ESP32 IP 位址改成: ");
    Serial.println(WiFi.localIP()); 

    // CORS 預檢請求
    server.on("/upload", HTTP_OPTIONS, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
        server.send(204);
    });

    // 接收樂譜資料
    server.on("/upload", HTTP_POST, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String jsonPayload = server.arg("plain");
        Serial.println("\n[網路] 接收到網頁資料包！");

        // 32KB 緩衝區，足以應付極長樂曲
        DynamicJsonDocument doc(32768); 
        DeserializationError error = deserializeJson(doc, jsonPayload);
        
        if (error) {
            Serial.print("[錯誤] JSON 解析失敗: ");
            Serial.println(error.c_str());
            server.send(400, "text/plain", "JSON 失敗");
            return;
        }

        stepDelayMs = doc["delay_ms"] | 250; 
        
        JsonArray score = doc["score"];
        currentScore.clear();
        for (int value : score) {
            currentScore.push_back(value);
        }

        Serial.printf("[成功] 樂譜載入完畢！共 %d 個網格，步進延遲: %d ms\n", currentScore.size(), stepDelayMs);
        server.send(200, "text/plain", "樂譜接收成功！");

        isPlaying = true;
        currentStep = 0;
        lastStepTime = millis();
    });

    server.begin();
}

// --- 5. 主迴圈 (網路監聽與步進控制) ---
void loop() {
    server.handleClient(); 

    if (isPlaying) {
        if (millis() - lastStepTime >= stepDelayMs) {
            lastStepTime = millis(); 

            if (currentStep < currentScore.size()) {
                decodeState(currentScore[currentStep]);
                currentStep++;
            } else {
                isPlaying = false;
                Serial.println("[系統] 樂譜播放結束。");
            }
        }
    }
}