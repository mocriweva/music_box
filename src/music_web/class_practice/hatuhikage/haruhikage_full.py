import mido
import re
from mido import Message, MidiFile, MidiTrack

# --- 1. 初始化 MIDI 與調性設定 ---
mid = MidiFile()
track = MidiTrack()
mid.tracks.append(track)

# 實體感測器對應：G3(55) 到 G5(79)，基準音 1 設為 C4(60)
key_C_major = {
    '0': 0, 
    '1': 60, '2': 62, '3': 64, '4': 65, '5': 67, '6': 69, '7': 71
}

ticks_per_beat = mid.ticks_per_beat
ticks_per_eighth = int(ticks_per_beat / 2) # 6/8拍：1單位 = 1個八分音符

# --- 2. 核心解析引擎 ---
def parse_and_add_notes(score_string):
    tokens = score_string.split()
    for token in tokens:
        if token == '|': continue
        match = re.match(r'([0-7])([#b]*)([\^v]*)(\*[\d\.]+)?', token)
        if not match: continue
            
        note_char, accidental, octaves, duration_str = match.groups()
        duration = float(duration_str.replace('*', '')) if duration_str else 1.0
            
        if note_char == '0':
            track.append(Message('note_off', note=0, velocity=0, time=int(duration * ticks_per_eighth)))
            continue
            
        midi_note = key_C_major[note_char]
        if accidental == '#': midi_note += 1
        elif accidental == 'b': midi_note -= 1
        for char in octaves:
            if char == '^': midi_note += 12
            elif char == 'v': midi_note -= 12
            
        track.append(Message('note_on', note=midi_note, velocity=80, time=0))
        track.append(Message('note_off', note=midi_note, velocity=80, time=int(duration * ticks_per_eighth)))

# --- 3. 《春日影》完整實體樂譜 ---
# 語法：[音符][八度]*[拍數] (1拍=八分音符)
haruhikage_score = """
# 【前奏 Intro】
| 3^*3 2^*1 1^*1 2^*1 | 3^*3 4^*0.5 3^*0.5 2^*2 | 3^*3 2^*1 1^*1 2^*1 | 3^*3 4^*0.5 3^*0.5 2^*2 |
| 3^*3 2^*1 1^*1 2^*1 | 3^*2 4^*0.5 3^*0.5 2^*1 1*1 2*1 | 

# 【主歌 A段 (かじかんだ心)】
| 3*1 3*1 2*1 4*1 3*1 2*1 | 2*1 2*1 1*1 4*1 3*1 2*1 | 2*1 1*1 2*1 3*3 | 0*3 3*1 5*1 1^*1 | 
| 7*2 1^*1 7*1 1^*2 | 7*1 6*1 5*1 5*1 2*1 4*2 | 4*1 3*1 3*1 5*3 | 4*1 3*1 2*1 3*1 5*2 | 
| 1*3 0*2 1*1 | 2*1 1*1 1*1 5v*1 1*2 | 4*1 3*1 2*1 1*1 1*3 | 0*2 1*1 2*1 |

# 【主歌 A段重複 (暗がりの中)】
| 3*1 3*1 2*1 4*1 3*1 2*1 | 2*1 2*1 1*1 4*1 3*1 2*1 | 2*1 1*1 2*1 3*3 | 0*3 3*1 5*1 1^*1 |
| 7*2 1^*1 7*1 1^*2 | 7*1 6*1 5*1 5*1 2*1 4*2 | 4*1 3*1 3*1 5*3 | 4*1 3*1 2*1 3*1 5*2 |
| 1*3 0*2 1*1 | 2*1 1*1 1*1 5v*1 1*2 |

# 【導歌 B段 (求め続けた)】
| 4*1 4*1 4*1 3*1 2*1 1*1 | 1*3 0*3 | 6v*1 5v*1 5v*1 5v*1 4v*1 4v*1 | 3v*1 2v*1 2v*1 2v*1 5v*2 |
| 5v*1 4v*1 4v*1 4v*1 3v*1 2v*1 | 2v*1 1v*1 7v*1 1*3 | 6v*1 5v*1 5v*1 5v*1 4v*1 4v*1 | 3v*1 2v*1 2v*1 2v*1 3v*2 |
| 4*1 3*1 3*1 3*1 3*1 2*1 | 2^*1 1^*2 1^*1 1^*2 | 7*1 6*2 6*3 | 0*1 6*1 6*1 5*1 4*1 4*1 |
| 4*1.5 3*0.5 4*1 5*3 | 0*6 |

# 【副歌 Chorus (雲間を縫ってきらり)】
| 3*0.5 2*0.5 3*0.5 2*0.5 3*1 4*1 | 5*1 4*0.5 5*0.5 6*1 6*1 7*1 1^*1 | 2^*1 1^*1 5*2 5*1 4*1 | 4*2 3*1 3*1 4*1 5*3 |
| 3*0.5 2*0.5 3*0.5 2*0.5 3*1 4*1 | 5*1 4*0.5 5*0.5 6*1 5#*1 6*1 7*1 | 0*1 5#*1 3^*1 3^*1 0*1 6*1 | 4^*1 3^*1 2^*1 2^*2 1^*1 |
| 1^*1 7*1 1^*1 5*1 1^*1 2^*1 | 1^*1 1^*1 1*1 5*2 | 2^*1 1^*1 1^*1 1*1 5*1 1^*1 | 2^*1 1^*1 1^*1 1*1 5*1 1^*1 |
| 2^*1.5 3^*0.5 2^*1 1^*1 1^*2 | 7*1 6*2 6*1 5*2 | 5*1 4*1 4*1 3*1 2*2 | 3*3 0*3 | 3*1 4*1 3*1 4*1 3*1 2*1 | 1*6 |

# 【尾奏 Outro】
| 3^*3 4^*1 3^*1 2^*1 | 1*1 2*1 1*6 |
"""

print("🎵 啟動終極引擎，正在編譯《春日影》全曲...")
parse_and_add_notes(haruhikage_score)
mid.save('Haruhikage_Ultimate.mid')
print("✅ 成功產出實體音樂盒專用 MIDI：Haruhikage_Ultimate.mid")