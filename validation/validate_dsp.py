#!/usr/bin/env python3
"""
Particula Dub - reference DSP validation (NumPy only).

Re-implements the plugin's signal path in Python and compares measurements
against analytical predictions. This is a *reference model*, not the C++ binary.

Run:  python3 validate_dsp.py     ->  prints a summary and writes results.json
"""
import json
import math
import pathlib
import numpy as np

FS = 44100


# ---------------------------------------------------------------- oscillator
def polyblep(t, dt):
    out = np.zeros_like(t)
    m1 = t < dt
    x = t[m1] / dt
    out[m1] = x + x - x * x - 1.0
    m2 = t > 1.0 - dt
    x = (t[m2] - 1.0) / dt
    out[m2] = x * x + x + x + 1.0
    return out


def saw(f0, n, blep=True):
    inc = f0 / FS
    ph = (inc * np.arange(n)) % 1.0
    s = 2.0 * ph - 1.0
    return s - polyblep(ph, inc) if blep else s


def inharmonic_db(x, f0, guard=6):
    """Energy outside +-guard bins of the true harmonics, relative to the energy inside (dB)."""
    n = len(x)
    X = np.abs(np.fft.rfft(x * np.kaiser(n, 14))) ** 2
    df = FS / n
    mask = np.zeros(len(X), bool)
    k = 1
    while k * f0 < FS / 2:
        c = int(round(k * f0 / df))
        mask[max(0, c - guard):c + guard + 1] = True
        k += 1
    return 10 * math.log10(X[~mask].sum() / X[mask].sum())


def exp_aliasing():
    n = 2 ** 16
    res = {}
    for f0 in (220.0, 880.0, 1760.0):
        res[str(f0)] = {
            "naive_db": round(inharmonic_db(saw(f0, n, False), f0), 1),
            "polyblep_db": round(inharmonic_db(saw(f0, n, True), f0), 1),
        }
    return res


# ------------------------------------------------------------------ detune
def beat_measure(f0, cents, n=2 ** 20):
    ratio = 2 ** (cents / 1200)
    x = 0.5 * (saw(f0, n) + saw(f0 * ratio, n))
    X = np.abs(np.fft.rfft(x * np.kaiser(n, 14)))
    df = FS / n
    lo, hi = int((f0 - 6) / df), int((f0 * ratio + 6) / df)
    seg = X[lo:hi]
    pk = [i for i in range(1, len(seg) - 1) if seg[i] > seg[i - 1] and seg[i] >= seg[i + 1]]
    pk = sorted(pk, key=lambda i: seg[i], reverse=True)[:2]

    def interp(i):
        a, b, c = np.log(seg[i - 1]), np.log(seg[i]), np.log(seg[i + 1])
        return i + 0.5 * (a - c) / (a - 2 * b + c)

    f = sorted((lo + interp(i)) * df for i in pk)
    return f[1] - f[0], f0 * (ratio - 1)


def exp_detune():
    out = {}
    for f0 in (110.0, 146.83, 220.0):
        m, p = beat_measure(f0, 12.0)
        out[str(f0)] = {"predicted_hz": round(p, 3), "measured_hz": round(m, 3)}
    return out


# ------------------------------------------------------------------- delay
def simulate_delay(T, fb, tone, a, secs):
    c = 1 - math.exp(-2 * math.pi * tone / FS)
    D = int(round(T * FS))
    n = int(secs * FS)
    buf = [0.0] * D
    out = np.zeros(n)
    st = 0.0
    tanh = math.tanh
    for i in range(n):
        j = i % D
        wet = buf[j]
        st += c * (wet - st)
        buf[j] = tanh((a if i == 0 else 0.0) + st * fb)
        out[i] = wet
    return out, c, D


