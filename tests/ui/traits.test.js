// The plug-in layer routes three parameters of a product to the common frame (sw::Shell): Output, In, Mix (ui/specs.json "traits": the parameter index or -1).
// Past mistakes this guards against: GT03's Input (a gain) routed to the frame's In switch (the whole board was bypassed at 0 dB).
// usage: node tests/ui/traits.test.js
const S = require('../../ui/specs.json');
let bad = 0;
for (const [code, t] of Object.entries(S.traits)) {
  const sp = S.specs[code];
  if (t.in >= 0 && sp[t.in].name !== 'In') { console.log(code, 'In is routed to', sp[t.in].name, '- only a parameter named In (an on/off switch) may be'); bad++; }
  if (t.in >= 0 && !(sp[t.in].steps && sp[t.in].steps.length === 2)) { console.log(code, 'In is not a 2-step parameter'); bad++; }
  if (t.output >= 0 && sp[t.output].unit !== 'dB') { console.log(code, 'Output is routed to', sp[t.output].name, '(' + sp[t.output].unit + ')'); bad++; }
  if (t.mix >= 0 && sp[t.mix].unit !== '%') { console.log(code, 'Mix is routed to', sp[t.mix].name, '(' + sp[t.mix].unit + ')'); bad++; }
  if (t.bypass !== (t.in < 0)) { console.log(code, 'bypass trait does not match the In routing'); bad++; }
}
if (bad) { console.log(bad, 'problem(s)'); process.exit(1); }
console.log(Object.keys(S.traits).length, 'products: In / Output / Mix routing is consistent');
