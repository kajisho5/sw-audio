#!/usr/bin/env python3
"""SWINGBY: the pictures and the demo video of the sales page, made from the real window (ui/in07, the preview's mock of the native side,
tools/in07_ui_preview.py) and the real sound (build/in07_audition: the factory presets' audition phrases, exactly as the plug-in plays
them). Nothing is drawn by hand: when the window or the sound changes, run this again.
  <out>/screen_<play|layer|arp|mod|fx>_<dark|light>.webp   1920 x 1290 (the window at 1.5x), the five screens in both looks
  <out>/demo.mp4                                           40 s, 1280 x 860, 30 fps: PLAY (dark) through the first 10 factory presets,
                                                           the window following the notes of the sound (the same phrase, the same times)
usage: tools/in07_marketing_media.py <out dir> [--no-video]
Needs build/in07_ui_data, build/in07_audition (built here when missing), Playwright (Chromium), ffmpeg (libx264, aac), Pillow."""
import json, math, pathlib, struct, subprocess, sys, wave

from PIL import Image
from playwright.sync_api import sync_playwright

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from in07_ui_preview import REPO, build_page  # noqa: E402

out = pathlib.Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
VIDEO = '--no-video' not in sys.argv

# the audition phrases: build/in07_audition writes each preset's notes (products/in07/presets.cpp presetAudition(): its category's phrase,
# or its notes held when it plays the arpeggiator or the gate) into index.json, (start s, length s, key)
def phrase(entry):
    return [tuple(n) for n in entry['notes']]

# the sales page's chapter list (LISTEN): presets 1..10 of the factory list, the seconds each one plays
SEGMENTS = [(0, 4), (1, 3), (2, 4), (3, 5), (4, 5), (5, 4), (6, 2), (7, 4), (8, 4), (9, 5)]
FPS, FS = 30, 48000


def page_for(theme):
    work = out / '_page'
    work.mkdir(exist_ok=True)
    return build_page(work, '60', theme)


def prepare(pg):
    # no learned controllers, a licensed copy (the chip shows LICENSED, not TRIAL), the preset list at the top
    pg.evaluate("""() => {
      window.MOCK_CC = [-1, -1, -1, -1, -1, -1, -1, -1];
      window.MOCK_LIC = { state: 'licensed', id: 'L-20261009-0000demo', machine: window.MOCK_LIC.machine };
      SW.reply('licence', window.MOCK_LIC);
    }""")


def load(pg, i):
    pg.evaluate('i => SW.loadPreset(SW.presets.factory[i])', i)


def shots():
    plan = [  # screen, preset, what the screen shows
        ('play', 0, None), ('layer', 0, None), ('arp', 5, 'arp'), ('mod', 2, None), ('fx', 10, None)]
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--allow-file-access-from-files'])
        for theme in ('dark', 'light'):
            pg = br.new_page(viewport={'width': 1280, 'height': 860}, device_scale_factor=1.5)
            errors = []
            pg.on('pageerror', lambda e: errors.append(str(e)))
            pg.goto(page_for(theme).as_uri())
            pg.wait_for_timeout(1500)
            pg.evaluate('document.fonts.ready')
            prepare(pg)
            for screen, preset, extra in plan:
                load(pg, preset)
                pg.wait_for_timeout(200)
                pg.evaluate('s => SW.go(s)', screen)
                if extra == 'arp':   # the arpeggiator and the trance gate switched on, a chord held: the rings run
                    pg.evaluate("""() => {
                      SW.P.tap('in07.arp.on', 1); SW.P.tap('in07.arp.mode', 2); SW.P.tap('in07.arp.octaves', 2);
                      SW.P.tap('in07.gate.on', 1); SW.P.tap('in07.gate.depth', 70);
                      [1,0,1,1, 1,0,1,0, 1,1,0,1, 1,0,1,1].forEach((v, k) => SW.P.tap('in07.gate.step' + (k + 1), v));
                      [100,60,85,40, 100,70,90,0, 100,55,80,45, 95,65,85,30].forEach((v, k) => SW.P.tap('in07.arp.vel' + (k + 1), v));
                      [48, 55, 60].forEach(k => SW.noteOn(k, 0.8));
                    }""")
                else:
                    pg.evaluate('SW.noteOn(60, 0.8)')
                pg.wait_for_timeout(450 if extra != 'arp' else 1300)
                path = out / f'screen_{screen}_{theme}.png'
                pg.screenshot(path=str(path))
                pg.evaluate('() => { for (let k = 0; k < 128; k++) SW.noteOff(k); }')
                if extra == 'arp':
                    pg.evaluate("() => { SW.P.tap('in07.arp.on', 0); SW.P.tap('in07.gate.on', 0); }")
                im = Image.open(path).convert('RGB')
                im.save(path.with_suffix('.webp'), 'WEBP', quality=88, method=6)
                path.unlink()
                print(path.with_suffix('.webp').name, im.size)
            if errors:
                sys.exit(f'page errors: {errors[:5]}')
            pg.close()
        br.close()


