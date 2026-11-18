"""Author deterministic Foley/electronic/ambience WAVs; no models or services."""
from pathlib import Path
import hashlib
import json
import math
import wave
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "ArtSource" / "Audio"
OUT.mkdir(parents=True, exist_ok=True)
RATE = 48000
rng = np.random.default_rng(1030)


def write(name, signal, description, loop=False):
    signal = np.asarray(signal)
    signal = signal / max(1, np.max(np.abs(signal)) / .94)
    data = np.asarray(signal * 32767, dtype="<i2")
    path = OUT / (name + ".wav")
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1 if data.ndim == 1 else data.shape[1]); wav.setsampwidth(2); wav.setframerate(RATE); wav.writeframes(data.tobytes())
    return {"name": name, "source": "Locally authored deterministic synthesis; seed 1030", "description": description,
            "sample_rate": RATE, "seconds": len(signal) / RATE, "loop": loop, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


records = []
t = np.arange(int(RATE * .42)) / RATE
noise = rng.normal(0, 1, len(t))
gun = noise * np.exp(-t * 55) * .64 + np.sin(2 * np.pi * (115 * t - 45 * t * t)) * np.exp(-t * 23) * .48
gun += np.roll(noise, 380) * np.exp(-np.maximum(0, t - .018) * 55) * ((t > .018) & (t < .1)) * .14
records.append(write("S_RifleReport", gun, "Impulse, low pressure tail and short mechanical reflection"))
t = np.arange(int(RATE * .2)) / RATE
quiet = rng.normal(0, .18, len(t)) * np.exp(-t * 65) + .33 * np.sin(2 * np.pi * 190 * t) * np.exp(-t * 40)
records.append(write("S_SuppressedReport", quiet, "Short low report; audibility is also governed by server hearing rules"))
t = np.arange(int(RATE * .23)) / RATE
foot = rng.normal(0, .25, len(t)) * np.exp(-t * 30) + .2 * np.sin(2 * np.pi * 95 * t) * np.exp(-t * 45)
records.append(write("S_DirtFootstep", foot, "Boot impact and gravel scrape"))
t = np.arange(int(RATE * 4)) / RATE
envelope = np.sin(np.minimum(t * 30, np.pi / 2)) ** 2 * np.minimum(1, (4 - t) * 30)
call = .1 * np.sin(2 * np.pi * (600 * t + 80 * np.sin(2 * np.pi * 2 * t) / (2 * np.pi * 2))) * (np.mod(t, .5) < .3) * envelope
records.append(write("S_AlarmCalling", call, "Interrupted call warning, distinct from successful alarm"))
t = np.arange(int(RATE * 3)) / RATE
alarm = .18 * np.sin(2 * np.pi * np.where(np.mod(t, .8) < .4, 880, 660) * t) * np.minimum(1, t * 50) * np.minimum(1, (3 - t) * 30)
records.append(write("S_AlarmSent", alarm, "Successful external alarm transmission"))
t = np.arange(int(RATE * .12)) / RATE
records.append(write("S_HitConfirm", .09 * np.sin(2 * np.pi * 1700 * t) * np.exp(-t * 65), "Subtle confirmed hit tick"))
t = np.arange(int(RATE * .6)) / RATE
records.append(write("S_Detection", .1 * np.sin(2 * np.pi * (420 * t + 180 * t * t)) * np.sin(np.pi * t / .6) ** 2, "Directional visual warning is accompanied by a short rising cue"))
duration = 24
length = RATE * duration
frequency = np.fft.rfftfreq(length, 1 / RATE)
spectrum = rng.normal(size=len(frequency)) + 1j * rng.normal(size=len(frequency))
spectrum *= np.exp(-frequency / 1700) * (1 - np.exp(-frequency / 75))
wind = np.fft.irfft(spectrum, n=length)
wind /= max(np.max(np.abs(wind)), 1e-9)
t = np.arange(length) / RATE
wind *= (.025 + .015 * np.sin(2 * np.pi * t / duration) ** 2)
records.append(write("S_MountainWind", np.column_stack((wind, np.roll(wind, 350))), "Periodic filtered wind, seamless stereo loop", True))
(OUT / "provenance.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
print(json.dumps({"local_audio_assets": len(records), "network": False, "inference": False}))
