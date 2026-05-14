#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <vector>
#include <Stepper.h>

// ==========================================
// 🌐 區域網路 (Wi-Fi) 設定
// ==========================================
const char* ssid = "ED417C";       
const char* password = "4172417@"; 
WebServer server(80);

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
// 🧠 全自動狀態機與參數
// ==========================================
bool isWebPlaying = false;      
bool isPhysicalPlaying = true;  
bool last_sensor_state[8] = {false}; // 用來記錄上一次的感測器狀態

std::vector<int> currentScore;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

// 感測器參數 (已套用 3400~3600 遲滯區間)
bool current_sensor_state[8] = {false};
const int THRESHOLD_L[8] = {600, 600, 600, 600, 600, 600, 600, 4095};
const int THRESHOLD_H[8] = {1600, 1400, 1900, 1600, 1600, 1600, 1600, 4095};

int sensor_analog_values[8] = {0};
unsigned long lastSerialPrintTime = 0; 
int last_binary_val = 0; // 🌟 記錄上一次的二進位狀態

// ==========================================
// 🎵 物理音訊引擎 (DSP Parameters)
// ==========================================
#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512

const float defaultFreqs[15] = {
    261.63, 293.66, 329.63, 349.23, 392.00, 
    440.00, 493.88, 523.25, 587.33, 659.25, 
    698.46, 783.99, 880.00, 987.77, 1046.50
};

const char* noteNames[7] = {"Do (C4)", "Re (D4)", "Mi (E4)", "Fa (F4)", "Sol (G4)", "La (A4)", "Si (B4)"};

volatile float targetFreq1 = 0.0, targetFreq2 = 0.0;
volatile float phase1 = 0.0, phase2 = 0.0;
volatile float amp1 = 0.0, amp2 = 0.0;

// --- I2S 初始化 ---
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

// --- 獨立核心 1：物理音訊合成器 ---
void audioTask(void *pvParameters) {
    int16_t sampleBuffer[NUM_SAMPLES];
    size_t bytesWritten;

    while (true) {
        if (!isWebPlaying && !isPhysicalPlaying && amp1 < 1.0 && amp2 < 1.0) {
            memset(sampleBuffer, 0, sizeof(sampleBuffer));
            i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
            vTaskDelay(10 / portTICK_PERIOD_MS); 
            continue;
        }

        for (int i = 0; i < NUM_SAMPLES; i++) {
            float sample = 0;
            if (amp1 > 1.0) {
                sample += sin(phase1) * amp1;
                phase1 += (TWO_PI * targetFreq1) / SAMPLE_RATE;
                if (phase1 >= TWO_PI) phase1 -= TWO_PI;
                // 🌟 註解掉衰減，讓聲音持續
                // amp1 *= 0.99985; 
            }
            if (amp2 > 1.0) {
                sample += sin(phase2) * amp2;
                phase2 += (TWO_PI * targetFreq2) / SAMPLE_RATE;
                if (phase2 >= TWO_PI) phase2 -= TWO_PI;
                // 🌟 註解掉衰減，讓聲音持續
                // amp2 *= 0.99985; 
            }
            sampleBuffer[i] = (int16_t)sample;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// --- 給網頁專用的查表法解碼器 ---
void decodeState(int stateID) {
    if (stateID == 0) return; 
    
    if (stateID >= 1 && stateID <= 15) {
        targetFreq1 = defaultFreqs[stateID - 1];
        phase1 = 0; amp1 = 15000.0; 
    } 
    else if (stateID >= 16 && stateID <= 120) {
        int index = 16;
        for (int i = 0; i < 15; i++) {
            for (int j = i + 1; j < 15; j++) {
                if (index == stateID) {
                    targetFreq1 = defaultFreqs[i];
                    targetFreq2 = defaultFreqs[j];
                    phase1 = 0; phase2 = 0;
                    amp1 = 15000.0; amp2 = 15000.0;
                    return;
                }
                index++;
            }
        }
    }
}

// ==========================================
// 🚀 系統初始化 (Setup)
// ==========================================
void setup() {
    Serial.begin(115200);
    myStepper.setSpeed(10);

    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);

    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("\n連線 Wi-Fi 中...");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n✅ 連線成功！IP: %s\n", WiFi.localIP().toString().c_str());

    server.on("/upload", HTTP_OPTIONS, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.sendHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
        server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
        server.send(204);
    });

    server.on("/upload", HTTP_POST, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String jsonPayload = server.arg("plain");
        
        DynamicJsonDocument doc(32768); 
        DeserializationError error = deserializeJson(doc, jsonPayload);
        if (error) { server.send(400, "text/plain", "JSON 失敗"); return; }

        stepDelayMs = doc["delay_ms"] | 250; 
        JsonArray score = doc["score"];
        currentScore.clear();
        for (int value : score) currentScore.push_back(value);

        Serial.printf("📡 [網頁] 樂譜載入完畢！強制切換至數位播放模式。\n");
        server.send(200, "text/plain", "樂譜接收成功！");

        isPhysicalPlaying = false; 
        isWebPlaying = true;
        currentStep = 0;
        lastStepTime = millis();
    });

    server.begin();
    Serial.println("🎸 系統就緒！預設進入【實體二進位測試模式 (長音版)】...");
}

