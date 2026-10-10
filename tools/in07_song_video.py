#!/usr/bin/env python3
"""SWINGBY: a vertical video (1080 x 1920, for X / TikTok / Shorts) of a demo song with the real plug-in window following the music: the PLAY
screen shows the preset that is playing (its values as the song sets them, the orbit view on its notes, the host's beat), the list under it
lights each track's sound as it plays, and the song's sections run along the bottom. The window is ui/in07 with the preview's mock of the
native side (tools/in07_ui_preview.py); the sound is the song's master (tools/in07_song.cpp). Nothing is drawn by hand.
  tools/in07_song_video.py <song> <out.mp4> [--bars 12:24] [--end 3.0] [--still <seconds into the clip> <out.png>]
Renders the song (build/in07_song, built when missing) for its master and its events. Needs build/in07_ui_data, Playwright (Chromium),
ffmpeg (libx264, aac)."""
import json, math, os, pathlib, subprocess, sys

from playwright.sync_api import sync_playwright

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from in07_ui_preview import REPO, build_page  # noqa: E402

FPS = 30
W, H = 1080, 1920
SCALE = W / 1280          # the window across the whole width
WIN_TOP = 380
NOTE_OFF, SET_PARAM, MOD_WHEEL, BEND, NOTE_ON = 0, 1, 2, 3, 4

args = sys.argv[1:]
if len(args) < 2:
    sys.exit(__doc__)
song, out = args[0], pathlib.Path(args[1]).resolve()
opt = lambda k, d: args[args.index(k) + 1] if k in args else d
bar0, bar1 = (int(x) for x in opt('--bars', '12:24').split(':'))
END = float(opt('--end', '3.0'))
STILL = float(args[args.index('--still') + 1]) if '--still' in args else None
work = out.parent / ('_' + out.stem)
work.mkdir(parents=True, exist_ok=True)


def song_files():
    exe = REPO / 'build/in07_song'
    if not exe.exists():
        subprocess.run(['g++', '-std=c++17', '-O2', '-Icore/include', '-Iproducts', '-Itools', 'tools/in07_song.cpp',
                        *map(str, sorted((REPO / 'products/in07').glob('*.cpp'))), 'products/ms01/ms01.cpp', 'products/ms04/ms04.cpp',
                        *map(str, sorted((REPO / 'core/src').glob('*.cpp'))), *map(str, sorted((REPO / 'core/third_party/monocypher').glob('*.c'))),
                        '-o', str(exe)], cwd=REPO, check=True)
    ev, wav = work / 'events.json', work / 'master.wav'
    env = dict(os.environ, SW_SONG_EVENTS=str(ev))
    if not wav.exists() or STILL is not None:
        subprocess.run([str(exe), song, '-' if STILL is not None else str(wav)], cwd=REPO, env=env, check=True, stdout=subprocess.DEVNULL)
    return json.loads(ev.read_text()), wav


data, wav = song_files()
BEAT = 60.0 / data['bpm']
T0, T1 = bar0 * 4 * BEAT, bar1 * 4 * BEAT
TOTAL = T1 - T0 + END
tracks = data['tracks']
shown = [k for k, t in enumerate(tracks) if any(e[1] == NOTE_ON and bar0 * 4 <= e[0] < bar1 * 4 + 1 for e in t['events'])]
ROLE_ORDER = {'lead': 0, 'music': 1, 'bed': 2}
SOURCE = {'factory': 'FACTORY', 'user': 'ULTRA PACK', 'song': 'MADE FOR THIS TRACK'}


class State:
    """Every track's values, held notes and last note times, the events applied up to a time (seconds into the song)."""
    def __init__(self):
        self.vals = [list(t['plain']) for t in tracks]
        self.held = [set() for _ in tracks]
        self.on_t = [-1e9] * len(tracks)
        self.off_t = [-1e9] * len(tracks)
        self.count = [0] * len(tracks)
        self.pos = [0] * len(tracks)
        self.changed = set()

    def run_to(self, t):
        for k, tr in enumerate(tracks):
            ev, i = tr['events'], self.pos[k]
            while i < len(ev) and ev[i][0] * BEAT <= t + 1e-9:
                beat, kind, a, v = ev[i]
                if kind == NOTE_ON:
                    self.held[k].add(a); self.on_t[k] = beat * BEAT; self.count[k] += 1
                elif kind == NOTE_OFF:
                    self.held[k].discard(a); self.off_t[k] = beat * BEAT
                elif kind == SET_PARAM:
                    self.vals[k][a] = v; self.changed.add(k)
                i += 1
            self.pos[k] = i

    def featured(self, t, last):
        # the most recent note of the first role that plays: a lead (held, or up to a beat after it), then the rest (held, or half a beat after)
        for role, hold in (('lead', 1.0), ('music', 0.5), ('bed', 0.0)):
            best, bt = None, -1e9
            for k, tr in enumerate(tracks):
                if tr['role'] != role: continue
                live = self.held[k] or t - self.off_t[k] < hold * BEAT
                if live and self.on_t[k] > bt: best, bt = k, self.on_t[k]
            if best is not None: return best
        return last

    def glow(self, k, t):
        flash = math.exp(-(t - self.on_t[k]) / 0.18) if t >= self.on_t[k] else 0.0
        return max(0.42 if self.held[k] else 0.0, flash)


