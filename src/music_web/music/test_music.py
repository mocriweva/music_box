import mido
import re
from mido import Message, MidiFile, MidiTrack

mid = MidiFile()
track = MidiTrack()
mid.tracks.append(track)

key_C_major = {'0': 0, '1': 60, '2': 62, '3': 64, '4': 65, '5': 67, '6': 69, '7': 71}
ticks_per_eighth = int(mid.ticks_per_beat / 2)

def parse_notes(score_string):
    tokens = score_string.split()
    for token in tokens:
        if token == '|': continue
        match = re.match(r'([0-7])([#b]*)([\^v]*)(\*[\d\.]+)?', token)
        if not match: continue
        n, acc, octv, dur_str = match.groups()
        dur = float(dur_str.replace('*', '')) if dur_str else 1.0
        
        if n == '0':
            track.append(Message('note_off', note=0, velocity=0, time=int(dur * ticks_per_eighth)))
            continue
            
        midi_note = key_C_major[n]
        if acc == '#': midi_note += 1
        elif acc == 'b': midi_note -= 1
        
        for c in octv:
            if c == '^': midi_note += 12
            elif c == 'v': midi_note -= 12
            
        track.append(Message('note_on', note=midi_note, velocity=80, time=0))
        track.append(Message('note_off', note=midi_note, velocity=80, time=int(dur * ticks_per_eighth)))

# === 優化後的實體樂譜資料 ===
score_data = """
| 3^*2 2^*2 1^*2 2^*2 | 3^*1.5 4^*0.5 3^*1 2^*4 | 3^*2 2^*2 1^*2 2^*2 | 3^*1.5 4^*0.5 3^*1 2^*4 |
| 3^*2 2^*2 1^*2 2^*2 | 3^*1.5 4^*0.5 3^*1 2^*2 1*1 2*1 | 3*2 3*1 2*1 4*2 3*1 2*1 | 2*2 2*1 1*1 1*2 4*1 2*1 |
| 2*2 1*1 2*1 3*4 | 0*3 3*1 5*1 1^*1 7*2 | 7*2 1^*2 7*2 1^*2 | 7*1 6*1 5*2 5*1 2*1 4*2 |
| 4*2 3*1 3*1 5*4 | 4*1 3*1 2*2 3*2 5*2 | 1*6 0*1 1*1 | 2*2 1*1.5 1*0.5 1*2 5*1 1*1 |
| 4*2 3*1 2*1 1*2 1*2 | 1*5 0*1 1*1 2*1 | 3*2 3*1 2*1 4*2 3*1 2*1 | 2*2 2*1 1*1 4*2 3*1 2*1 |
| 2*2 1*1 2*1 3*4 | 0*3 3*1 5*1 1^*1 7*2 | 7*2 1^*2 7*2 1^*2 | 7*1 6*1 5*2 5*1 2*1 4*2 |
"""

parse_notes(score_data)
mid.save('sun.mid')
print("✅ 實體音樂盒專用 MIDI 編譯完成：MyMusicBox_Song.mid")