def exp_delay():
    T, fb, tone = 0.375, 0.65, 2200.0   # 1/8. at 120 BPM
    out, c, D = simulate_delay(T, fb, tone, 0.01, 10.0)   # small amplitude -> linear regime
    k_max = len(out) // D - 1
    win = [out[k * D:(k + 1) * D] for k in range(1, k_max + 1)]
    area = [w.sum() for w in win]
    ratios = [area[i + 1] / area[i] for i in range(6)]

    w_c = 2 * math.pi * tone / FS
    H = c / abs(1 - (1 - c) * np.exp(-1j * w_c))
    n_idx = np.arange(D)
    mags = [abs(np.sum(w * np.exp(-1j * w_c * n_idx))) for w in win[:5]]
    hf = [mags[i + 1] / mags[i] for i in range(4)]

    first_below = next(i + 1 for i, a_ in enumerate(area) if abs(a_) < 1e-3 * abs(area[0]))

    big, _, _ = simulate_delay(T, 0.95, tone, 1.0, 30.0)   # worst case: fb=0.95, full-scale input
    return {
        "delay_s": T, "feedback": fb, "tone_hz": tone, "one_pole_c": round(c, 4),
        "area_ratio_predicted": fb,
        "area_ratio_measured": [round(r, 4) for r in ratios],
        "hf_ratio_at_tone_predicted": round(fb * float(H), 4),
        "hf_ratio_at_tone_measured": [round(r, 4) for r in hf],
        "per_repeat_db_at_tone_predicted": round(20 * math.log10(fb * float(H)), 2),
        "echoes_to_minus60db_predicted": round(1 + math.log(1e-3) / math.log(fb), 2),
        "echo_index_first_below_minus60db_measured": first_below,
        "stability_fb095_fullscale_max_abs": round(float(np.max(np.abs(big))), 4),
        "stability_fb095_peak_last_5s": round(float(np.max(np.abs(big[-5 * FS:]))), 4),
    }


# ------------------------------------------------------------------ filter
def svf_gain_at_fc(fc, Q):
    n = 2 ** 16
    g = math.tan(math.pi * fc / FS)
    k = 1.0 / Q
    a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2
    ic1 = ic2 = 0.0
    y = np.zeros(n)
    for i in range(n):
        v3 = (1.0 if i == 0 else 0.0) - ic2
        v1 = a1 * ic1 + a2 * v3
        v2 = ic2 + a2 * ic1 + a3 * v3
        ic1 = 2 * v1 - ic1
        ic2 = 2 * v2 - ic2
        y[i] = v2
    w = 2 * math.pi * fc / FS
    return abs(np.sum(y * np.exp(-1j * w * np.arange(n))))


def exp_filter():
    res = {}
    for q in (0.707, 1.2, 3.0, 6.0):
        h = svf_gain_at_fc(700.0, q)
        res[str(q)] = {"measured_gain": round(h, 4), "measured_db": round(20 * math.log10(h), 2),
                       "predicted_db": round(20 * math.log10(q), 2)}
    cutoff, amt, fdec = 700.0, 2.5, 0.35
    peak = cutoff * 2 ** amt
    t1k = fdec * (1 - math.log2(1000 / cutoff) / amt)
    return {"resonance_gain_at_fc": res,
            "env_peak_hz": round(peak, 1),
            "env_sweep_rate_oct_per_s": round(amt / fdec, 2),
            "env_time_to_pass_1khz_s": round(t1k, 3)}


# ------------------------------------------------------------------ chords
def sethares(freqs, amps):
    d = 0.0
    for i in range(len(freqs)):
        for j in range(i + 1, len(freqs)):
            fmin = min(freqs[i], freqs[j]); df = abs(freqs[i] - freqs[j])
            s = 0.24 / (0.0207 * fmin + 18.96)
            d += amps[i] * amps[j] * (math.exp(-3.5 * s * df) - math.exp(-5.75 * s * df))
    return d


def partials(m, nh=8):
    f0 = 440 * 2 ** ((m - 69) / 12)
    return [f0 * h for h in range(1, nh + 1)], [1.0 / h for h in range(1, nh + 1)]


def exp_chords(root=50):
    sets = {"Off": [0], "m7": [0, 3, 7, 10], "m9": [0, 3, 10, 14],
            "m11": [0, 3, 7, 10, 17], "sus m7": [0, 5, 7, 10]}
    out = {}
    for name, iv in sets.items():
        F, A, single = [], [], 0.0
        for i in iv:
            f, a = partials(root + i)
            single += sethares(f, a)
            F += f; A += a
        out[name] = {"intervals": iv,
                     "freqs_hz": [round(440 * 2 ** ((root + i - 69) / 12), 2) for i in iv],
                     "interaction_roughness": round(sethares(F, A) - single, 3)}
    return out


if __name__ == "__main__":
    results = {"aliasing_inharmonic_db": exp_aliasing(), "detune_beating": exp_detune(),
               "dub_delay": exp_delay(), "filter": exp_filter(), "chords_D3": exp_chords()}
    path = pathlib.Path(__file__).with_name("results.json")
    path.write_text(json.dumps(results, indent=2))
    print(json.dumps(results, indent=2))
