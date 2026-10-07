#!/usr/bin/env python3
"""ui/products.json: code, name, line, category colour, chassis, evo text — from docs/project/03_product_lineup.csv and the design tokens."""
import csv, json, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
tok = {c["name"]: c["value"] for c in json.load(open(root / "docs/project/02_design_tokens.json"))["color"]["tokens"]}
cat = {"EQ": "eq", "Dynamics・Mastering": "dyn", "Saturation": "sat", "Space・Time": "space", "Vocal": "vocal", "Restore": "restore", "Meter": "meter", "Creative・Instrument": "creative", "Broadcast": "broadcast", "—": "broadcast"}
cat["EQ"] = "eq"
acts = json.load(open(root / "ui/actions.json"))
out = {}
for r in csv.DictReader(open(root / "docs/project/03_product_lineup.csv")):
    k = cat.get(r["category"], "meter")
    out[r["code"]] = {"code": r["code"], "name": r["name"], "line": r["line"], "category": r["category"], "chassis": r["chassis"], "evo": r["evo"],
                      "actions": acts.get(r["code"], []), "acc": tok["cat-" + k], "hi": tok["cat-" + k + "-hi"], "ear": tok["cat-" + k + "-ear"], "ring": tok["cat-" + k + "-ring"]}
(root / "ui/products.json").write_text(json.dumps(out, ensure_ascii=False, separators=(",", ":")))
print(len(out), "products")
