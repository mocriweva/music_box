#include <Stepper.h>

// --- 馬達參數 ---
const int stepsPerRevolution = 2048;
Stepper myStepper(stepsPerRevolution, 13, 27, 14, 33); 

// --- 按鈕與狀態機參數 ---
const int BUTTON_PIN = 4;
bool isMotorRunning = false;     
int buttonState;                 // 記憶「過濾雜訊後」的穩定狀態
int lastButtonState = HIGH;      // 記憶「上一個瞬間」的原始讀值

unsigned long lastDebounceTime = 0; 
const unsigned long debounceDelay = 50; 

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  myStepper.setSpeed(10); 
  
  // 初始化 buttonState 為高電位
  buttonState = digitalRead(BUTTON_PIN);
  
  Serial.println("✅ 邏輯修復版啟動！請按按鈕。");
}

void loop() {
  // 1. 讀取當下原始電位
  int reading = digitalRead(BUTTON_PIN);

  // 2. 如果狀態有變動 (人手按下或雜訊)，重置計時器
  if (reading != lastButtonState) {
    lastDebounceTime = millis(); 
  }

  // 3. 如果穩定時間超過 50 毫秒，代表這不是雜訊
  if ((millis() - lastDebounceTime) > debounceDelay) {
    
    // 如果這個穩定下來的狀態，跟我們之前記錄的「穩定態」不一樣
    if (reading != buttonState) {
      buttonState = reading; // 更新穩定態
      
      // 只有當新的穩定態是 LOW (代表按鈕被按下了)，才執行切換
      if (buttonState == LOW) {
        isMotorRunning = !isMotorRunning; 
        
        if (isMotorRunning) {
          Serial.println("▶️ 啟動音樂盒！");
        } else {
          Serial.println("⏸️ 暫停音樂盒！");
        }
      }
    }
  }
  
  // 儲存原始讀值，給下一個瞬間比較用
  lastButtonState = reading;

  // ---------------------------------------------------------
  // 4. 馬達執行邏輯 (時間切片)
  // ---------------------------------------------------------
  if (isMotorRunning) {
    // 每次走 10 步，走完立刻回去檢查按鈕，保持系統高靈敏度
    myStepper.step(10); 
  }
}