#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <driver/ledc.h>
#include <math.h>
#include <vector>

// ==========================================
// 🌐 區域網路 (Wi-Fi) 設定
// ==========================================
const char* ssid = "Park0421";       
const char* password = "20070724"; 
WebServer server(80);

// ==========================================
// 🔌 硬體腳位定義 (Pin Definitions)
// ==========================================
#define I2S_LRC  25  
#define I2S_BCLK 26  
#define I2S_DOUT 22  

const int motorPins[4] = {13, 27, 14, 33};
const int motorPWMChannels[4] = {4, 5, 6, 7}; 
const int motorPWMFreq = 2500;   
const int motorPWMResolution = 8; 

volatile int motorPWMDuty = 200; 
const int motorStepIntervalMs = 3; 

const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; 
#define START_BTN_PIN 4  

// ==========================================
// 🧠 全自動狀態機與參數
// ==========================================
bool isWebPlaying = false;      
bool isPhysicalPlaying = true;  
bool isMotorRunning = false;     
int playMode = 0; // 0: 實體模擬(15音), 1: 原始無損(4和弦)

volatile bool motorShouldRun = false;
volatile int motorDirection = -1; 

bool current_sensor_state[8] = {false};
bool last_sensor_state[8] = {false}; 
bool isCalibrated = false;

std::vector<int> currentScore;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

int baseline_white[8] = {0}; 
int baseline_black[8] = {0}; 
int jump_up[8] = {0};   
int jump_down[8] = {0}; 

// ==========================================
// 🎵 物理音訊引擎 (DSP LUT 查表法加速)
// ==========================================
#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512
#define LUT_SIZE 1024 // 🌟 波形查表陣列大小

float sineLUT[LUT_SIZE]; // 🌟 預先計算好的正弦波陣列

const float defaultFreqs[15] = {
    261.63, 293.66, 329.63, 349.23, 392.00, 
    440.00, 493.88, 523.25, 587.33, 659.25, 
    698.46, 783.99, 880.00, 987.77, 1046.50
};

// 🌟 將合成器擴充至 8 聲道 (8-Voice Polyphony)
volatile float targetFreq[8] = {0};
volatile float phase[8] = {0}; 
volatile float amp[8] = {0};
volatile float maxAmp[8] = {0};
volatile bool attack[8] = {false};
volatile bool releaseMode[8] = {false};


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

// 🌟 開機時預先算好波形，存入記憶體
void initLUT() {
    for(int i = 0; i < LUT_SIZE; i++) {
        sineLUT[i] = sin((float)i * TWO_PI / LUT_SIZE);
    }
}

// 🌟 獨立核心 1：極速 LUT 查表合成器 (搭載線性插值平滑技術)
// 🌟 獨立核心 1：極速 LUT 查表合成器 (8 軌混音)
void audioTask(void *pvParameters) {
    int16_t sampleBuffer[NUM_SAMPLES];
    size_t bytesWritten;
    const float decayFactor = 0.9999; 
    const float attackStep = 500.0; 

    while (true) {
        bool allQuiet = true;
        for(int v=0; v<8; v++) { // 🌟 改為 8
            if(amp[v] >= 1.0 || attack[v]) { allQuiet = false; break; }
        }

        if (!isWebPlaying && !isPhysicalPlaying && allQuiet) {
            memset(sampleBuffer, 0, sizeof(sampleBuffer));
            i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
            vTaskDelay(10 / portTICK_PERIOD_MS); 
            continue;
        }

        for (int i = 0; i < NUM_SAMPLES; i++) {
            float sample = 0;
            
            for (int v = 0; v < 8; v++) { // 🌟 改為 8
                if (releaseMode[v]) {
                    amp[v] *= 0.85; 
                    if (amp[v] <= 1.0) { amp[v] = 0.0; releaseMode[v] = false; }
                } 
                else if (attack[v]) {
                    amp[v] += attackStep; 
                    if (amp[v] >= maxAmp[v]) { amp[v] = maxAmp[v]; attack[v] = false; }
                } 
                else if (amp[v] > 1.0) {
                    amp[v] *= decayFactor; 
                }

                if (amp[v] > 1.0 || attack[v] || releaseMode[v]) {
                    int index1 = (int)phase[v];
                    int index2 = (index1 + 1) % LUT_SIZE;
                    float fraction = phase[v] - index1;
                    
                    float interpolatedValue = sineLUT[index1] + fraction * (sineLUT[index2] - sineLUT[index1]);
                    sample += interpolatedValue * amp[v];

                    phase[v] += (targetFreq[v] * LUT_SIZE) / SAMPLE_RATE;
                    if (phase[v] >= LUT_SIZE) phase[v] -= LUT_SIZE;
                }
            }
            
            if (sample > 32767.0) sample = 32767.0;
            if (sample < -32768.0) sample = -32768.0;
            
            sampleBuffer[i] = (int16_t)sample;
        }
        i2s_write(I2S_NUM_0, sampleBuffer, sizeof(sampleBuffer), &bytesWritten, portMAX_DELAY);
    }
}

