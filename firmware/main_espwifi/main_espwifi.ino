#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <vector>
#include <Stepper.h>

// ==========================================
// 🌐 基地台熱點 (AP Mode) 設定
// ==========================================
const char* ap_ssid = "MusicBox-ESP32";   // ⚠️ 你可以自己改熱點名稱
const char* ap_password = "12345678";     // ⚠️ 熱點密碼 (必須至少 8 碼)
WebServer server(80);

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
// 🧠 全自動狀態機與參數
// ==========================================
bool isWebPlaying = false;      
bool isPhysicalPlaying = true;  

std::vector<int> currentScore;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

bool current_sensor_state[7] = {false};
bool last_sensor_state[7] = {false};
const int THRESHOLD_H[7] = {1850, 1850, 1850, 1850, 1850, 1850, 1850};
const int THRESHOLD_L[7] = {1600, 1600, 1600, 1600, 1600, 1600, 1600};

// ==========================================
// 🎵 物理音訊引擎 (DSP)
// ==========================================
#define SAMPLE_RATE 44100
#define NUM_SAMPLES 512

const float defaultFreqs[15] = {
    261.63, 293.66, 329.63, 349.23, 392.00, 
    440.00, 493.88, 523.25, 587.33, 659.25, 
    698.46, 783.99, 880.00, 987.77, 1046.50
};

volatile float targetFreq1 = 0.0, targetFreq2 = 0.0;
volatile float phase1 = 0.0, phase2 = 0.0;
volatile float amp1 = 0.0, amp2 = 0.0;

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
                amp1 *= 0.99985; 
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

    // 🌟 改為 AP 熱點模式
    Serial.println("\n正在啟動 ESP32 專屬 Wi-Fi 熱點...");
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress IP = WiFi.softAPIP();
    
    Serial.println("✅ 熱點啟動成功！");
    Serial.print("📶 Wi-Fi 名稱: "); Serial.println(ap_ssid);
    Serial.print("🔑 Wi-Fi 密碼: "); Serial.println(ap_password);
    Serial.print("📡 請將網頁的 ESP32 IP 位址改成: "); Serial.println(IP); // 通常是 192.168.4.1

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
    Serial.println("🎸 系統就緒！預設進入【實體感測模式】...");
}

// ==========================================
// 🔄 主迴圈
// ==========================================
void loop() {
    server.handleClient(); 

    if (isWebPlaying) {
        myStepper.step(10); 
        
        if (millis() - lastStepTime >= stepDelayMs) {
            lastStepTime = millis(); 
            if (currentStep < currentScore.size()) {
                decodeState(currentScore[currentStep]);
                currentStep++;
            } else {
                isWebPlaying = false;
                isPhysicalPlaying = true;
                Serial.println("⏹️ [網頁] 播放結束，自動回歸【實體感測模式】。");
            }
        }
    }
    else if (isPhysicalPlaying) {
        myStepper.step(10); 
        
        int stateID_from_sensors = 0;
        bool any_rising_edge = false;

        for (int i = 0; i < 7; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(50); 
            
            int val = analogRead(pin_SIG);
            
            if (val > THRESHOLD_H[i]) current_sensor_state[i] = true;
            else if (val < THRESHOLD_L[i]) current_sensor_state[i] = false;

            if (current_sensor_state[i]) {
                stateID_from_sensors |= (1 << i);
            }

            if (current_sensor_state[i] && !last_sensor_state[i]) {
                any_rising_edge = true;
            }
            last_sensor_state[i] = current_sensor_state[i];
        }

        if (any_rising_edge && stateID_from_sensors > 0) {
            decodeState(stateID_from_sensors);
        }
    }
}