OVERLAY_CSS = '''
html, body { background: #030405; }
#stage { position: absolute; left: 0; top: 0; width: 1080px; height: 1920px; overflow: hidden; color: #e9f3ef;
  background: radial-gradient(ellipse 80% 55% at 50% 38%, #0e1516 0, #070a0b 55%, #030405 100%); font-family: 'Barlow Condensed', sans-serif; }
#stage .kick { position: absolute; left: 64px; top: 70px; font: 700 22px 'Space Mono', monospace; letter-spacing: .3em; color: #8de0c3; opacity: .8; }
#stage .h1 { position: absolute; left: 64px; right: 64px; top: 118px; font: 400 50px Michroma, sans-serif; letter-spacing: .06em; line-height: 1.2; color: #f4fbf8; }
#stage .h2 { position: absolute; left: 64px; right: 64px; top: 270px; font: 700 34px 'Noto Sans CJK JP', sans-serif; letter-spacing: .04em; color: #cfe6dd; }
#win { position: absolute; left: 0; top: ''' + str(WIN_TOP) + '''px; width: 1080px; height: ''' + str(round(860 * SCALE)) + '''px; overflow: hidden;
  box-shadow: 10px 16px 50px rgba(0,0,0,.6); border-top: 1px solid rgba(141,224,195,.25); border-bottom: 1px solid rgba(141,224,195,.12); }
#win #sw { transform: scale(''' + str(SCALE) + ''') !important; transform-origin: 0 0; }
#now { position: absolute; left: 64px; right: 64px; top: 1150px; }
#now .lab { font: 700 20px 'Space Mono', monospace; letter-spacing: .32em; color: #8de0c3; opacity: .75; }
#now .name { font: 400 62px Michroma, sans-serif; letter-spacing: .08em; color: #f4fbf8; margin-top: 10px; white-space: nowrap; }
#now .src { font: 500 28px 'Barlow Condensed', sans-serif; letter-spacing: .24em; color: #9bb8ad; margin-top: 6px; }
#chips { position: absolute; left: 64px; right: 64px; top: 1370px; display: grid; grid-template-columns: repeat(3, 1fr); gap: 12px 14px; }
.chip { position: relative; height: 62px; border-radius: 10px; border: 1px solid rgba(141,224,195,.16); background: rgba(255,255,255,.03);
  padding: 0 16px; display: flex; align-items: center; justify-content: space-between; overflow: hidden; }
.chip .n { font: 600 27px 'Barlow Condensed', sans-serif; letter-spacing: .06em; color: #cfe2da; white-space: nowrap; }
.chip .c { font: 700 14px 'Space Mono', monospace; letter-spacing: .14em; color: #6f8c82; }
.chip .g { position: absolute; inset: 0; background: linear-gradient(135deg, rgba(141,224,195,.55), rgba(141,224,195,.12)); opacity: 0; }
.chip.cur { border-color: rgba(141,224,195,.75); }
#bar { position: absolute; left: 64px; right: 64px; top: 1760px; }
#bar .row { display: flex; justify-content: space-between; font: 700 20px 'Space Mono', monospace; letter-spacing: .2em; color: #8fb0a4; }
#bar .track { position: relative; height: 6px; margin-top: 16px; background: rgba(255,255,255,.08); border-radius: 3px; overflow: hidden; }
#bar .fill { position: absolute; left: 0; top: 0; bottom: 0; background: #8de0c3; }
#bar .marks { position: relative; height: 0; }
#end { position: absolute; inset: 0; opacity: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 34px;
  background: radial-gradient(ellipse 70% 45% at 50% 45%, #0e1516 0, #050708 60%, #030405 100%); }
#end img { width: 720px; }
#end .t { font: 400 30px Michroma, sans-serif; letter-spacing: .3em; color: #8de0c3; }
#end .j { font: 700 40px 'Noto Sans CJK JP', sans-serif; letter-spacing: .08em; color: #f4fbf8; }
#end .f { font: 700 22px 'Space Mono', monospace; letter-spacing: .22em; color: #9bb8ad; text-align: center; line-height: 1.9; }
#end .s { white-space: pre-line; line-height: 1.7; position: absolute; bottom: 110px; left: 64px; right: 64px; text-align: center; font: 500 24px 'Barlow Condensed', sans-serif; letter-spacing: .12em; color: #7d978e; }
'''