// ==========================================
// ⚙️ 步進馬達與硬體控制 
// ==========================================
static const uint8_t stepSequence[4][4] = {
    {1,0,1,0}, {0,1,1,0}, {0,1,0,1}, {1,0,0,1}
};
static int motorStepIndex = 0;

void initMotorPWM() {
    for (int p = 0; p < 4; p++) {
        ledcSetup(motorPWMChannels[p], motorPWMFreq, motorPWMResolution);
        ledcAttachPin(motorPins[p], motorPWMChannels[p]);
        ledcWrite(motorPWMChannels[p], 0); 
    }
}

void stepOnce(int dir, int duty) {
    motorStepIndex = (motorStepIndex + dir + 4) % 4;
    for (int p = 0; p < 4; p++) {
        ledcWrite(motorPWMChannels[p], stepSequence[motorStepIndex][p] ? duty : 0);
    }
}

void motorCoilsOff() {
    for (int p = 0; p < 4; p++) ledcWrite(motorPWMChannels[p], 0);
}

void motorTask(void *pvParameters) {
    while (true) {
        if (motorShouldRun) {
            stepOnce(motorDirection, motorPWMDuty);
            vTaskDelay(pdMS_TO_TICKS(motorStepIntervalMs));
        } else {
            motorCoilsOff();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

void silenceAll() {
    for(int v=0; v<8; v++) { 
        attack[v] = false; 
        if (amp[v] > 1.0) releaseMode[v] = true; 
    }
}

/// 🌟 新增：動態頻率音量補償 (Frequency-Dependent EQ)
float getVolumeCompensation(float freq) {
    if (freq <= 0.1) return 0.0;
    
    // 將 sqrt 改為 0.8 次方，讓高低頻的落差更明顯
    float comp = pow(440.0 / freq, 0.8); 
    
    // 放寬補償極限
    if (comp > 1.8) comp = 1.8;   // 允許低音稍微再大聲一點
    if (comp < 0.2) comp = 0.2;   // 🌟 允許高音被狠狠壓到只剩 20%
    
    return comp;
}

// 🌟 動態聲道分配引擎 (Voice Allocator)
void playNote(float freq, float targetAmp) {
    if (freq <= 0.1) return;

    // 1. 同音重播 (Re-trigger)：消除連續音波波聲的關鍵！
    // 如果這個頻率已經在響，不重置相位，直接推高音量。
    for (int v = 0; v < 8; v++) {
        if (abs(targetFreq[v] - freq) < 1.0) {
            maxAmp[v] = targetAmp;
            releaseMode[v] = false;
            attack[v] = true;
            return; 
        }
    }

    // 2. 尋找閒置的空聲道
    int voiceToUse = -1;
    for (int v = 0; v < 8; v++) {
        if (amp[v] <= 1.0 && !attack[v] && !releaseMode[v]) {
            voiceToUse = v; break;
        }
    }

    // 3. 尋找已經在快速衰減 (Release) 的聲道
    if (voiceToUse == -1) {
        for (int v = 0; v < 8; v++) {
            if (releaseMode[v]) { voiceToUse = v; break; }
        }
    }

    // 4. 強制搶奪目前最小聲的聲道
    if (voiceToUse == -1) {
        float minAmp = 999999.0;
        for (int v = 0; v < 8; v++) {
            if (amp[v] < minAmp) { minAmp = amp[v]; voiceToUse = v; }
        }
    }

    // 觸發新音符
    targetFreq[voiceToUse] = freq;
    phase[voiceToUse] = 0; // 只有全新的音才重置相位
    maxAmp[voiceToUse] = targetAmp;
    amp[voiceToUse] = 0.0; // 從 0 完美淡入
    releaseMode[voiceToUse] = false;
    attack[voiceToUse] = true;
}

const char* noteNames[15] = {
    "Do (C4)", "Re (D4)", "Mi (E4)", "Fa (F4)", "Sol (G4)", 
    "La (A4)", "Si (B4)", "Do (C5)", "Re (D5)", "Mi (E5)", 
    "Fa (F5)", "Sol (G5)", "La (A5)", "Si (B5)", "Do (C6)"
};

// 🌟 解碼器 1：實體模式 (移除強制靜音，保留天然共振)
void decodePhysicalState(int stateID) {
    if (stateID == 0) return; 
    
    float baseAmp = 18000.0; 
    if (stateID >= 1 && stateID <= 15) {
        float f = defaultFreqs[stateID - 1];
        playNote(f, baseAmp * getVolumeCompensation(f));
        Serial.printf("🎵 掃描到單音: ID %d -> %s\n", stateID, noteNames[stateID - 1]);
    } 
    else if (stateID >= 16 && stateID <= 120) {
        int index = 16;
        for (int i = 0; i < 15; i++) {
            for (int j = i + 1; j < 15; j++) {
                if (index == stateID) {
                    float f1 = defaultFreqs[i];
                    float f2 = defaultFreqs[j];
                    playNote(f1, (baseAmp / 2.0) * getVolumeCompensation(f1));
                    playNote(f2, (baseAmp / 2.0) * getVolumeCompensation(f2));
                    Serial.printf("🎶 掃描到和弦: ID %d -> %s + %s\n", stateID, noteNames[i], noteNames[j]);
                    return;
                }
                index++;
            }
        }
    }
}

// 🌟 解碼器 2：原始無損模式
void decodeOriginalState(int n1, int n2, int n3, int n4) {
    int notes[4] = {n1, n2, n3, n4};
    int activeCount = 0;
    for(int i=0; i<4; i++) { if(notes[i] > 0) activeCount++; }
    if (activeCount == 0) return;

    float allocatedAmp = 20000.0 / activeCount;
    for(int i=0; i<4; i++) {
        if (notes[i] > 0) {
            float f = 440.0 * pow(2.0, (notes[i] - 69) / 12.0);
            playNote(f, allocatedAmp * getVolumeCompensation(f));
        }
    }
}

void calibrateSensors() {
    // 🌟 暫時接管馬達，避免與背景的 motorTask 發生衝突
    bool prevMotorState = motorShouldRun;
    motorShouldRun = false; 
    delay(30); 

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
            analogRead(pin_SIG); // 🌟 恢復你的神來一筆：空讀消除 MUX 殘影！
            temp_sum[i] += analogRead(pin_SIG); 
        }
        delay(5);
    }
    for (int i = 0; i < 8; i++) {
        baseline_white[i] = temp_sum[i] / samples;
        baseline_black[i] = baseline_white[i]; 
    }

    Serial.println("⚙️ [階段 2] 尋找校正黑線... 馬達啟動動態掃描！");
    
    bool blackLineDetected = false; 
    bool blackLinePassed = false;   
    int bufferSteps = 0;
    const int bufferLimit = 40;     
    const int maxSearchSteps = 3000; 
    int step_count = 0;

    // 校正階段手動推進馬達
    while (step_count < maxSearchSteps) {
        stepOnce(motorDirection, motorPWMDuty);
        delay(motorStepIntervalMs);
        step_count++;
        
        bool currentStepHitBlack = false;

        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            
            analogRead(pin_SIG); // 🌟 掃描階段同樣恢復空讀機制！
            int val = analogRead(pin_SIG);
            
            if (val > baseline_black[i]) {
                baseline_black[i] = val; 
            }
            if (val > (baseline_white[i] + 800)) {
                currentStepHitBlack = true;
            }
        }

        if (!blackLineDetected && currentStepHitBlack) {
            blackLineDetected = true;
            Serial.println("👀 發現黑線邊緣！持續推進直到完全跨過黑線...");
        }

        if (blackLineDetected && !currentStepHitBlack) {
            if (!blackLinePassed) {
                blackLinePassed = true;
                Serial.println("⚪ 已完全跨過黑線進入白紙區！開始推進起始緩衝距離...");
            }
        }

        if (blackLinePassed) {
            bufferSteps++;
            if (bufferSteps >= bufferLimit) {
                Serial.println("🛑 已完美停在黑線後方的白紙起始區，停止掃描。");
                break; 
            }
        }
    }

    if (!blackLineDetected) {
        Serial.println("⚠️ 警告：超過最大搜尋範圍仍未發現黑線！請確認紙帶是否有放好。");
    }

    motorCoilsOff(); // 掃描完畢斷電省電

    Serial.println("✅ 校正完成！各通道動態參數如下：");
    for (int i = 0; i < 8; i++) {
        int delta = baseline_black[i] - baseline_white[i];
        if (delta < 200) delta = 500;
        jump_up[i] = delta * 0.6;   
        jump_down[i] = delta * 0.4; 
        
        Serial.printf("通道[%d] 白:%4d | 黑:%4d | 觸發區間: +%d ~ +%d\n", 
                      i, baseline_white[i], baseline_black[i], jump_down[i], jump_up[i]);
        delay(5); 
    }
    Serial.println("\n🎶 進入讀譜待命模式！(等待網頁啟動指令...)");

    // 🌟 保留上一版的補測紀錄與馬達歸還機制
    isCalibrated = true;             
    motorShouldRun = prevMotorState; 
}

