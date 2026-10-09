// SW AUDIO core — the stream's own clock for jobs that run in steps (an IR designed or transformed, a kernel loaded a few partitions at a time: RV04, ST05, GT02). The host cuts the audio into blocks
// of any size, so "one step per process() call" makes the same job take 4 times as many samples with blocks of 64 as with 256 (and the new IR arrives at a different sample): here a job looks at
// the world only on a grid of the stream's absolute sample count (every kGrid = 64 samples since prepare()): process() cuts its block at the grid points, and a job starts, takes a step (every
// 4 grid points = 256 samples) or is committed at a grid point, never in the middle of a segment. What comes out is the same whatever the blocks (parameter events cut the audio at their sample first).
#pragma once

namespace sw {

struct GridClock {
    static constexpr int kGrid = 64, kStepTicks = 4;   // a grid point every 64 samples; a job step every 4 of them (256 samples)
    long pos = 0;                                      // samples since prepare()
    int toNext() const { return kGrid - static_cast<int>(pos % kGrid); }   // samples to the next grid point (1 .. kGrid)
    bool advance(int len) { pos += len; return pos % kGrid == 0; }          // true when that landed on a grid point
    void reset() { pos = 0; }
};

}  // namespace sw
