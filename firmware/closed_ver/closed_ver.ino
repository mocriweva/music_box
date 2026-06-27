#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <math.h>
#include <vector>
#include <Stepper.h>
#include <DNSServer.h> // 🌟 新增 DNS 函式庫

// ==========================================
// 🌐 區域網路 (Wi-Fi AP) 設定
// ==========================================
WebServer server(80);
const byte DNS_PORT = 53;
DNSServer dnsServer;

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
bool isMotorRunning = false;    
bool isCalibrated = false;       
bool startCalibrationFlag = false; 

bool current_sensor_state[8] = {false};
bool last_sensor_state[8] = {false}; 

std::vector<int> currentScore;
int currentStep = 0;
unsigned long lastStepTime = 0;
int stepDelayMs = 250; 

int sensor_analog_values[8] = {0};
unsigned long lastSerialPrintTime = 0; 
int last_binary_val = 0; 

// --- 全自動校正與動態比例門檻參數 ---
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

volatile float targetFreq1 = 0.0, targetFreq2 = 0.0;
volatile float phase1 = 0.0, phase2 = 0.0;
volatile float amp1 = 0.0, amp2 = 0.0;

// ==========================================
// 🌟 網頁控制台 HTML 字串 (PROGMEM)
// ==========================================
const char index_html[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>8-Bit 電子音樂盒控制台</title>
    <script src="https://unpkg.com/@tonejs/midi"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/tone/14.8.49/Tone.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js"></script>
    <style>
        body { font-family: 'Segoe UI', sans-serif; background-color: #f4f4f9; padding: 20px; }
        .container { max-width: 1000px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); }
        h1 { color: #333; margin-bottom: 5px; }
        .subtitle { color: #666; font-size: 0.9em; margin-bottom: 20px; }
        .controls, .export-controls, .progress-area { margin-top: 15px; display: flex; gap: 15px; align-items: center; background: #f1f1f1; padding: 15px; border-radius: 5px; flex-wrap: wrap;}
        button { padding: 10px 20px; font-size: 16px; cursor: pointer; border: none; border-radius: 5px; background-color: #28a745; color: white; font-weight: bold; transition: 0.2s; }
        button:disabled { background-color: #ccc; cursor: not-allowed; }
        #stop-btn { background-color: #dc3545; }
        #pdf-btn { background-color: #007bff; }
        #wifi-btn { background-color: #17a2b8; }
        #wifi-btn:hover:not(:disabled) { background-color: #138496; }
        #motor-start-btn { background-color: #ff9800; }
        #motor-stop-btn { background-color: #dc3545; }
        #calibrate-btn { background-color: #9c27b0; }
        #status { font-weight: bold; color: #0056b3; margin-top: 10px; }
        #detected-range { font-weight: bold; color: #d63384; margin-top: 5px; font-size: 1.1em; }
        .progress-area { background: #fff3cd; border-left: 5px solid #ffc107; width: 100%; box-sizing: border-box; display: none; flex-direction: column; align-items: flex-start; gap: 5px;}
        progress { width: 100%; height: 20px; }
        #canvas-container { overflow-x: auto; border: 1px solid #ccc; margin-top: 20px; background-color: #fff; max-height: 500px; overflow-y: auto;}
        canvas { display: block; }
        .input-group { display: flex; flex-direction: column; gap: 5px; font-weight: bold; color: #444; }
        input[type="number"], input[type="text"], select { padding: 5px; font-size: 14px; border: 1px solid #ccc; border-radius: 4px; }
    </style>
</head>
<body>
<div class="container">
    <h1>🎛️ 8-Bit 紙帶樂譜編譯控制台</h1>
    <div class="subtitle">連線 IP: <span id="display-ip">192.168.4.1</span> (機台直連模式)</div>
    
    <div class="export-controls" style="background-color: #ffe0e0; border-left: 5px solid #ff5722;">
        <button id="calibrate-btn">⚙️ 啟動機台校正</button>
        <div style="font-size: 14px; color: #d32f2f;">請先放入白底黑線校正紙，並點擊此按鈕解鎖系統。</div>
    </div>

    <div class="export-controls" style="background-color: #e8f5e9; border-left: 5px solid #28a745;">
        <button id="wifi-btn" disabled>📶 無線傳送 JSON 樂譜</button>
        <div style="border-left: 2px solid #ccc; margin: 0 10px; height: 40px;"></div>
        <button id="motor-start-btn">▶️ 啟動實體馬達</button>
        <button id="motor-stop-btn">⏹️ 停止實體馬達</button>
    </div>

    <div class="controls" style="background-color: #e2efff; border-left: 5px solid #007bff;">
        <input type="file" id="midi-upload" accept=".mid,.midi">
        <div class="input-group" style="flex-grow: 1;">
            <select id="track-select" disabled><option>等待上傳 MIDI...</option></select>
        </div>
    </div>

    <div id="status">系統就緒，等待檔案上傳...</div>
    <div id="detected-range"></div>
    
    <div class="controls">
        <button id="play-btn" disabled>▶️ 預覽聲音</button>
        <button id="stop-btn" disabled>⏹️ 停止</button>
        <div class="input-group">
            <label>🎹 音色波形</label>
            <select id="waveform-select">
                <option value="sine">水晶音樂 (Sine)</option>
                <option value="square" selected>紅白機 8-Bit (Square)</option>
                <option value="triangle">復古貝斯 (Triangle)</option>
                <option value="sawtooth">電子管樂 (Sawtooth)</option>
            </select>
        </div>
        <label style="margin-left: auto;">⚙️ 轉速 (BPM): <input type="range" id="speed-slider" min="60" max="300" value="120"> <span id="bpm-val">120</span></label>
    </div>

    <div class="progress-area" id="progress-area">
        <div style="font-weight: bold; color: #856404;">🎵 播放進度：<span id="progress-text">0 / 0</span></div>
        <progress id="playback-progress" value="0" max="100"></progress>
    </div>

    <div class="export-controls">
        <div class="input-group"><label>X孔距 (mm)</label><input type="number" id="spacing-x-mm" min="2" max="20" step="0.5" value="5"></div>
        <div class="input-group"><label>Y軌距 (mm)</label><input type="number" id="spacing-y-mm" min="2" max="20" step="0.5" value="10"></div>
        <div class="input-group"><label>每行網格數</label><input type="number" id="steps-per-row" min="50" max="500" step="10" value="120"></div>
        <button id="pdf-btn" disabled>📄 匯出 A4 分頁紙帶</button>
    </div>
    <div id="canvas-container"><canvas id="tapeCanvas"></canvas></div>
</div>

<script>
// 固定 API 連線 IP 為本機 ESP32
const espIp = "192.168.4.1";

let currentMidi = null; 
let allowedNotes = [];
let lut = []; 
let paperTapeData = []; 
const MM_TO_PX = 3.779528;
let synth = null;
let isPlaying = false;
let audioStartTime = 0;
let currentStepDelaySec = 0;
let animationFrameId = null;
let lastRenderedStep = -1;

const allWhiteKeys = [
    24, 26, 28, 29, 31, 33, 35, 36, 38, 40, 41, 43, 45, 47, 48, 50, 52, 53, 55, 57, 59, 
    60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79, 81, 83, 84, 86, 88, 89, 91, 93, 95, 
    96, 98, 100, 101, 103, 105, 107, 108
];

function initLUT() {
    lut = []; lut.push([]); 
    allowedNotes.forEach(n => lut.push([n])); 
    for (let i = 0; i < allowedNotes.length; i++) {
        for (let j = i + 1; j < allowedNotes.length; j++) { lut.push([allowedNotes[i], allowedNotes[j]]); }
    }
}

function getLookupIndex(activeNotes) {
    if (activeNotes.length === 0) return 0;
    let notes = activeNotes.slice(0, 2).sort((a,b) => a-b); 
    for (let i = 0; i < lut.length; i++) {
        if (lut[i].length === notes.length && lut[i].every((val, index) => val === notes[index])) return i;
    }
    return 0; 
}

function smartMapNote(midiNote) {
    let pitchClass = midiNote % 12;
    const pitchToWhite = [0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11]; 
    let finalNote = (Math.max(Math.floor(midiNote / 12) - 1, Math.floor(allowedNotes[0] / 12) - 1) + 1) * 12 + pitchToWhite[pitchClass];
    while (finalNote > allowedNotes[14]) finalNote -= 12; 
    while (finalNote < allowedNotes[0]) finalNote += 12; 
    return finalNote;
}

document.getElementById('midi-upload').addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    
    stopPlayback();
    document.getElementById('play-btn').disabled = true; 
    document.getElementById('pdf-btn').disabled = true; 
    document.getElementById('wifi-btn').disabled = true;
    document.getElementById('detected-range').innerText = ""; 
    
    const reader = new FileReader();
    reader.onload = async function(e) {
        if(typeof Midi === 'undefined') { alert("❌ 無法載入 Tone.js，可能處於無網路環境！"); return; }
        currentMidi = new Midi(e.target.result);
        const trackSelect = document.getElementById('track-select');
        trackSelect.innerHTML = '';
        let hasTracks = false;

        currentMidi.tracks.forEach((track, index) => {
            if (track.notes.length > 0) {
                hasTracks = true;
                let option = document.createElement('option');
                option.value = index;
                option.text = `[軌道 ${index}] ${track.name || 'Unknown'} (${track.notes.length} notes) ${(track.channel === 9) ? "⚠️(Drum)" : ""}`;
                trackSelect.appendChild(option);
            }
        });
        if (hasTracks) { trackSelect.disabled = false; processMidiTrack(parseInt(trackSelect.value)); }
    };
    reader.readAsArrayBuffer(file);
});

document.getElementById('track-select').addEventListener('change', (e) => {
    stopPlayback();
    processMidiTrack(parseInt(e.target.value));
});
document.getElementById('speed-slider').addEventListener('input', (e) => document.getElementById('bpm-val').innerText = e.target.value);
document.getElementById('waveform-select').addEventListener('change', (e) => {
    if (synth) synth.set({ oscillator: { type: e.target.value } });
});

function processMidiTrack(trackIndex) {
    let track = currentMidi.tracks[trackIndex];
    let snappedNotes = track.notes.map(n => {
        const ptow = [0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11];
        return (Math.floor(n.midi / 12)) * 12 + ptow[n.midi % 12];
    });

    let maxScore = -1; let bestWindow = [];
    for (let i = 0; i <= allWhiteKeys.length - 15; i++) {
        let currentWindow = allWhiteKeys.slice(i, i + 15);
        let score = snappedNotes.filter(sn => sn >= currentWindow[0] && sn <= currentWindow[14]).length;
        if (score > maxScore) { maxScore = score; bestWindow = currentWindow; }
    }
    allowedNotes = bestWindow; initLUT(); 

    let minNoteName = Tone.Frequency(allowedNotes[0], "midi").toNote();
    let maxNoteName = Tone.Frequency(allowedNotes[14], "midi").toNote();
    document.getElementById('detected-range').innerText = `🎯 自動鎖定主旋律最佳音域：${minNoteName} ~ ${maxNoteName}`;

    const stepTicks = currentMidi.header.ppq / 2; 
    let totalSteps = Math.ceil(Math.max(...track.notes.map(n => n.ticks)) / stepTicks) + 1;
    let rawGrid = new Array(totalSteps).fill(null).map(() => []); 

    track.notes.forEach(note => {
        let stepIndex = Math.round(note.ticks / stepTicks);
        let safeNote = smartMapNote(note.midi); 
        if (!rawGrid[stepIndex].includes(safeNote)) rawGrid[stepIndex].push(safeNote);
    });

    paperTapeData = rawGrid.map(notes => notes.length === 0 ? 0 : getLookupIndex(notes.slice(0, 2).map(n => smartMapNote(n))));
    
    document.getElementById('playback-progress').max = paperTapeData.length;
    document.getElementById('status').innerText = `✅ 解析完成！共 ${paperTapeData.length} 個物理網格。`;
    drawCanvas(paperTapeData, -1);
    
    document.getElementById('play-btn').disabled = false; 
    document.getElementById('pdf-btn').disabled = false;
    document.getElementById('wifi-btn').disabled = false;
}

document.getElementById('spacing-x-mm').addEventListener('input', () => { if (paperTapeData.length > 0) drawCanvas(paperTapeData, -1); });
document.getElementById('spacing-y-mm').addEventListener('input', () => { if (paperTapeData.length > 0) drawCanvas(paperTapeData, -1); });
document.getElementById('steps-per-row').addEventListener('input', () => { if (paperTapeData.length > 0) drawCanvas(paperTapeData, -1); });

function drawCanvas(data, currentStep = -1) {
    const canvas = document.getElementById('tapeCanvas');
    const ctx = canvas.getContext('2d');
    
    const spacingX_px = (parseFloat(document.getElementById('spacing-x-mm').value) || 5) * MM_TO_PX;
    const spacingY_px = (parseFloat(document.getElementById('spacing-y-mm').value) || 10) * MM_TO_PX;
    const stepsPerRow = parseInt(document.getElementById('steps-per-row').value) || 120;
    const rectWidth = spacingX_px * 0.7; 
    const rectHeight = 3 * MM_TO_PX; 
    const margin = 20 * MM_TO_PX; 
    
    const totalRows = Math.ceil(data.length / stepsPerRow);
    const rowHeight = (7 * spacingY_px) + (margin * 2.5); 

    canvas.width = (Math.min(data.length, stepsPerRow) * spacingX_px) + (margin * 2.5);
    canvas.height = totalRows * rowHeight;
    
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    for (let row = 0; row < totalRows; row++) {
        let startY = row * rowHeight;
        let actualStepsInThisRow = Math.min(stepsPerRow, data.length - (row * stepsPerRow));
        let rowWidth = (actualStepsInThisRow * spacingX_px);
        
        ctx.setLineDash([5, 5]); ctx.strokeStyle = "#aaaaaa"; ctx.lineWidth = 1;
        ctx.strokeRect(margin/2, startY + margin/2, rowWidth + margin*1.5, (7 * spacingY_px) + margin);
        ctx.setLineDash([]); 
        
        ctx.font = "14px Arial"; ctx.fillStyle = "#888888";
        ctx.fillText("✂️ 沿虛線裁切", margin/2 + 5, startY + margin/2 - 5);

        ctx.lineWidth = 1; ctx.strokeStyle = "#e0e0e0"; ctx.font = "bold 12px Arial";
        ctx.textAlign = "right"; ctx.textBaseline = "middle";

        for (let i = 0; i < 8; i++) {
            let lineY = startY + (i * spacingY_px) + margin;
            ctx.beginPath(); ctx.moveTo(margin, lineY); ctx.lineTo(margin + rowWidth, lineY); ctx.stroke();
            ctx.fillStyle = (i === 7) ? "#d63384" : "#444444"; 
            ctx.fillText((i === 7) ? "CLK" : `D${i+1}`, margin - 10, lineY);
        }

        ctx.fillStyle = "#007bff"; ctx.textAlign = "left";
        if (row > 0) ctx.fillText(`◄ 黏接 第 ${row} 段`, margin, startY + margin/2 + 15);
        if (row < totalRows - 1) {
            ctx.textAlign = "right";
            ctx.fillText(`黏接 第 ${row + 2} 段 ►`, margin + rowWidth, startY + margin/2 + 15);
        }

        let startIdx = row * stepsPerRow;
        let endIdx = Math.min(startIdx + stepsPerRow, data.length);
        
        for (let i = startIdx; i < endIdx; i++) {
            let stateID = data[i];
            let x = ((i - startIdx) * spacingX_px) + margin;
            
            ctx.fillStyle = "#000000";
            ctx.fillRect(x - rectWidth/2, startY + (7 * spacingY_px) + margin - rectHeight/2, rectWidth, rectHeight);
            
            let binaryStr = stateID.toString(2).padStart(7, '0');
            for (let bit = 0; bit < 7; bit++) {
                if (binaryStr[bit] === '1') {
                    let y = startY + (bit * spacingY_px) + margin;
                    ctx.fillRect(x - rectWidth/2, y - rectHeight/2, rectWidth, rectHeight);
                }
            }

            if (i === currentStep) {
                ctx.fillStyle = "rgba(255, 0, 0, 0.2)";
                ctx.fillRect(x - spacingX_px/2, startY, spacingX_px, rowHeight);
                ctx.strokeStyle = "#ff0000"; ctx.lineWidth = 2;
                ctx.beginPath(); ctx.moveTo(x, startY + margin/2); ctx.lineTo(x, startY + margin/2 + (7 * spacingY_px) + margin); ctx.stroke();
            }
        }
    }
}

document.getElementById('pdf-btn').addEventListener('click', () => {
    if(typeof window.jspdf === 'undefined') { alert("❌ 無法載入 jsPDF，可能處於無網路環境！"); return; }
    const { jsPDF } = window.jspdf;
    drawCanvas(paperTapeData, -1);
    
    const doc = new jsPDF({ orientation: 'landscape', unit: 'mm', format: 'a4' });
    const canvas = document.getElementById('tapeCanvas');
    const pdfWidth = canvas.width / MM_TO_PX;
    const pdfHeight = canvas.height / MM_TO_PX;
    
    const a4Height = 200; 
    let currentY = 0;
    let pageNum = 1;

    while (currentY < pdfHeight) {
        if (pageNum > 1) doc.addPage();
        doc.addImage(canvas.toDataURL('image/png', 1.0), 'PNG', 0, -currentY, pdfWidth, pdfHeight);
        currentY += a4Height;
        pageNum++;
    }
    doc.save('MusicBox_Physical_Tape_Final.pdf');
});

async function playVirtualPaperTape() {
    if (isPlaying) return;
    if(typeof Tone === 'undefined') { alert("❌ 無法載入 Tone.js，可能處於無網路環境！"); return; }
    await Tone.start(); 
    
    if (!synth) {
        let selectedWaveform = document.getElementById('waveform-select').value;
        synth = new Tone.PolySynth(Tone.Synth, { 
            oscillator: { type: selectedWaveform }, 
            envelope: { attack: 0.05, decay: 0.2, sustain: 0.5, release: 1 } 
        }).toDestination();
    }
    
    isPlaying = true;
    lastRenderedStep = -1;
    document.getElementById('play-btn').disabled = true; 
    document.getElementById('stop-btn').disabled = false;
    document.getElementById('progress-area').style.display = 'flex';

    let bpm = parseInt(document.getElementById('speed-slider').value) || 120;
    currentStepDelaySec = ((60000 / bpm) / 2) / 1000; 
    audioStartTime = Tone.now() + 0.1; 

    for (let i = 0; i < paperTapeData.length; i++) {
        let notesToPlay = lut[paperTapeData[i]]; 
        if (notesToPlay && notesToPlay.length > 0) {
            let freqStrings = notesToPlay.map(n => Tone.Frequency(n, "midi").toNote());
            let exactTime = audioStartTime + (i * currentStepDelaySec);
            synth.triggerAttackRelease(freqStrings, "8n", exactTime);
        }
    }
    animationFrameId = requestAnimationFrame(updateVisualPlayhead);
}

function updateVisualPlayhead() {
    if (!isPlaying) return;
    let currentTime = Tone.now();
    let elapsedSec = currentTime - audioStartTime;
    let currentStep = Math.floor(elapsedSec / currentStepDelaySec);

    if (currentStep >= paperTapeData.length) {
        stopPlayback();
        return;
    }

    if (currentStep !== lastRenderedStep && currentStep >= 0) {
        lastRenderedStep = currentStep;
        document.getElementById('playback-progress').value = currentStep + 1;
        document.getElementById('progress-text').innerText = `${currentStep + 1} / ${paperTapeData.length}`;
        drawCanvas(paperTapeData, currentStep);
    }
    animationFrameId = requestAnimationFrame(updateVisualPlayhead);
}

function stopPlayback() {
    isPlaying = false;
    if (animationFrameId) cancelAnimationFrame(animationFrameId);
    if (synth) synth.releaseAll(); 
    document.getElementById('play-btn').disabled = false; 
    document.getElementById('stop-btn').disabled = true;
    drawCanvas(paperTapeData, -1); 
}

document.getElementById('play-btn').addEventListener('click', playVirtualPaperTape);
document.getElementById('stop-btn').addEventListener('click', stopPlayback);

// 🌟 API 通訊邏輯
document.getElementById('wifi-btn').addEventListener('click', async () => {
    const btn = document.getElementById('wifi-btn');
    btn.innerText = "⏳ 傳送中..."; btn.disabled = true;

    try {
        let currentBpm = parseInt(document.getElementById('speed-slider').value) || 120;
        let calculatedDelayMs = (60000 / currentBpm) / 2;
        const payload = { song_length: paperTapeData.length, score: paperTapeData, delay_ms: calculatedDelayMs };
        
        const response = await fetch(`http://${espIp}/upload`, {
            method: 'POST',
            headers: { 'Content-Type': 'text/plain' }, 
            body: JSON.stringify(payload)
        });

        if (response.ok) alert('✅ 樂譜傳送成功！機台已準備播放！');
        else alert('❌ ESP32 回應錯誤，狀態碼: ' + response.status);
    } catch (err) { alert('❌ 無法連線至 ESP32！錯誤: ' + err.message); } 
    finally { btn.innerText = "📶 無線傳送 JSON 樂譜"; btn.disabled = false; }
});

async function controlHardwareMotor(state) {
    try {
        const response = await fetch(`http://${espIp}/motor?state=${state}`);
        if (!response.ok) alert('❌ ESP32 回應錯誤');
    } catch (err) { alert('❌ 無法連線至 ESP32！錯誤: ' + err.message); }
}

// 綁定遙控按鈕
document.getElementById('motor-start-btn').addEventListener('click', () => controlHardwareMotor('start'));
document.getElementById('motor-stop-btn').addEventListener('click', () => controlHardwareMotor('stop'));
document.getElementById('calibrate-btn').addEventListener('click', async () => {
    try {
        const response = await fetch(`http://${espIp}/calibrate`);
        if (response.ok) alert('✅ 校正指令已發送！請確認機台馬達開始轉動掃描。');
    } catch (err) { alert('❌ 發送失敗，請確認連線。'); }
});
</script>
</body>
</html>
)=====";


// ==========================================
// ⚡ 省電優化：關閉步進馬達線圈
// ==========================================
void disableStepper() {
    digitalWrite(13, LOW);
    digitalWrite(27, LOW);
    digitalWrite(14, LOW);
    digitalWrite(33, LOW);
}

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
            }
            if (amp2 > 1.0) {
                sample += sin(phase2) * amp2;
                phase2 += (TWO_PI * targetFreq2) / SAMPLE_RATE;
                if (phase2 >= TWO_PI) phase2 -= TWO_PI;
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
    for (int step_count = 0; step_count < 150; step_count++) {
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
    
    // 掃描完畢，停止並釋放馬達電能
    disableStepper();

    Serial.println("✅ 校正完成！各通道動態參數如下：");
    for (int i = 0; i < 8; i++) {
        int delta = baseline_black[i] - baseline_white[i];
        if (delta < 200) delta = 500; 
        jump_up[i] = delta * 0.6;   
        jump_down[i] = delta * 0.4; 
    }
    Serial.println("\n🎶 機台已完成校正，進入讀譜待命模式！");
}

// ==========================================
// 🚀 系統初始化 (Setup)
// ==========================================
void setup() {
    Serial.begin(115200);
    myStepper.setSpeed(10);
    
    // 開機預設立刻將馬達斷電
    disableStepper();

    pinMode(pin_S0, OUTPUT); pinMode(pin_S1, OUTPUT);
    pinMode(pin_S2, OUTPUT); pinMode(pin_S3, OUTPUT);
    pinMode(pin_SIG, INPUT);

    initI2S();
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 1, NULL, 1);

    // ==========================================
    // 🌟 MAC 位址命名法與 DNS 伺服器啟動
    // ==========================================
    WiFi.mode(WIFI_AP);
    String mac = WiFi.macAddress(); 
    // 取最後 6 碼防止撞名
    String macSuffix = mac.substring(9, 11) + mac.substring(12, 14) + mac.substring(15, 17); 
    String uniqueSSID = "MusicBox_" + macSuffix; 
    
    WiFi.softAP(uniqueSSID.c_str()); 
    Serial.printf("\n✅ AP 基地台已啟動！SSID: %s\n", uniqueSSID.c_str());
    Serial.printf("IP 位址: %s\n", WiFi.softAPIP().toString().c_str());

    // 啟動 DNS 攔截
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // ==========================================
    // 🌟 API 路由設定
    // ==========================================
    
    // Captive Portal 彈出網頁首頁
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", index_html);
    });

    // 將所有找不到的路徑轉址回首頁 (強制入口的核心邏輯)
    server.onNotFound([]() {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        server.send(302, "text/plain", "");
    });

    // API 1：網頁 JSON 傳譜
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
        server.send(200, "text/plain", "樂譜接收成功！");
        isPhysicalPlaying = false; 
        isMotorRunning = false; 
        isWebPlaying = true;
        currentStep = 0;
        lastStepTime = millis();
    });

    // API 2：網頁遙控馬達啟動/停止
    server.on("/motor", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        String state = server.arg("state");
        if (state == "start") {
            isMotorRunning = true;
        } else if (state == "stop") {
            isMotorRunning = false;
            amp1 = 0.0; amp2 = 0.0; 
            disableStepper(); // 🌟 強制斷電
        }
        server.send(200, "text/plain", "OK");
    });

    // API 3：網頁校正指令接收
    server.on("/calibrate", HTTP_GET, []() {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        startCalibrationFlag = true; 
        server.send(200, "text/plain", "Calibration Started");
    });

    server.begin();
}

