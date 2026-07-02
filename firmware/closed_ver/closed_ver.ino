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
const char* ssid = "ED417C";       
const char* password = "4172417@"; 
WebServer server(80);

// ==========================================
// 🔌 硬體腳位定義 (Pin Definitions)
// ==========================================
#define I2S_LRC  25  
#define I2S_BCLK 26  
#define I2S_DOUT 22  

// --- 步進馬達腳位 (原 Stepper(steps, 13, 27, 14, 33)) ---
const int motorPins[4] = {13, 27, 14, 33};

// --- LEDC PWM 通道設定 ---
// 4 個線圈腳位各自使用一個 LEDC 通道
const int motorPWMChannels[4] = {4, 5, 6, 7}; // 0~3 保留給其他用途，避免與既有 PWM 衝突
const int motorPWMFreq = 2500;   // 2 kHz 斬波頻率，可依馬達實測調整 (1k~5k 都可)
const int motorPWMResolution = 8; // 8-bit -> duty 範圍 0~255

// ⭐ 功率控制參數：數值越小越省電/扭力越小，數值越大扭力越大/越耗電
// 建議先從 255 (全功率) 開始測試，逐步下調找到「還能穩定轉動」的最小值
volatile int motorPWMDuty = 200; // 預設約 55% 功率，請依實測調整
const int motorStepIntervalMs = 3; // 每步間隔 (ms)，數值越小轉速越快

const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; 
#define START_BTN_PIN 4  

// ==========================================
// 🧠 全自動狀態機與參數
// ==========================================
bool isWebPlaying = false;      
bool isPhysicalPlaying = true;  
bool isMotorRunning = false;     

// ⭐ 給 motorTask 讀取的「馬達是否該轉」旗標 (由 loop() 統一更新)
volatile bool motorShouldRun = false;
volatile int motorDirection = -1; // 對應原本的 step(-10)，固定反向

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

// ==========================================
// ⚙️ 步進馬達 PWM 斬波驅動 (取代 Stepper.h)
// ==========================================

// 8 步半步序列 (半步能讓運轉更平順、每步扭力波動較小)
// 欄位對應 motorPins[0..3] = {13, 27, 14, 33}
static const uint8_t stepSequence[4][4] = {
    {1,0,1,0},
    {0,1,1,0},
    {0,1,0,1},
    {1,0,0,1}
};

static int motorStepIndex = 0;

void initMotorPWM() {
    for (int p = 0; p < 4; p++) {
        ledcSetup(motorPWMChannels[p], motorPWMFreq, motorPWMResolution);
        ledcAttachPin(motorPins[p], motorPWMChannels[p]);
        ledcWrite(motorPWMChannels[p], 0); // 開機先斷電，避免過熱
    }
}

// 走一步，duty 決定這一步線圈拿到的平均功率 (0~255)
void stepOnce(int dir, int duty) {
    motorStepIndex = (motorStepIndex + dir + 4) % 4;
    for (int p = 0; p < 4; p++) {
        ledcWrite(motorPWMChannels[p], stepSequence[motorStepIndex][p] ? duty : 0);
    }
}

// 全部斷電：省電、也避免線圈長時間通電發熱
void motorCoilsOff() {
    for (int p = 0; p < 4; p++) ledcWrite(motorPWMChannels[p], 0);
}

// --- 獨立 Task：背景低功率驅動馬達，不再阻塞 loop() ---
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
    // 校正階段直接用 stepOnce() 走 400 步，功率用 motorPWMDuty (與正常運轉一致)
    for (int step_count = 0; step_count < 1200; step_count++) {
        stepOnce(motorDirection, motorPWMDuty);
        delay(motorStepIntervalMs);
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
    motorCoilsOff();

    Serial.println("✅ 校正完成！各通道動態參數如下：");
    for (int i = 0; i < 8; i++) {
        int delta = baseline_black[i] - baseline_white[i];
        if (delta < 200) delta = 500;
        jump_up[i] = delta * 0.6;   
        jump_down[i] = delta * 0.4; 
        
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

    initMotorPWM(); // ⭐ 取代 myStepper.setSpeed(10)

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

    // ⭐ 新增：即時調整馬達 PWM 功率 /motor/power?value=0-255
    server.on("/motor/power", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        if (server.hasArg("value")) {
            int v = server.arg("value").toInt();
            v = constrain(v, 0, 255);
            motorPWMDuty = v;
            Serial.printf("🌐 [網頁遙控] 馬達功率調整為 %d/255\n", v);
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "缺少 value 參數");
        }
    });

    server.begin(); 

    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);
    // ⭐ 馬達 Task 放 core 0，避免跟音訊 Task (core 1) 搶資源
    xTaskCreatePinnedToCore(motorTask, "MotorTask", 2048, NULL, 1, NULL, 0);

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

    // ⭐ 統一在這裡把「該不該轉」的邏輯，同步給 motorTask
    motorShouldRun = isWebPlaying || (isPhysicalPlaying && isMotorRunning);

    if (isWebPlaying) {
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

            Serial.print("感測值: ");
                for (int i = 0; i < 8; i++) {
                    Serial.printf("[%d]:%4d  ", i, current_sensor_state[i]);
                }
                Serial.println(); 
        } 
    }
}
