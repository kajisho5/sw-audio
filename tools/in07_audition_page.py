#!/usr/bin/env python3
"""SWINGBY: the listening page of the 128 factory presets (the "SWINGBY Preset Gallery" artifact): renders every preset's audition phrase
with build/in07_audition (tools/in07_audition.cpp), encodes MP3 160 kbps (ffmpeg), takes 72 peaks of each for the cards' waveforms, and
fills tools/in07_gallery_template.html (the logos inlined). Publish <out>/index.html with <out>/audio/*.mp3 next to it.
usage: tools/in07_audition_page.py <out dir>"""
import array, base64, json, pathlib, subprocess, sys, wave

REPO = pathlib.Path(__file__).resolve().parents[1]
out = pathlib.Path(sys.argv[1]).resolve()
wav = out / 'wav'
(out / 'audio').mkdir(parents=True, exist_ok=True)
wav.mkdir(exist_ok=True)
exe = REPO / 'build/in07_audition'
if not exe.exists():
    subprocess.run(['g++', '-std=c++17', '-O2', f'-I{REPO}/core/include', f'-I{REPO}/products', str(REPO / 'tools/in07_audition.cpp'),
                    *map(str, (REPO / 'products/in07').glob('*.cpp')), '-o', str(exe)], check=True)
subprocess.run([str(exe), str(wav)], check=True)
index = json.loads((wav / 'index.json').read_text())
data = []
for p in index:
    src = wav / p['file']
    mp3 = out / 'audio' / src.with_suffix('.mp3').name
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(src), '-codec:a', 'libmp3lame', '-b:a', '160k', str(mp3)], check=True)
    with wave.open(str(src), 'rb') as w:
        n = w.getnframes(); a = array.array('h'); a.frombytes(w.readframes(n))
    per = n // 72
    peaks = [round(max((abs(x) for x in a[b * per * 2:(b + 1) * per * 2]), default=0) / 32768, 3) for b in range(72)]
    data.append({'n': p['name'], 'c': p['category'], 'f': 'audio/' + mp3.name, 's': round(p['seconds'], 1), 'p': peaks})
logo = lambda k: base64.b64encode((REPO / f'docs/design/design-system/project/assets/product-logos/swingby-{k}.svg').read_bytes()).decode()
page = (REPO / 'tools/in07_gallery_template.html').read_text()
page = page.replace('@@DARK@@', logo('dark')).replace('@@LIGHT@@', logo('light')).replace('@@DATA@@', json.dumps(data, ensure_ascii=False, separators=(',', ':')))
(out / 'index.html').write_text(page)
print(out / 'index.html', len(data), 'presets')
