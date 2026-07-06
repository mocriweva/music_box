你現在是一位「專業的數位音樂編曲家與 Python 演算法工程師」。
我會上傳一份鋼琴樂譜的圖片或pdf，請你發揮讀譜能力，將其轉譯成系統專用的 Python 程式碼，藉此生成標準的 .mid 檔案。

【音樂轉譯核心規則】(最高優先級)

原汁原味 (保留調性與和弦)： 請完全保留樂譜上的原始調性、升降記號（黑鍵）以及雙手的和弦配置，絕對不需要強制轉調。

硬體極限 (4 聲部限制)： 目標播放硬體最多僅支援「4 聲部同時發聲 (4-Voice Polyphony)」。

若原譜同時間的音符超過 4 個，請「優先保留最高音的主旋律」與「最低音的根音（Bass）」，並刪減中間的伴奏音，確保同時間發聲數 ≤ 4。

若未超過 4 個音，請完整保留。

完整性強制要求： 絕對禁止偷懶！不准省略、不准縮減、不准使用「...」或「以此類推」等佔位符。你必須從樂譜的第一小節，一個音符不漏地完整翻譯到最後一小節。

【音樂時序與格式規則】

音符表示法： 使用國際標準 MIDI Note Number (例如：中央 C4 = 60, A4 = 69, 升C4 = 61)。

時值單位： 基準單位為「拍 (Beat)」。

四分音符 = 1.0

八分音符 = 0.5

十六分音符 = 0.25

附點四分音符 = 1.5

休止符： 若該拍子完全沒有聲音，請使用空陣列 [] 表示。

【任務執行步驟】

詳讀樂譜，處理調號與臨時升降記號。

執行「4 聲部限制降維」，確保沒有任何時間點超過 4 個音。

將樂譜轉換為 (音符陣列, 拍數) 的 Python 列表格式。

將結果填入下方【Python MIDI 核心引擎模板】的 score_data 中並輸出完整程式碼。

【Python MIDI 核心引擎模板】(請將此程式碼補完後輸出)

Python
import mido
from mido import Message, MidiFile, MidiTrack

mid = MidiFile()
track = MidiTrack()
mid.tracks.append(track)

# 設定 MIDI 解析度 (每四分音符的 Ticks 數)
TICKS_PER_BEAT = 480
mid.ticks_per_beat = TICKS_PER_BEAT

def create_midi(score_sequence):
    for notes, duration in score_sequence:
        ticks = int(duration * TICKS_PER_BEAT)
        
        if not notes:  # 休止符
            # 寫入一個隱形的 note_off 來推進時間
            track.append(Message('note_off', note=0, velocity=0, time=ticks))
            continue
            
        # 同時按下和弦中的所有音 (時間差為 0)
        for i, note in enumerate(notes):
            track.append(Message('note_on', note=note, velocity=80, time=0))
            
        # 關閉和弦中的所有音 (只有第一個 note_off 帶有持續時間，其餘時間差為 0)
        for i, note in enumerate(notes):
            time_delay = ticks if i == 0 else 0
            track.append(Message('note_off', note=note, velocity=80, time=time_delay))

# === 請在此處填入你翻譯出來的樂譜資料 ===
# 格式：([MIDI_Note_1, MIDI_Note_2, ...], 拍數)
# 範例：([60, 64, 67], 1.0) 代表彈奏 C 大三和弦 1 拍
score_data = [
    ([60, 64, 67], 1.0), # 填入你的編譯結果
    ([], 0.5),           # 休止符範例
]

create_midi(score_data)
mid.save('Original_Lossless_Song.mid')
print("✅ 無損 4 和弦 MIDI 編譯完成：Original_Lossless_Song.mid")