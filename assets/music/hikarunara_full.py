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

# === 翻譯好的樂譜 ===
score_data = """
| 6^*2 5*2 4*4 | 4^*2 2*2 3*4 | 1^*2 1*2 2*4 | 0*2 #1^*2 2^*2 3^*2 |
| 5^*4 4^*3 1^^*1 | 3^*4 4^*1 4^*1 1^^*1 0*1 | 1^^*3 7^*1 3^*2 1^*2 | 4^*2 6^*2 5^*2 4^*1 3^*1 |
| 0*8 | 0*8 |
| 3^*4 1^*2 1^*2 | 2^*4 2^*4 | 1^*2 5*2 5*2 5*2 | 5*2 5*2 5*2 5*2 |
| 5*2 3^*2 5*2 5*2 | 2^*4 5*1 5*1 5*2 | 5*2 1^*2 5^*2 1^*2 | 2^*2 6*2 1^*4 |
| 1^*2 4^*0.5 4^*0.5 4^*0.5 #5^*0.5 5^*1 2^*0.5 1^*0.5 1^*1 2^*1 | #5^*1 5^*1 3^*1 5^*1 1^^*1 0*1 3^*1 5^*1 | 1^^*1 1^*1 3^*2 #2^*1 #4^*1 1^*1 2^*0.5 3^*0.5 |
| 4^*2 3^*1.5 2^*0.5 1^*2 3^*1 7^*1 | 2^*2 7^*2 5^*2 5^*1 6^*0.5 7^*0.5 |
"""

parse_notes(score_data)
mid.save('MyMusicBox_Song.mid')
print("✅ 成功產出實體音樂盒專用 MIDI：MyMusicBox_Song.mid")