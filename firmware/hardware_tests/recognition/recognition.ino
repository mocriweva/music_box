const int pin_S0 = 16, pin_S1 = 17, pin_S2 = 18, pin_S3 = 19;
const int pin_SIG = 34;

void setup() {
  Serial.begin(115200);
  pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
  pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
  pinMode(pin_SIG, INPUT); 
}

void loop() {
  // 只掃描 0 到 6 號通道 (共 7 顆)
  for (int channel = 0; channel < 16; channel++) {
    digitalWrite(pin_S0, bitRead(channel, 0)); 
    digitalWrite(pin_S1, bitRead(channel, 1));
    digitalWrite(pin_S2, bitRead(channel, 2));
    digitalWrite(pin_S3, bitRead(channel, 3));
    delayMicroseconds(5); 

    analogRead(pin_SIG);
    int analogValue = analogRead(pin_SIG);
    Serial.print(analogValue);
    Serial.print(" ");
    // 把數值轉成簡單的 0 或 1 印出來 (先隨便設個閾值 2000)
    /*if (analogValue > 2000) {
      Serial.print("1 "); // 黑點或懸空
    } else {
      Serial.print("0 "); // 白紙
    }*/
  }
  Serial.println(); // 7 顆印完後換行
  delay(100);       // 稍微延遲方便肉眼看
}