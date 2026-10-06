#!/usr/bin/env python3
"""Mark a product done in docs/tasks.md and insert its README table row.
usage: tools/done.py DY01 "<README 中身>" "<README 注意>" """
import re, sys, pathlib
code, body, note = sys.argv[1:4]
root = pathlib.Path(__file__).resolve().parent.parent
t = root / "docs/tasks.md"; s = t.read_text()
s, k = re.subn(rf"- \[ \] ({code} )", r"- [x] \1", s)
assert k == 1, f"{code} not found open in tasks.md"
m = re.search(r"## 製品（済 (\d+)・残り (\d+)）", s)
done, left = int(m.group(1)) + 1, int(m.group(2)) - 1
s = s.replace(m.group(0), f"## 製品（済 {done}・残り {left}）")
t.write_text(s)
r = root / "README.md"; text = r.read_text()
name = re.search(rf"- \[x\] {code} (.*?) —", s).group(1).replace(" 進化版", "")
row = f"| SW {code} {name} | {body} | {note} |\n"
rows = [l for l in text.split("\n") if l.startswith("| SW ")]
def key(l): return re.match(r"\| SW ([A-Z]+)(\d+)", l).groups()
order = "EQ CS DY MS SA LO GT RV DL MD ST VO RS CR IN MT UT LV".split()
# README keeps CS first, then EQ, DY, MS, LV (existing order); insert before the first row that sorts after it
pri = lambda g: (["CS", "EQ", "SA", "LO", "GT", "RV", "DL", "MD", "ST", "VO", "RS", "CR", "IN", "MT", "UT", "DY", "MS", "LV"].index(g[0]), int(g[1]))
new = pri(re.match(r"([A-Z]+)(\d+)", code).groups())
after = None
for l in rows:
    if pri(key(l)) > new: after = l; break
if after: text = text.replace(after + "\n", row + after + "\n", 1)
else: text = text.replace(rows[-1] + "\n", rows[-1] + "\n" + row, 1)
r.write_text(text)
