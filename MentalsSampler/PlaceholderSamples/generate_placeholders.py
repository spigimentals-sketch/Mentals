import wave
import struct
import math
import random

SAMPLE_RATE = 44100

def envelope(t, duration, attack, decay_rate):
    if t < attack:
        return t / attack
    return math.exp(-decay_rate * (t - attack))

def synth_tone(duration, freq, attack, decay_rate, waveform, harmonics=None):
    n = int(SAMPLE_RATE * duration)
    samples = []
    for i in range(n):
        t = i / SAMPLE_RATE
        env = envelope(t, duration, attack, decay_rate)
        phase = 2.0 * math.pi * freq * t
        if waveform == "sine":
            value = math.sin(phase)
        elif waveform == "saw":
            value = 2.0 * ((freq * t) % 1.0) - 1.0
        elif waveform == "square":
            value = 1.0 if (freq * t) % 1.0 < 0.5 else -1.0
        elif waveform == "harmonics":
            value = 0.0
            for amp, mult in harmonics:
                value += amp * math.sin(phase * mult)
        elif waveform == "noise":
            value = random.uniform(-1.0, 1.0)
        elif waveform == "bell":
            carrier = math.sin(phase)
            modulator = math.sin(phase * 3.5) * math.exp(-3.0 * t)
            value = math.sin(phase + 2.0 * modulator)
        else:
            value = 0.0
        samples.append(value * env)
    return samples

def write_wav(path, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.85 / peak
    with wave.open(path, "w") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(SAMPLE_RATE)
        frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples)
        f.writeframes(frames)

presets = [
    ("Brass",      dict(duration=1.0, freq=220.0, attack=0.01, decay_rate=2.5, waveform="saw")),
    ("Woodwinds",  dict(duration=1.0, freq=330.0, attack=0.05, decay_rate=1.5, waveform="sine")),
    ("Strings",    dict(duration=1.4, freq=196.0, attack=0.15, decay_rate=1.0, waveform="saw")),
    ("Mallets",    dict(duration=0.6, freq=440.0, attack=0.002, decay_rate=6.0, waveform="sine")),
    ("Percussion", dict(duration=0.4, freq=0.0,   attack=0.001, decay_rate=10.0, waveform="noise")),
    ("Choir",      dict(duration=1.5, freq=261.63, attack=0.2, decay_rate=0.8, waveform="harmonics",
                         harmonics=[(0.6, 1.0), (0.3, 2.0), (0.15, 3.0)])),
    ("Keys",       dict(duration=0.9, freq=440.0, attack=0.003, decay_rate=3.0, waveform="harmonics",
                         harmonics=[(0.7, 1.0), (0.3, 2.0), (0.1, 4.0)])),
    ("Bass",       dict(duration=0.8, freq=82.41, attack=0.01, decay_rate=2.0, waveform="sine")),
    ("Synth",      dict(duration=0.8, freq=220.0, attack=0.005, decay_rate=2.5, waveform="square")),
    ("Guitar",     dict(duration=0.9, freq=196.0, attack=0.003, decay_rate=3.5, waveform="saw")),
    ("Organ",      dict(duration=1.2, freq=220.0, attack=0.02, decay_rate=0.5, waveform="harmonics",
                         harmonics=[(0.5, 1.0), (0.3, 2.0), (0.2, 3.0)])),
    ("Bells",      dict(duration=1.8, freq=523.25, attack=0.002, decay_rate=1.2, waveform="bell")),
]

for name, params in presets:
    samples = synth_tone(**params)
    out_path = name.replace(" ", "_") + ".wav"
    write_wav(out_path, samples)
    print("Wrote", out_path)