void setup() {
    Serial.begin(115200); delay(1000);

    initMotorPWM(); motorCoilsOff(); 
    initLUT(); // 🌟 啟動時立刻把查表陣列準備好

    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT); pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT); pinMode(0, INPUT_PULLUP); pinMode(START_BTN_PIN, INPUT_PULLUP); 

    WiFi.mode(WIFI_STA); WiFi.begin(ssid, password);
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
        DynamicJsonDocument doc(65536); 
        if (deserializeJson(doc, jsonPayload)) { server.send(400, "text/plain", "JSON 失敗"); return; }

        playMode = doc["mode"] | 0; 
        stepDelayMs = doc["delay_ms"] | 250; 
        JsonArray score = doc["score"];
        currentScore.clear();
        for (int value : score) currentScore.push_back(value);

        Serial.printf("\n📡 樂譜載入完畢！目前模式: %s\n", playMode == 0 ? "實體 15音" : "無損 4和弦");
        server.send(200, "text/plain", "樂譜接收成功！");

        isPhysicalPlaying = false; isMotorRunning = false; isWebPlaying = true;
        currentStep = 0; lastStepTime = millis();
    });

    // 1. 恢復純粹的馬達控制路由
    server.on("/motor", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String state = server.arg("state");
        if (state == "start") { 
            isMotorRunning = true; 
            Serial.println("🌐 [網頁遙控] 馬達已啟動！"); 
        } 
        else if (state == "stop") { 
            isMotorRunning = false; 
            if (isPhysicalPlaying) silenceAll(); // 只有實體模式下才順便靜音
            Serial.println("🌐 [網頁遙控] 馬達已停止！"); 
        }
        server.send(200, "text/plain", "OK");
    });

    // 🌟 新增：專屬的網頁音樂強制停止路由
    server.on("/web_stop", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        isWebPlaying = false;      // 強制結束 Wi-Fi 播放狀態
        isPhysicalPlaying = true;  // 系統回歸實體紙帶待命模式
        silenceAll();              // 瞬間執行 8 聲道平滑靜音
        Serial.println("🌐 [網頁遙控] 網頁數位播放已強制停止！"); 
        server.send(200, "text/plain", "OK");
    });

    // 2. 🌟 新增：專屬的網頁音樂強制停止路由
    server.on("/web_stop", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        isWebPlaying = false;      
        isPhysicalPlaying = true;  
        silenceAll();              
        Serial.println("🌐 [網頁遙控] 網頁數位播放已強制停止！"); 
        server.send(200, "text/plain", "OK");
    });

    server.on("/motor/power", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        if (server.hasArg("value")) {
            motorPWMDuty = constrain(server.arg("value").toInt(), 0, 255);
            server.send(200, "text/plain", "OK");
        } else server.send(400, "text/plain", "缺少 value");
    });

    server.begin(); 
    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(motorTask, "MotorTask", 2048, NULL, 1, NULL, 0);

    while (Serial.available()) { Serial.read(); } 
    
    Serial.println("\n=======================================");
    Serial.println("⏸️ 系統已就緒！等待指令中...");
    Serial.println("=======================================");
    Serial.println("👉 [網頁播放] 點擊網頁「無線傳送樂譜」 -> 自動略過校正，直接播音樂");
    Serial.println("👉 [實體模式] 點擊網頁「▶️ 啟動實體馬達」 -> 開始執行紅外線測紙校正");
    Serial.println("👉 [序列埠指令] 在上方輸入大寫 'S' 並發送 -> 開始執行紅外線測紙校正");
    Serial.println("=======================================\n");

    bool skipCalibration = false;
    while (true) {
        server.handleClient(); 
        
        // 條件 1：網頁傳來無線樂譜，跳出迴圈並「略過」紙帶校正
        if (isWebPlaying) { skipCalibration = true; break; } 
        
        // 條件 2：網頁按下啟動實體馬達，跳出迴圈並「執行」紙帶校正
        if (isMotorRunning) { skipCalibration = false; break; } 
        
        // 🌟 條件 3：序列埠手動輸入 S
        if (Serial.available()) {
            char c = Serial.read();
            if (c == 's' || c == 'S') { 
                while(Serial.available()) Serial.read(); 
                skipCalibration = false; 
                break; 
            }
        }
        delay(10); 
    }
    
    if (!skipCalibration) calibrateSensors(); 
} 

