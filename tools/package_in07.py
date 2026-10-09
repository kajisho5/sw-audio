#!/usr/bin/env python3
"""SWINGBY (SW IN07): the folder a customer downloads — the plug-ins, the manuals (Japanese, English; HTML made from docs/dist/in07/*.md),
a short read-me, and the licences of everything inside (the fonts' OFL, the SDKs' licences found in the build tree, NOTICE.md).
The caller zips it (macOS: ditto, so bundles and signatures survive; Windows: any zip).
usage: tools/package_in07.py <build dir> <out dir> <windows-x64|macos-universal|linux-x64> <version>
Needs the Python package "markdown"."""
import pathlib, shutil, sys

import markdown

REPO = pathlib.Path(__file__).resolve().parents[1]
build, out, plat, version = pathlib.Path(sys.argv[1]).resolve(), pathlib.Path(sys.argv[2]).resolve(), sys.argv[3], sys.argv[4]
name = f'SWINGBY-{version}-{plat}'
dst = out / name
if dst.exists():
    shutil.rmtree(dst)
dst.mkdir(parents=True)

# ---- the plug-ins (bundles copied whole, links kept: a signed macOS bundle must stay as it was signed)
exts = ['.vst3', '.clap'] + (['.component'] if plat.startswith('macos') else [])
found = []
where = build / 'plugins' if (build / 'plugins').is_dir() else build   # CMake's plug-in output (Windows: plugins/CLAP/Release, plugins/VST3/...)
for e in exts:
    for p in sorted(where.rglob('SW IN07 SWINGBY' + e), key=lambda x: len(x.parts)):   # the outermost first (a VST3 bundle holds a file of the same name)
        if 'cpm' in p.parts or '_deps' in p.parts or p in found or any(q in p.parents for q in found):
            continue
        if p.is_dir():
            shutil.copytree(p, dst / p.name, symlinks=True)
        else:
            shutil.copy2(p, dst / p.name)
        found.append(p)
        break
if not any(p.suffix == '.vst3' for p in found) or not any(p.suffix == '.clap' for p in found):
    sys.exit(f'the plug-ins were not found under {build}: {found}')

# ---- the manuals
CSS = """body{font:16px/1.7 -apple-system,'Segoe UI','Hiragino Sans','Yu Gothic UI',Meiryo,sans-serif;max-width:860px;margin:40px auto;padding:0 20px;color:#1b2622;background:#fff}
h1{font-size:28px;letter-spacing:.04em}h2{margin-top:2.2em;border-bottom:1px solid #cfe3dc;padding-bottom:4px}h3{margin-top:1.6em}
table{border-collapse:collapse;margin:12px 0;width:100%}td,th{border:1px solid #d5e2dd;padding:6px 10px;vertical-align:top;text-align:left}th{background:#f1f6f4}
code{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:.9em;background:#f1f6f4;padding:1px 4px;border-radius:3px}hr{border:0;border-top:1px solid #d5e2dd}
@media (prefers-color-scheme:dark){body{background:#0c1112;color:#e8e8e8}th{background:#141c1d}td,th{border-color:#2a3432}code{background:#141c1d}h2{border-color:#2a3432}}"""
for src, title, fname in [('manual_ja.md', 'SWINGBY 取扱説明書', 'SWINGBY マニュアル（日本語）.html'), ('manual_en.md', 'SWINGBY User Guide', 'SWINGBY Manual (English).html')]:
    text = (REPO / 'docs/dist/in07' / src).read_text(encoding='utf-8').replace('〔VERSION〕', version)
    body = markdown.markdown(text, extensions=['tables'])
    lang = 'ja' if src.endswith('_ja.md') else 'en'
    (dst / fname).write_text(f'<!doctype html><html lang="{lang}"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
                             f'<title>{title}</title><style>{CSS}</style></head><body>{body}</body></html>', encoding='utf-8')

# ---- read-me
where = {
    'windows-x64': 'VST3: C:\\Program Files\\Common Files\\VST3\\   CLAP: C:\\Program Files\\Common Files\\CLAP\\',
    'macos-universal': 'AU: /Library/Audio/Plug-Ins/Components/   VST3: /Library/Audio/Plug-Ins/VST3/   CLAP: /Library/Audio/Plug-Ins/CLAP/',
    'linux-x64': 'VST3: ~/.vst3/   CLAP: ~/.clap/',
}.get(plat, '')
(dst / 'README.txt').write_text(f"""SW IN07 SWINGBY {version} ({plat})  -  SEVENTHWELL

[日本語]
使う形式のファイルを次のフォルダにコピーして、DAW を起動し直してください。
{where}
くわしくは「SWINGBY マニュアル（日本語）.html」。ライセンスがない間は体験版として動きます
（起動から 30 秒後、そのあと 60 秒ごとに 3 秒の無音）。有効化はプラグインの画面右上のライセンスの表示から。

[English]
Copy the format you use to the folder below and restart your DAW.
{where}
See "SWINGBY Manual (English).html". Without a licence it runs as the trial (3 seconds of silence
30 seconds after start and every 60 seconds after that). Activate from the licence chip at the top right of the window.

Third-party licences: the "licenses" folder.
""", encoding='utf-8', newline='\r\n' if plat.startswith('windows') else '\n')

# ---- licences
lic = dst / 'licenses'
lic.mkdir()
shutil.copy2(REPO / 'NOTICE.md', lic / 'NOTICE.md')
for f in sorted((REPO / 'ui/in07/fonts').glob('OFL-*.txt')):
    shutil.copy2(f, lic / f.name)
shutil.copy2(REPO / 'core/third_party/monocypher/LICENCE.md', lic / 'Monocypher-LICENCE.md')
wanted = {'clap-src/LICENSE': 'CLAP-SDK-LICENSE.txt', 'clap-wrapper-src/LICENSE': 'clap-wrapper-LICENSE.txt', 'vst3sdk/LICENSE.txt': 'VST3-SDK-LICENSE.txt'}
if plat.startswith('macos'):
    wanted['AudioUnitSDK/LICENSE.txt'] = 'AudioUnitSDK-LICENSE.txt'
if plat.startswith('windows'):
    wanted['webview2-src/LICENSE.txt'] = 'WebView2-SDK-LICENSE.txt'
missing = []
for rel, target in wanted.items():
    hits = [p for p in build.rglob(pathlib.Path(rel).name) if str(p).replace('\\', '/').endswith(rel)]
    if not hits and rel.startswith('AudioUnitSDK'):
        hits = [p for p in build.rglob('LICENSE*') if 'audiounitsdk' in str(p).lower()]
    if hits:
        shutil.copy2(hits[0], lic / target)
    else:
        missing.append(rel)
if missing:
    sys.exit(f'licence files not found in the build tree: {missing}')
print(dst)