OVERLAY_JS = '''
(cfg) => {
  const el = (t, c, txt) => { const e = document.createElement(t); if (c) e.className = c; if (txt !== undefined) e.textContent = txt; return e; };
  const st = document.createElement('style'); st.textContent = cfg.css; document.head.appendChild(st);
  const stage = el('div'); stage.id = 'stage';
  stage.appendChild(el('div', 'kick', 'SEVENTHWELL  /  SW IN07'));
  stage.appendChild(el('div', 'h1', 'EVERY SOUND IN THIS TRACK IS SWINGBY.'));
  stage.appendChild(el('div', 'h2', 'この曲の音は、ドラムまでぜんぶ SWINGBY。'));
  const win = el('div'); win.id = 'win'; win.appendChild(document.getElementById('sw')); stage.appendChild(win);
  const now = el('div'); now.id = 'now';
  now.appendChild(el('div', 'lab', 'NOW PLAYING'));
  const nm = el('div', 'name', ''), src = el('div', 'src', '');
  now.appendChild(nm); now.appendChild(src); stage.appendChild(now);
  const chips = el('div'); chips.id = 'chips';
  const cs = cfg.chips.map(c => { const d = el('div', 'chip'); const g = el('div', 'g'); d.appendChild(g); d.appendChild(el('span', 'n', c.name)); d.appendChild(el('span', 'c', c.cat)); chips.appendChild(d); return { d, g }; });
  stage.appendChild(chips);
  const bar = el('div'); bar.id = 'bar';
  const row = el('div', 'row'); const left = el('span', '', ''), right = el('span', '', cfg.title.toUpperCase() + '  ·  ' + cfg.bpm + ' BPM');
  row.appendChild(left); row.appendChild(right); bar.appendChild(row);
  const tr = el('div', 'track'); const fill = el('div', 'fill'); tr.appendChild(fill); bar.appendChild(tr); stage.appendChild(bar);
  const end = el('div'); end.id = 'end';
  const logo = el('img'); logo.src = SW.url['logo-dark'] || ''; end.appendChild(logo);
  end.appendChild(el('div', 't', 'THE ORBITAL PRESET SYNTH'));
  end.appendChild(el('div', 'j', '選んで、弾いて、軌道に乗せる。'));
  end.appendChild(el('div', 'f', 'CLAP  ·  VST3  ·  AU\\nWINDOWS  ·  MACOS'));
  end.lastChild.style.whiteSpace = 'pre-line';
  end.appendChild(el('div', 's', cfg.credit));
  stage.appendChild(end);
  document.body.appendChild(stage);
  window.VID = {
    frame: f => {
      f.glow.forEach((v, i) => { cs[i].g.style.opacity = v.toFixed(3); cs[i].d.classList.toggle('cur', i === f.cur); });
      if (f.name !== undefined) { nm.textContent = f.name; src.textContent = f.src; }
      left.textContent = f.section; fill.style.width = (100 * f.progress).toFixed(2) + '%';
      end.style.opacity = f.end.toFixed(3);
    }
  };
}
'''


