// SWINGBY browser trial: the AudioWorklet that runs the engine (swingby.wasm, tools/in07_web/swingby_wasm.cpp). The page sends the
// WebAssembly bytes in processorOptions and then messages: set (a parameter, plain value), on / off (a note), preset (a factory preset as
// one patch), values (every parameter), wheel, bend, tempo, alloff. The engine renders 128 frames a call into its two buffers.
class SwingbyProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    const o = options.processorOptions || {};
    const stub = new Proxy({}, { get: () => () => 0 });   // the C library's few system calls (nothing is written anywhere)
    const inst = new WebAssembly.Instance(new WebAssembly.Module(o.bytes), { wasi_snapshot_preview1: stub });
    this.e = inst.exports;
    if (this.e._initialize) this.e._initialize();
    this.e.sw_init(sampleRate, 128);
    this.blocks = 0; this.peak = 0;
    this.port.onmessage = ev => this.message(ev.data);
    this.port.postMessage({ t: 'ready', params: this.e.sw_num_params() });
  }
  message(m) {
    const e = this.e;
    switch (m.t) {
      case 'set': e.sw_set(m.id, m.v); break;
      case 'on': e.sw_note_on(m.key, m.vel); break;
      case 'off': e.sw_note_off(m.key); break;
      case 'preset': e.sw_preset(m.index); break;
      case 'values': for (let i = 0; i < m.values.length; i++) if (i !== m.skip) e.sw_set(i, m.values[i]); break;
      case 'wheel': e.sw_wheel(m.v); break;
      case 'bend': e.sw_bend(m.v); break;
      case 'tempo': e.sw_tempo(m.bpm); break;
      case 'alloff': e.sw_all_off(); break;
    }
  }
  process(inputs, outputs) {
    const out = outputs[0];
    const n = out[0].length;
    for (let off = 0; off < n; off += 128) {
      const k = Math.min(128, n - off);
      this.e.sw_process(k);
      const mem = this.e.memory.buffer;
      const l = new Float32Array(mem, this.e.sw_left(), k), r = new Float32Array(mem, this.e.sw_right(), k);
      out[0].set(l, off);
      if (out[1]) out[1].set(r, off);
      for (let i = 0; i < k; i++) this.peak = Math.max(this.peak, Math.abs(l[i]));
    }
    if (++this.blocks % 94 === 0) { this.port.postMessage({ t: 'meter', peak: this.peak, notes: this.e.sw_notes() }); this.peak = 0; }   // about 4 a second
    return true;
  }
}
registerProcessor('swingby', SwingbyProcessor);