def audio():
    exe = REPO / 'build/in07_audition'
    subprocess.run(['g++', '-std=c++17', '-O2', f'-I{REPO}/core/include', f'-I{REPO}/products', str(REPO / 'tools/in07_audition.cpp'),
                    *map(str, sorted((REPO / 'products/in07').glob('*.cpp'))), '-o', str(exe)], check=True)
    wav = out / '_wav'
    wav.mkdir(exist_ok=True)
    subprocess.run([str(exe), str(wav)], check=True)
    index = json.loads((wav / 'index.json').read_text())
    L, R = [], []
    fade = int(0.005 * FS)   # 5 ms at each joint, so a cut never clicks
    for i, secs in SEGMENTS:
        with wave.open(str(wav / index[i]['file']), 'rb') as w:
            n = int(secs * FS)
            raw = w.readframes(n)
        s = struct.unpack('<%dh' % (len(raw) // 2), raw)
        l, r = list(s[0::2]), list(s[1::2])
        l += [0] * (n - len(l)); r += [0] * (n - len(r))
        for k in range(fade):
            g = k / fade
            l[k] *= g; r[k] *= g
            l[n - 1 - k] *= g; r[n - 1 - k] *= g
        L += l; R += r
    total = len(L)
    out_fade = int(2.0 * FS)   # the last 2 s fade out
    for k in range(out_fade):
        g = 0.5 - 0.5 * math.cos(math.pi * k / out_fade)
        L[total - 1 - k] *= g; R[total - 1 - k] *= g
    path = out / '_demo.wav'
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(FS)
        w.writeframes(b''.join(struct.pack('<hh', int(round(a)), int(round(b))) for a, b in zip(L, R)))
    return path, index


def video(wav, index):
    frames = out / '_frames'
    frames.mkdir(exist_ok=True)
    for f in frames.glob('*.png'):
        f.unlink()
    # the events of the video: a preset at the start of its segment, then its phrase's notes (the sound's own times)
    ev, t0 = [], 0.0
    for i, secs in SEGMENTS:
        ev.append((t0, 'load', i))
        for start, length, key in phrase(index[i]):
            if start < secs:
                ev.append((t0 + start, 'on', key))
                ev.append((min(t0 + start + length, t0 + secs - 0.02), 'off', key))
        t0 += secs
    ev.sort(key=lambda e: (e[0], {'off': 0, 'load': 1, 'on': 2}[e[1]]))
    n = int(t0 * FPS)
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--allow-file-access-from-files'])
        pg = br.new_page(viewport={'width': 1280, 'height': 860}, device_scale_factor=1)
        pg.clock.install(time=0)   # the page's time (animation frames, timers) runs only as far as each frame
        pg.goto(page_for('dark').as_uri())
        pg.clock.run_for(1500)
        pg.wait_for_timeout(1500)   # the fonts and pictures load in real time
        pg.evaluate('document.fonts.ready')
        pg.clock.run_for(500)
        prepare(pg)
        pg.evaluate("SW.go('play')")
        e = 0
        for k in range(n):
            t = k / FPS
            while e < len(ev) and ev[e][0] <= t + 1e-9:
                _, kind, arg = ev[e]
                if kind == 'load': load(pg, arg)
                elif kind == 'on': pg.evaluate('k => SW.noteOn(k, 0.8)', arg)
                else: pg.evaluate('k => SW.noteOff(k)', arg)
                e += 1
            pg.clock.run_for(round((k + 1) * 1000 / FPS) - round(k * 1000 / FPS))   # whole milliseconds, no drift
            pg.screenshot(path=str(frames / f'{k:05d}.png'))
            if k % 150 == 0:
                print(f'frame {k} / {n}', flush=True)
        br.close()
    mp4 = out / 'demo.mp4'
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', str(FPS), '-i', str(frames / '%05d.png'), '-i', str(wav),
                    '-c:v', 'libx264', '-preset', 'slow', '-crf', '20', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                    '-c:a', 'aac', '-b:a', '160k', '-shortest', str(mp4)], check=True)
    print(mp4, mp4.stat().st_size, 'bytes')


shots()
if VIDEO:
    w, idx = audio()
    video(w, idx)