// ==========================================
// 🔄 主迴圈 (全自動狀態切換)
// ==========================================
// ==========================================
// 🔄 主迴圈 (全自動狀態切換與即時監控)
// ==========================================
void loop() {
    server.handleClient(); 
    dnsServer.processNextRequest(); // 維持 DNS 運作以回應彈窗請求

    if (startCalibrationFlag) {
        startCalibrationFlag = false;
        calibrateSensors();
        isCalibrated = true;
    }

    if (!isCalibrated) return;

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
                amp1 = 0.0; amp2 = 0.0;
                disableStepper(); // 播完後斷電
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
                val = analogRead(pin_SIG); // 連續讀取兩次穩定 ADC
                sensor_analog_values[i] = val; 
                
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
                    decodeState(current_binary_val);
                }
            } 
            else if (!current_sensor_state[7] && last_sensor_state[7]) {
                amp1 = 0.0; amp2 = 0.0;
            }

            for (int i = 0; i < 8; i++) last_sensor_state[i] = current_sensor_state[i];
        }
    }

    // ==========================================
    // 📊 序列埠即時監控 (每 300 毫秒輸出一次)
    // ==========================================
    if (millis() - lastSerialPrintTime > 300) {
        lastSerialPrintTime = millis();
        Serial.print("📡 即時感測 | ");
        
        for (int i = 0; i < 8; i++) {
            // 🌟 靜態除錯機制：如果馬達沒在轉，就手動更新數值方便人工作業
            if (!isMotorRunning && isPhysicalPlaying) {
                digitalWrite(pin_S0, bitRead(i, 0));
                digitalWrite(pin_S1, bitRead(i, 1));
                digitalWrite(pin_S2, bitRead(i, 2));
                digitalWrite(pin_S3, bitRead(i, 3));
                delayMicroseconds(5); 
                sensor_analog_values[i] = analogRead(pin_SIG);
            }
            Serial.printf("S%d:%4d ", i, sensor_analog_values[i]);
        }
        Serial.println();
    }
}