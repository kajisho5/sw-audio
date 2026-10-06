// SW AUDIO core — autoregressive (AR) modelling helpers for repair: Levinson-Durbin fit and least-squares interpolation of a run of missing samples (Janssen / Godsill-Rayner).
//   prediction x[n] ~ sum_{k=1..p} a[k] x[n-k]; the excitation e[n] = x[n] - sum a[k] x[n-k]. The missing samples u (m of them) minimise sum e[n]^2 over the m + p samples they touch:
//   the normal equations are a banded symmetric positive definite Toeplitz system (band p) built from the autocorrelation of c = (1, -a1 .. -ap), solved by a banded Cholesky factorisation.
// No allocation when the caller hands in preallocated workspaces (arInterpolate takes them).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace sw {

// fit an order-p AR model to x[0..n) (Hann window applied inside, `win` is scratch of n doubles); a[1..p] (a[0] unused); returns the prediction error power (per windowed sample), 0 for silence
inline double arFit(const double* x, int n, int p, double* a, double* win, double* r /* p+1 */) {
    for (int i = 0; i < n; ++i) win[i] = x[i] * (0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * (i + 0.5) / n));
    for (int l = 0; l <= p; ++l) { double s = 0; for (int i = l; i < n; ++i) s += win[i] * win[i - l]; r[l] = s; }
    for (int k = 0; k <= p; ++k) a[k] = 0.0;
    if (r[0] < 1e-18) return 0.0;
    r[0] *= 1.0 + 1e-9;   // tiny white-noise regularisation (keeps the recursion stable for pure tones)
    double err = r[0];
    double tmp[128];
    for (int i = 1; i <= p; ++i) {
        double acc = r[i]; for (int j = 1; j < i; ++j) acc -= a[j] * r[i - j];
        const double k = acc / err;
        for (int j = 1; j < i; ++j) tmp[j] = a[j] - k * a[i - j];
        for (int j = 1; j < i; ++j) a[j] = tmp[j];
        a[i] = k; err *= (1.0 - k * k);
        if (err < 1e-18 * r[0]) { err = 1e-18 * r[0]; }
    }
    return err / n;
}

// the excitation at n (needs x[n-p .. n])
inline double arResidual(const double* x, int n, const double* a, int p) { double e = x[n]; for (int k = 1; k <= p; ++k) e -= a[k] * x[n - k]; return e; }

struct ArWork { std::vector<double> c, rc, L, rhs, b; };
inline void arWorkPrepare(ArWork& w, int maxM, int p) {
    w.c.assign(static_cast<size_t>(p + 1), 0.0); w.rc.assign(static_cast<size_t>(p + 1), 0.0);
    w.L.assign(static_cast<size_t>(maxM) * static_cast<size_t>(p + 1), 0.0); w.rhs.assign(static_cast<size_t>(maxM), 0.0); w.b.assign(static_cast<size_t>(maxM + p + 1), 0.0);
}

// x[0 .. s+m+p) holds the signal with x[s .. s+m) to be replaced (their current values are ignored); needs s >= p. Returns false when the system is singular (x untouched).
inline bool arInterpolate(double* x, int s, int m, const double* a, int p, ArWork& w) {
    if (m <= 0 || s < p) return false;
    w.c[0] = 1.0; for (int k = 1; k <= p; ++k) w.c[static_cast<size_t>(k)] = -a[k];
    for (int l = 0; l <= p; ++l) { double v = 0; for (int k = 0; k + l <= p; ++k) v += w.c[static_cast<size_t>(k)] * w.c[static_cast<size_t>(k + l)]; w.rc[static_cast<size_t>(l)] = v; }
    // b[j] = e[s + j] with the unknowns set to zero, j = 0 .. m+p-1
    for (int i = 0; i < m; ++i) x[s + i] = 0.0;
    for (int j = 0; j < m + p; ++j) w.b[static_cast<size_t>(j)] = arResidual(x, s + j, a, p);
    for (int i = 0; i < m; ++i) { double v = 0; for (int k = 0; k <= p; ++k) v += w.c[static_cast<size_t>(k)] * w.b[static_cast<size_t>(i + k)]; w.rhs[static_cast<size_t>(i)] = -v; }
    // banded Cholesky of R (R[i][j] = rc[|i-j|] for |i-j| <= p): L[i][d] = L(i, i-d), d = 0..p
    const size_t bw = static_cast<size_t>(p + 1);
    for (int i = 0; i < m; ++i) {
        for (int d = std::min(p, i); d >= 0; --d) {
            const int j = i - d;
            double v = d <= p ? w.rc[static_cast<size_t>(d)] : 0.0;
            for (int k = std::max(0, i - p); k < j; ++k) v -= w.L[static_cast<size_t>(i) * bw + static_cast<size_t>(i - k)] * w.L[static_cast<size_t>(j) * bw + static_cast<size_t>(j - k)];
            if (d == 0) { if (v <= 1e-12) return false; w.L[static_cast<size_t>(i) * bw] = std::sqrt(v); }
            else w.L[static_cast<size_t>(i) * bw + static_cast<size_t>(d)] = v / w.L[static_cast<size_t>(j) * bw];
        }
    }
    // forward: L y = rhs ; backward: L^T u = y
    for (int i = 0; i < m; ++i) { double v = w.rhs[static_cast<size_t>(i)]; for (int k = std::max(0, i - p); k < i; ++k) v -= w.L[static_cast<size_t>(i) * bw + static_cast<size_t>(i - k)] * w.rhs[static_cast<size_t>(k)]; w.rhs[static_cast<size_t>(i)] = v / w.L[static_cast<size_t>(i) * bw]; }
    for (int i = m - 1; i >= 0; --i) { double v = w.rhs[static_cast<size_t>(i)]; for (int k = i + 1; k < std::min(m, i + p + 1); ++k) v -= w.L[static_cast<size_t>(k) * bw + static_cast<size_t>(k - i)] * w.rhs[static_cast<size_t>(k)]; w.rhs[static_cast<size_t>(i)] = v / w.L[static_cast<size_t>(i) * bw]; }
    for (int i = 0; i < m; ++i) x[s + i] = w.rhs[static_cast<size_t>(i)];
    return true;
}

}  // namespace sw
