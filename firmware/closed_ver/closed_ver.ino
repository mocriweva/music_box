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
const char* ssid = "林冰飯";       
const char* password = "shrimpy724"; 
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
#define START_BTN_PIN 4  

// ==========================================
// 🧠 全自動狀態機與參數
// ==========================================
bool isWebPlaying = false;      
bool isPhysicalPlaying = true;  
bool isMotorRunning = false;     

bool current_sensor_state[8] = {false};
bool last_sensor_state[8] = {false}; 

std::vector<int> currentScore;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

int sensor_analog_values[8] = {0};
unsigned long lastSerialPrintTime = 0; 

int baseline_white[8] = {0}; 
int baseline_black[8] = {0}; 
int jump_up[8] = {0};   
int jump_down[8] = {0}; 

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
    const float decayFactor = 0.9999; 

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
                amp1 *= decayFactor; 
            }
            if (amp2 > 1.0) {
                sample += sin(phase2) * amp2;
                phase2 += (TWO_PI * targetFreq2) / SAMPLE_RATE;
                if (phase2 >= TWO_PI) phase2 -= TWO_PI;
                amp2 *= decayFactor; 
            }
            sampleBuffer[i] = (int16_t)sample;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// --- 查表法解碼器 ---
void decodeState(int stateID) {
    if (stateID == 0) {
        amp1 = 0.0; amp2 = 0.0; 
        return; 
    }
    
    if (stateID >= 1 && stateID <= 15) {
        targetFreq1 = defaultFreqs[stateID - 1];
        amp1 = 15000.0; 
    } 
    else if (stateID >= 16 && stateID <= 120) {
        int index = 16;
        for (int i = 0; i < 15; i++) {
            for (int j = i + 1; j < 15; j++) {
                if (index == stateID) {
                    targetFreq1 = defaultFreqs[i];
                    targetFreq2 = defaultFreqs[j];
                    amp1 = 15000.0; amp2 = 15000.0;
                    return;
                }
                index++;
            }
        }
    }
}

// ==========================================
// 🔍 開機白紙與黑線全自動雙點校正
// ==========================================
void calibrateSensors() {
    Serial.println("\n⚙️ [階段 1] 測量白紙基準值...");
    long temp_sum[8] = {0};
    int samples = 50; 

    for (int s = 0; s < samples; s++) {
        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            analogRead(pin_SIG); 
            temp_sum[i] += analogRead(pin_SIG); 
        }
        delay(5);
    }
    for (int i = 0; i < 8; i++) baseline_white[i] = temp_sum[i] / samples;

    Serial.println("⚙️ [階段 2] 尋找校正黑線... 馬達啟動掃描！");
    for (int step_count = 0; step_count < 400; step_count++) {
        myStepper.step(-10); 
        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            
            int val = analogRead(pin_SIG);
            if (val > baseline_black[i]) {
                baseline_black[i] = val; 
            }
        }
    }

    Serial.println("✅ 校正完成！各通道動態參數如下：");
    for (int i = 0; i < 8; i++) {
        int delta = baseline_black[i] - baseline_white[i];
        if (delta < 200) delta = 500;
        jump_up[i] = delta * 0.6;   
        jump_down[i] = delta * 0.4; 
        
        // 🌟 把消失的除錯列印加回來了！
        Serial.printf("通道[%d] 白:%4d | 黑:%4d | 觸發區間: +%d ~ +%d\n", 
                      i, baseline_white[i], baseline_black[i], jump_down[i], jump_up[i]);
    }
    Serial.println("\n🎶 進入讀譜待命模式！(等待網頁啟動指令...)");
}