// ==========================================
// 🔄 主迴圈 (全自動狀態切換)
// ==========================================
void loop() {
    server.handleClient(); 

    if (isWebPlaying) {
        myStepper.step(-10); 
        
        if (millis() - lastStepTime >= stepDelayMs) {
            lastStepTime = millis(); 
            if (currentStep < currentScore.size()) {
                decodeState(currentScore[currentStep]);
                currentStep++;
            } else {
                isWebPlaying = false;
                isPhysicalPlaying = true;
                // 網頁播完也強制靜音一次，避免殘留聲音
                amp1 = 0.0; amp2 = 0.0;
                Serial.println("⏹️ [網頁] 播放結束，自動回歸實體測試模式。");
            }
        }
    }
    else if (isPhysicalPlaying) {
        myStepper.step(-20); // 馬達反轉方向
        
        int current_binary_val = 0;

        // 1. 讀取 S0 ~ S6 的感測器狀態
        for (int i = 0; i < 7; i++) {
            digitalWrite(pin_S1, bitRead(i, 0));
            digitalWrite(pin_S0, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            
            delayMicroseconds(5); 
            int val = analogRead(pin_SIG);
            val = analogRead(pin_SIG);
            sensor_analog_values[i] = val; 
            
            if (val > THRESHOLD_H[i]) current_sensor_state[i] = true;
            else if (val < THRESHOLD_H[i]) current_sensor_state[i] = false;

            // ⚠️ 只有 0~5 軌是資料，算進二進位總和。S6 是 Clock 不算入！
            if (current_sensor_state[i] && i < 6) {
                // 你之前要求 LSB 換邊，現在只有 6 軌資料，所以是 (5 - i)
                current_binary_val += (1 << i); 
            }
        }

        // 2. 🌟 時脈正緣/負緣觸發機制 (Clock 獨立在 S6)
        if (current_sensor_state[6] && !last_sensor_state[6]) {
            // 正緣觸發：Clock 剛碰到黑線瞬間，讀取資料並發出長音
            if (current_binary_val > 0) {
                Serial.printf("⏱️ [時脈觸發] 讀取到狀態碼: %d\n", current_binary_val);
                decodeState(current_binary_val);
                if (current_binary_val >= 1 && current_binary_val <= 7) {
                    Serial.printf("🎵 發出: %s\n", noteNames[current_binary_val - 1]);
                }
            }
        } 
        else if (!current_sensor_state[6] && last_sensor_state[6]) {
            // 負緣觸發：Clock 剛離開黑線瞬間，強制靜音
            Serial.println("🔇 Clock 結束，停止發聲。");
            amp1 = 0.0;
            amp2 = 0.0;
        }

        // 3. 更新記憶狀態，為下一次的邊緣比較做準備
        for (int i = 0; i < 7; i++) {
            last_sensor_state[i] = current_sensor_state[i];
        }

        // 4. 除錯列印 (可註解)
        if (millis() - lastSerialPrintTime >= 200) {
            lastSerialPrintTime = millis();
            
            Serial.print("感測值: ");
            for (int i = 0; i < 7; i++) {
                Serial.printf("[%d]:%4d  ", i, sensor_analog_values[i]);
            }
            Serial.println(); 
            
        }
    }
}