def main():
    chips = [{'name': tracks[k]['preset'], 'cat': tracks[k]['category']} for k in shown]
    credit = '"' + data['title'].upper() + '"  —  EVERY SOUND SWINGBY\nMASTERED WITH SW MS04 CLIPPER + SW MS01 MAXIMIZER'
    frames = work / 'frames'
    frames.mkdir(exist_ok=True)
    for f in frames.glob('*.png'):
        f.unlink()
    n = int(round(TOTAL * FPS))
    sel = None
    st = State()
    with sync_playwright() as pw:
        br = pw.chromium.launch(args=['--allow-file-access-from-files'])
        pg = br.new_page(viewport={'width': W, 'height': H}, device_scale_factor=1)
        errors = []
        pg.on('pageerror', lambda e: errors.append(str(e)))
        pg.clock.install(time=0)
        pg.goto(build_page(work, '60', 'dark').as_uri())
        pg.clock.run_for(1500)
        pg.wait_for_timeout(1500)
        pg.evaluate('document.fonts.ready')
        pg.clock.run_for(500)
        pg.evaluate("""() => {
          const mock = window.SWMOCK; window.SWMOCK = m => { if (m[0] === 'p' || m[0] === 'n' || m[0] === 'o') return; mock(m); };   // the video sends the host's info
          window.MOCK_CC = [-1, -1, -1, -1, -1, -1, -1, -1];
          window.MOCK_LIC = { state: 'licensed', id: 'L-20261010-0000demo', machine: window.MOCK_LIC.machine };
          SW.reply('licence', window.MOCK_LIC);
          SW.go('play');
        }""")
        pg.evaluate(OVERLAY_JS, {'css': OVERLAY_CSS, 'chips': chips, 'title': data['title'], 'bpm': round(data['bpm']), 'credit': credit})
        pg.clock.run_for(300)
        sec_names = data['sections']
        ks = range(n) if STILL is None else [int(round(STILL * FPS))]
        if STILL is not None:   # the state up to the frame before
            st.run_to(T0 + max(0, ks[0] - 1) / FPS)
        for k in ks:
            t = T0 + k / FPS
            st.run_to(t)
            cur = st.featured(t, sel)
            info = {'bpm': data['bpm'], 'playing': t < T1 + BEAT, 'beat': t / BEAT, 'learn': -1, 'cc': [-1] * 8}
            if cur is not None:
                info['note'] = st.count[cur]
                info['held'] = len(st.held[cur])
            if cur != sel or (cur is not None and cur in st.changed):
                tr = tracks[cur]
                vals = list(st.vals[cur])
                if tr['source'] == 'factory':
                    pg.evaluate('''([v, i, sel]) => { v[SW.P.idx('in07.preset')] = sel; SW.update(v); SW.reply('loaded', { kind: 'factory', index: i }); }''',
                                [vals, tr['factory'], tr['factory'] + 1])
                elif cur != sel:
                    pg.evaluate('''([v, name, cat]) => { SW.update(v); SW.reply('loaded', { kind: 'user', path: '/presets/' + name + '.swpreset', name, category: cat }); }''',
                                [vals, tr['preset'], tr['category']])
                else:
                    pg.evaluate('v => SW.update(v)', vals)
            st.changed.clear()
            pg.evaluate('i => SW.update([], i)', info)
            beat = t / BEAT
            section = next((s['name'] for s in sec_names if s['bar0'] * 4 <= beat < s['bar1'] * 4), '')
            f = {'glow': [st.glow(i, t) for i in shown], 'cur': shown.index(cur) if cur in shown else -1,
                 'section': section.upper() + '   BAR ' + str(int(beat // 4) + 1), 'progress': min(1.0, (t - T0) / (T1 - T0)),
                 'end': min(1.0, max(0.0, (t - T1) / 0.25))}
            if cur != sel and cur is not None:
                f['name'] = tracks[cur]['preset'].upper()
                f['src'] = tracks[cur]['category'] + '  ·  ' + SOURCE.get(tracks[cur]['source'], '')
            sel = cur
            pg.evaluate('f => VID.frame(f)', f)
            pg.clock.run_for(round((k + 1) * 1000 / FPS) - round(k * 1000 / FPS))
            path = frames / f'{k:05d}.png' if STILL is None else pathlib.Path(args[args.index('--still') + 2]).resolve()
            pg.screenshot(path=str(path))
            if STILL is None and k % 150 == 0:
                print(f'frame {k} / {n}', flush=True)
        br.close()
    if errors:
        sys.exit(f'page errors: {errors[:5]}')
    if STILL is not None:
        return
    # the sound: the master from the first bar to the last, a 20 ms fade in, two beats of the next bar fading out, then silence for the card
    fade_at, fade_len = T1 - T0, 2 * BEAT
    af = (f'atrim=start={T0:.6f}:end={T1 + fade_len:.6f},asetpts=PTS-STARTPTS,afade=t=in:d=0.02,'
          f'afade=t=out:st={fade_at:.6f}:d={fade_len:.6f}:curve=qsin,apad=whole_dur={TOTAL:.6f}')
    subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-framerate', str(FPS), '-i', str(frames / '%05d.png'), '-i', str(wav),
                    '-filter:a', af, '-c:v', 'libx264', '-preset', 'slow', '-crf', '19', '-pix_fmt', 'yuv420p', '-movflags', '+faststart',
                    '-c:a', 'aac', '-b:a', '192k', '-ar', '48000', '-t', f'{TOTAL:.3f}', str(out)], check=True)
    print(out, out.stat().st_size, 'bytes', f'{TOTAL:.2f} s')


main()