void loop() {
    server.handleClient(); 
    motorShouldRun = (isPhysicalPlaying && isMotorRunning);

    if (isWebPlaying) {
        if (millis() - lastStepTime >= (stepDelayMs * 0.85)) silenceAll();

        if (millis() - lastStepTime >= stepDelayMs) {
            lastStepTime = millis(); 
            
            if (playMode == 0) {
                if (currentStep < currentScore.size()) {
                    decodePhysicalState(currentScore[currentStep]);
                    currentStep++;
                } else {
                    isWebPlaying = false; isPhysicalPlaying = true; silenceAll();
                    Serial.println("⏹️ 播放結束。");
                }
            } else {
                if (currentStep * 4 < currentScore.size()) {
                    decodeOriginalState(currentScore[currentStep*4], currentScore[currentStep*4+1], currentScore[currentStep*4+2], currentScore[currentStep*4+3]);
                    currentStep++;
                } else {
                    isWebPlaying = false; isPhysicalPlaying = true; silenceAll();
                    Serial.println("⏹️ 播放結束。");
                }
            }
        }
    }
    else if (isPhysicalPlaying && isMotorRunning) {
        if (!isCalibrated) {
            Serial.println("⚠️ 偵測到尚未校正，自動開始補測紙帶...");
            calibrateSensors();
            return; // 讓系統跳出這回合的 loop，下個瞬間再正式開始讀音符
        }
        int current_binary_val = 0;
        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0)); digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2)); digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            int val = analogRead(pin_SIG); val = analogRead(pin_SIG); 
            if (val > (baseline_white[i] + jump_up[i])) current_sensor_state[i] = true;
            else if (val < (baseline_white[i] + jump_down[i])) current_sensor_state[i] = false;
            if (current_sensor_state[i] && i < 7) current_binary_val += (1 << i); 
        }

        if (current_sensor_state[7] && !last_sensor_state[7]) {
            if (current_binary_val > 0) decodePhysicalState(current_binary_val); 
        } 
        for (int i = 0; i < 8; i++) last_sensor_state[i] = current_sensor_state[i];
    }
}