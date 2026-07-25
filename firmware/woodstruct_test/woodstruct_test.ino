#include <Stepper.h>

// ==========================================
// 🔌 硬體腳位定義 (Pin Definitions)
// ==========================================
const int stepsPerRevolution = 2048; 
Stepper myStepper(stepsPerRevolution, 13, 27, 14, 33);
#define MOTOR_DIR 1 

const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34; 

// ==========================================
// 🧠 校正用變數 (擷取自原主程式)
// ==========================================
int baseline_white[8] = {0}; 
int baseline_black[8] = {0}; 
int jump_up[8] = {0};   
int jump_down[8] = {0}; 
bool isCalibrated = false;
bool motorShouldRun = false; 

// 斷電防發熱保護
void motorCoilsOff() {
    digitalWrite(13, LOW);
    digitalWrite(27, LOW);
    digitalWrite(14, LOW);
    digitalWrite(33, LOW);
}

// 你的紅外線初始化校正函式
void calibrateSensors() {
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
            analogRead(pin_SIG); 
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
    const int maxSearchSteps = 9000; 
    int step_count = 0;

    while (step_count < maxSearchSteps) {
        myStepper.step(MOTOR_DIR); 
        step_count++;
        
        bool currentStepHitBlack = false;

        for (int i = 0; i < 8; i++) {
            digitalWrite(pin_S0, bitRead(i, 0));
            digitalWrite(pin_S1, bitRead(i, 1));
            digitalWrite(pin_S2, bitRead(i, 2));
            digitalWrite(pin_S3, bitRead(i, 3));
            delayMicroseconds(5); 
            
            analogRead(pin_SIG); 
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

    motorCoilsOff(); 

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
    //Serial.println("\n🏁 初始化測試結束，馬達將進入無限運轉模式...");

    isCalibrated = true;             
    motorShouldRun = prevMotorState; 
}

// ==========================================
// 🚀 主程式
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. 初始化 MUX 與紅外線腳位
    pinMode(pin_S0, OUTPUT); 
    pinMode(pin_S1, OUTPUT); 
    pinMode(pin_S2, OUTPUT); 
    pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);

    // 2. 初始化馬達轉速與狀態
    myStepper.setSpeed(10);
    motorCoilsOff();

    Serial.println("\n\n===================================");
    Serial.println("🛠️ 硬體組裝測試程式 輸入s啟動");
    Serial.println("===================================");

    // 3. 執行紅外線初始化測試
    /*while(1){
        if (Serial.available()) {
            char c = Serial.read();
            
            // 如果收到小寫 s 或大寫 S
            if (c == 's' || c == 'S') { 
                calibrateSensors(); 
            }
        }
        break;
    }
    motorCoilsOff();
    */
}

void loop() {
    // 1. 監聽序列埠指令
    if (Serial.available()) {
        char c = Serial.read();
        
        // 如果收到小寫 s 或大寫 S
        if (c == 's' || c == 'S') { 
            // 把緩衝區裡多餘的換行符號清空
            while(Serial.available()) Serial.read(); 
            
            Serial.println("\n🔄 收到指令 (S)，手動觸發紅外線校正程序...");
            
            // 🌟 核心修改：直接呼叫校正函式
            calibrateSensors(); 
            
            Serial.println("✅ 本次校正測試完畢！(再次輸入 s 可重新測試)");
        }
    }

    // 2. 空迴圈中加入微小延遲，讓出 CPU 資源避免當機
    delay(20); 
}