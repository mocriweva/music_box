# 🎹 ESP32 Optical & Wi-Fi Dual-Mode Music Box

This is a dual-mode electronic music box project that combines physical punched paper tapes with digital Wi-Fi transmission. Powered by an ESP32 microcontroller, this project uses infrared sensors to read physical paper tapes and features a built-in 8-voice polyphony I2S hardware synthesizer. It perfectly recreates the mechanical romance of a traditional music box while offering the convenience of modern IoT.

## ✨ Features

*   **🔀 Dual-Mode Playback**
    *   **Physical Optical Mode**: Uses a stepper motor to drive the punched paper tape and reads notes in real-time via an 8-channel infrared sensor array and a MUX (CD74HC4067).
    *   **Digital Wi-Fi Mode**: Upload MIDI files via a dedicated web console, translate them into JSON, and transmit them wirelessly over Wi-Fi to the ESP32 for direct playback.
*   **🎵 Advanced Audio Engine (DSP)**
    *   **8-Voice Polyphony**: Supports complex chords and natural sustain stacking, completely breaking free from monophonic limitations.
    *   **Dynamic EQ**: Automatically attenuates high frequencies and boosts low frequencies based on the Fletcher-Munson equal-loudness contours, overcoming the physical limitations of small bare speakers.
    *   **Linear Interpolation & ADSR Release**: Eliminates truncation errors from LUT (Look-Up Table) synthesis and the click/pop noises caused by sudden power cuts, ensuring ultra-pure sound quality.
*   **🤖 Smart Auto-Calibration System**
    *   Built-in "White Paper -> Black Line -> White Paper" tracking algorithm and dual dummy-read filtering mechanism. It automatically captures the infrared trigger thresholds for each channel and includes a "lazy calibration" safety net.
*   **🌐 Web Console**
    *   Supports MIDI parsing and Tone.js dual-mode sound preview (paper tape constraint simulation / lossless playback), providing completely independent remote control for both the motor and the music.

## 🛠️ Hardware

*   **Microcontroller**: ESP32
*   **Audio Output**: I2S DAC Decoder Board + Small Speaker
*   **Sensor Module**: Infrared Reflective Sensor Array (8 Channels)
*   **Multiplexer**: CD74HC4067 (Solves the shortage of ESP32 analog pins)
*   **Drive Mechanism**: Stepper Motor + Stepper Motor Driver Board (For rolling the paper tape)

## 🚀 Getting Started

### 1. Flash Firmware
1. Open the `main.ino` project file in the Arduino IDE.
2. Ensure the necessary Wi-Fi and WebServer libraries are installed and included.
3. Modify `ssid` and `password` in the code to match your local Wi-Fi credentials.
4. Compile and flash to the ESP32.

### 2. Web Console Setup
1. Open the Serial Monitor in the Arduino IDE to get the device's IP address after connecting.
2. Open `index.html` (the web console) in your computer or mobile browser.
3. Enter the obtained IP address into the "ESP32 IP Address" field on the left-center of the page.

### 3. Operation Guide (Sorry for Only Chinese Ver. Provided)
*   **Physical Paper Tape Playback**:
    1. Insert the punched paper tape into the sensor reading slot.
    2. Enter `S` in the Serial Monitor.
    3. The machine will automatically perform infrared calibration and print the value it recognizes.
    4. Click the "▶️ 啟動實體馬達" button on the webpage for beginning rolling and playing once completed.
*   **Wi-Fi Digital Playback**:
    1. Select and upload the `.mid` file you want to play at the top of the webpage.
    2. Click "📶 無線傳送Json樂譜".
    3. Upon successful reception, the ESP32 will automatically skip physical calibration and play the music directly.
*   **Independent Stop System**:
    The webpage provides independent "⏹️ Stop Wireless Playback" and "⏹️ Stop Physical Motor" buttons. Their states are completely decoupled and will not interfere with each other.
*   **Physical structure designed**:
    Create a laser-cut wooden or 3D-printed enclosure to add a resonance box for further low-frequency sound improvement.
## 🔮 Future Work
*   Develop an automatic punch machine to directly convert exported scores from the web console into physical punched paper tapes.
![alt text](assets/tools/image.png)
![alt text](assets/tools/image-1.png)
## 📝 License
[MIT License](LICENSE)