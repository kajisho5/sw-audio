#!/usr/bin/env python3
"""SWINGBY demo songs: numbers about a master (tools/in07_song.cpp), by section, for checks without ears.
  - octave bands (31 Hz .. 16 kHz) against a -3 dB/octave (pink) line through 1 kHz: what sticks out or is missing
  - the low end's width (side over mid under 120 Hz, dB: a club needs it mono, under -15 dB) and the whole width
  - the sample peak and the DC offset
No click check: three detectors were tried on these masters (2026-10-10) and none found steps of 0.02..0.15 put into them on purpose
without also flagging the clipped basses' corners or the drums' attacks; clicks are the engine's unit tests' job (preset changes, note
ends, the gate).
usage: tools/in07_song_check.py <master.wav> <events.json>      (the events: SW_SONG_EVENTS=<file> build/in07_song <song> <out.wav>)
Needs numpy."""
import json, sys, wave

import numpy as np


def load(path):
    w = wave.open(path)
    n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n)
    if sw == 3:
        a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = a[:, 0].astype(np.int32) | (a[:, 1].astype(np.int32) << 8) | (a[:, 2].astype(np.int32) << 16)
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / 8388607.0
    else:
        x = np.frombuffer(raw, dtype=np.int16) / 32767.0
    return x.reshape(-1, ch), w.getframerate()


def spectrum(m, fs, N=1 << 15):
    acc, k = np.zeros(N // 2 + 1), 0
    for i in range(0, max(1, len(m) - N), N // 2):
        seg = m[i:i + N]
        if len(seg) < N:
            break
        acc += np.abs(np.fft.rfft(seg * np.hanning(N))) ** 2
        k += 1
    return np.fft.rfftfreq(N, 1 / fs), acc / max(k, 1)


def main():
    x, fs = load(sys.argv[1])
    ev = json.loads(open(sys.argv[2]).read())
    bar = 4 * 60.0 / ev['bpm']
    cent = [31.25 * 2 ** k for k in range(10)]
    print(f"{ev['title']} ({ev['style']}, {ev['bpm']:g} bpm): {len(x) / fs:.1f} s, sample peak {20 * np.log10(np.abs(x).max() + 1e-12):.2f} dBFS, "
          f"DC {100 * np.abs(x.mean(axis=0)).max():.3f} %")
    print('section     bands vs pink (31 Hz .. 16 kHz, dB)                       low width  width')
    for s in ev['sections']:
        a, b = int(s['bar0'] * bar * fs), int(s['bar1'] * bar * fs)
        seg = x[a:b]
        mid, side = seg.mean(axis=1), 0.5 * (seg[:, 0] - seg[:, 1])
        f, pm = spectrum(mid, fs)
        _, ps = spectrum(side, fs)
        e = np.array([10 * np.log10((pm + ps)[(f >= c / np.sqrt(2)) & (f < c * np.sqrt(2))].sum() + 1e-20) for c in cent])
        pink = np.array([e[5] + 3.0 * (5 - i) for i in range(10)])
        low = (f < 120) & (f > 20)
        lw = 10 * np.log10(ps[low].sum() / (pm[low].sum() + 1e-20) + 1e-20)
        wd = 10 * np.log10(ps.sum() / (pm.sum() + 1e-20) + 1e-20)
        print(f"{s['name']:10s} " + ' '.join(f'{v:+5.1f}' for v in (e - pink)) + f'   {lw:6.1f}  {wd:6.1f}')

main()