// ==========================================
// 🚀 系統初始化 (Setup)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    myStepper.setSpeed(10);

    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);
    pinMode(0, INPUT_PULLUP);             
    pinMode(START_BTN_PIN, INPUT_PULLUP); 

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
        if (deserializeJson(doc, jsonPayload)) { server.send(400, "text/plain", "JSON 失敗"); return; }

        stepDelayMs = doc["delay_ms"] | 250; 
        JsonArray score = doc["score"];
        currentScore.clear();
        for (int value : score) currentScore.push_back(value);

        Serial.printf("\n📡 [網頁] 樂譜載入完畢！切換至數位模式。\n");
        server.send(200, "text/plain", "樂譜接收成功！");

        isPhysicalPlaying = false; 
        isMotorRunning = false; 
        isWebPlaying = true;
        currentStep = 0;
        lastStepTime = millis();
    });

    server.on("/motor", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String state = server.arg("state");
        if (state == "start") {
            isMotorRunning = true;
            Serial.println("🌐 [網頁遙控] 馬達已啟動！");
        } else if (state == "stop") {
            isMotorRunning = false;
            amp1 = 0.0; amp2 = 0.0; 
            Serial.println("🌐 [網頁遙控] 馬達已停止！");
        }
        server.send(200, "text/plain", "OK");
    });

    server.begin(); 

    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);

    while (Serial.available()) { Serial.read(); }

    Serial.println("\n⏸️ 系統已就緒，請選擇運作模式：");
    Serial.println("👉 實機模式：放入紙帶後，按 BOOT 鍵或【啟動按鈕】進行校正。");
    Serial.println("👉 開發模式：於此視窗輸入字母 's' 並按 Enter 以手動進入實體校正。");
    Serial.println("👉 數位模式：直接用網頁控制台發送樂譜 (將自動略過實機校正)。");

    bool skipCalibration = false;
    while (true) {
        server.handleClient(); 

        if (isWebPlaying) {
            skipCalibration = true; 
            break;
        }

        if (Serial.available()) {
            char c = Serial.read();
            if (c == 's' || c == 'S') {
                while(Serial.available()) Serial.read(); 
                break; 
            }
        }
        
        // 🌟 強化的硬體脈衝過濾器 (Debounce 延長為 200ms)
        /*if (digitalRead(0) == LOW || digitalRead(START_BTN_PIN) == LOW) {
            delay(200); // 延長判定時間，濾掉 Serial Monitor 打開時造成的 DTR/RTS 突波
            if (digitalRead(0) == LOW || digitalRead(START_BTN_PIN) == LOW) { 
                while(digitalRead(0) == LOW || digitalRead(START_BTN_PIN) == LOW); 
                break; 
            }
        }*/
        delay(10); 
    }

    if (!skipCalibration) {
        calibrateSensors(); 
    }
}

// ==========================================
// 🔄 主迴圈
// ==========================================
void loop() {
    server.handleClient(); 

    if (isWebPlaying) {
        myStepper.step(-10); 
        
        if (millis() - lastStepTime >= (stepDelayMs * 0.85)) {
            amp1 = 0.0;
            amp2 = 0.0;
        }

        if (millis() - lastStepTime >= stepDelayMs) {
            lastStepTime = millis(); 
            if (currentStep < currentScore.size()) {
                decodeState(currentScore[currentStep]);
                currentStep++;
            } else {
                isWebPlaying = false;
                isPhysicalPlaying = true;
                amp1 = 0.0; amp2 = 0.0;
                Serial.println("⏹️ [網頁] 播放結束，自動回歸實體測試待命模式。");
            }
        }
    }
    else if (isPhysicalPlaying) {
        if (isMotorRunning) {
            myStepper.step(-10); 
            int current_binary_val = 0;

            for (int i = 0; i < 8; i++) {
                digitalWrite(pin_S0, bitRead(i, 0));
                digitalWrite(pin_S1, bitRead(i, 1));
                digitalWrite(pin_S2, bitRead(i, 2));
                digitalWrite(pin_S3, bitRead(i, 3));
                delayMicroseconds(5); 
                
                int val = analogRead(pin_SIG);
                val = analogRead(pin_SIG); 
                
                if (val > (baseline_white[i] + jump_up[i])) {
                    current_sensor_state[i] = true;
                } 
                else if (val < (baseline_white[i] + jump_down[i])) {
                    current_sensor_state[i] = false;
                }

                if (current_sensor_state[i] && i < 7) {
                    current_binary_val += (1 << i); 
                }
            }

            if (current_sensor_state[7] && !last_sensor_state[7]) {
                if (current_binary_val > 0) {
                    Serial.printf("⏱️ [時脈觸發] 讀取到狀態碼: %d\n", current_binary_val);
                    decodeState(current_binary_val);
                }
            } 
            else if (!current_sensor_state[7] && last_sensor_state[7]) {
                amp1 = 0.0;
                amp2 = 0.0;
            }

            for (int i = 0; i < 8; i++) {
                last_sensor_state[i] = current_sensor_state[i];
            }
        } 
    }
}