#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> // 需要安裝 ArduinoJson 函式庫

WebServer server(80);

void setup() {
    Serial.begin(115200);
    // 讓 ESP32 自己開一個 Wi-Fi 熱點
    WiFi.softAP("MusicBox-ESP32", "12345678"); 
    IPAddress IP = WiFi.softAPIP();
    Serial.print("ESP32 IP: ");
    Serial.println(IP); // 預設通常是 192.168.4.1

    // 設定接收樂譜的 API 路由
    server.on("/upload", HTTP_POST, []() {
        // 允許跨域請求 (CORS)，讓你的瀏覽器可以順利傳資料
        server.sendHeader("Access-Control-Allow-Origin", "*");
        
        String jsonPayload = server.arg("plain");
        Serial.println("收到網頁傳來的樂譜資料！");

        // 使用 ArduinoJson 解開陣列
        DynamicJsonDocument doc(16384); // 開啟一塊足夠大的記憶體空間
        DeserializationError error = deserializeJson(doc, jsonPayload);
        
        if (error) {
            server.send(400, "text/plain", "JSON 解析失敗");
            return;
        }

        int songLength = doc["song_length"];
        JsonArray score = doc["score"];

        // TODO: 把 score 陣列存入 ESP32 的陣列或 SPIFFS 檔案系統中
        // TODO: 觸發步進馬達與 I2S 開始發聲
        
        server.send(200, "text/plain", "樂譜接收成功");
    });

    server.begin();
}

void loop() {
    server.handleClient(); // 不斷監聽網頁傳來的請求
}