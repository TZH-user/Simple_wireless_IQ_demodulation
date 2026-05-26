import csv
import math
import os
from dataclasses import dataclass
from typing import Dict, List

import numpy as np


@dataclass
class Config:
    sample_rate_hz: float = 2.048e6
    block_n: int = 4096
    decim: int = 32
    baseline_blocks: int = 4
    trigger_blocks: int = 2
    low_if_delta_hz: float = 1700.0
    power_delta_pm: float = 350.0
    env_cv_delta_pm: float = 300.0
    phase_delta_pm: float = 260.0
    duration_s: float = 0.052
    change_time_s: float = 0.024
    valid_latency_s: float = 0.020
    target_low_if_hz: float = -5e3
    adc_bits: int = 14
    adc_target_peak_v: float = 0.80
    rng_seed: int = 20260527


CFG = Config()
OUTPUT_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "runtime_mod_switch_focused_py_output_20260527",
)


def build_profiles() -> List[Dict]:
    return [
        dict(name="ideal", freq_error_hz=0.0, drift_hz=0.0, iq_gain_err=0.0,
             iq_phase_err_deg=0.0, image_leakage_db=-np.inf, dc_offset=0.0,
             ripple_depth=0.0),
        dict(name="board_like_normal", freq_error_hz=1100.0, drift_hz=60.0,
             iq_gain_err=0.025, iq_phase_err_deg=2.0, image_leakage_db=-24.0,
             dc_offset=0.010, ripple_depth=0.012),
        dict(name="board_like_hard", freq_error_hz=4200.0, drift_hz=180.0,
             iq_gain_err=0.060, iq_phase_err_deg=5.0, image_leakage_db=-15.0,
             dc_offset=0.025, ripple_depth=0.025),
    ]


def build_cases() -> List[Dict]:
    mods = ["AM", "FM", "ASK", "FSK", "PSK"]
    snrs = [20.0, 10.0]
    freq_steps = [0.0, 10e3, -10e3, 20e3, 50e3]
    cases = []
    case_id = 0
    for profile in build_profiles():
        for snr_db in snrs:
            for mod in mods:
                case_id += 1
                cases.append(make_case(case_id, "no_change", profile, snr_db, mod, mod, 0.0))
            for mod_a in mods:
                for mod_b in mods:
                    if mod_a == mod_b:
                        continue
                    for freq_step_hz in freq_steps:
                        case_id += 1
                        scenario = "mod_switch_same_freq" if freq_step_hz == 0 else "freq_mod_switch"
                        cases.append(make_case(case_id, scenario, profile, snr_db, mod_a, mod_b, freq_step_hz))
    return cases


def make_case(case_id, scenario, profile, snr_db, mod_a, mod_b, freq_step_hz):
    low_if_a = CFG.target_low_if_hz + profile["freq_error_hz"]
    return dict(
        case_id=case_id,
        scenario=scenario,
        has_change=scenario != "no_change",
        profile=profile,
        profile_name=profile["name"],
        snr_db=snr_db,
        mod_a=mod_a,
        mod_b=mod_b,
        freq_step_hz=freq_step_hz,
        low_if_a_hz=low_if_a,
        low_if_b_hz=low_if_a + freq_step_hz,
        seed=910000 + case_id * 31,
    )


def make_bits(n, fs, symbol_rate_hz):
    samples_per_symbol = max(1, int(round(fs / symbol_rate_hz)))
    symbol_count = int(math.ceil(n / samples_per_symbol)) + 2
    reg = np.ones(9, dtype=np.uint8)
    b = np.zeros(symbol_count, dtype=np.float64)
    for k in range(symbol_count):
        b[k] = reg[-1]
        new_bit = reg[4] ^ reg[8]
        reg[1:] = reg[:-1]
        reg[0] = new_bit
    bits = np.repeat(b, samples_per_symbol)
    return bits[:n]


def synth_mod(modulation, low_if_hz, t):
    if modulation == "AM":
        env = 1.0 + 0.50 * np.sin(2 * np.pi * 10e3 * t)
        return env * np.exp(1j * 2 * np.pi * low_if_hz * t)
    if modulation == "FM":
        msg = np.sin(2 * np.pi * 10e3 * t)
        inst_freq = low_if_hz + 75e3 * msg
        phase = 2 * np.pi * np.cumsum(inst_freq) / CFG.sample_rate_hz
        return np.exp(1j * phase)
    if modulation == "ASK":
        bits = make_bits(len(t), CFG.sample_rate_hz, 10e3)
        env = 0.05 + 0.95 * bits
        return env * np.exp(1j * 2 * np.pi * low_if_hz * t)
    if modulation == "FSK":
        bits = make_bits(len(t), CFG.sample_rate_hz, 10e3)
        inst_freq = low_if_hz + (2 * bits - 1) * 10e3
        phase = 2 * np.pi * np.cumsum(inst_freq) / CFG.sample_rate_hz
        return np.exp(1j * phase)
    if modulation == "PSK":
        bits = make_bits(len(t), CFG.sample_rate_hz, 10e3)
        symbols = 2 * bits - 1
        return symbols * np.exp(1j * 2 * np.pi * low_if_hz * t)
    raise ValueError(modulation)


def apply_frontend(iq, t, profile):
    if np.isfinite(profile["image_leakage_db"]):
        leak = 10 ** (profile["image_leakage_db"] / 20.0)
        iq = iq + leak * np.conj(iq)
    i = np.real(iq) * (1 + profile["iq_gain_err"])
    q = np.imag(iq) * (1 - profile["iq_gain_err"])
    ph = np.deg2rad(profile["iq_phase_err_deg"])
    iq = i + 1j * (q * np.cos(ph) + i * np.sin(ph))
    if profile["ripple_depth"] != 0:
        iq = iq * (1 + profile["ripple_depth"] * np.sin(2 * np.pi * 1200 * t))
    iq = iq + profile["dc_offset"] * (1 + 0.7j)
    return iq


def add_awgn(iq, snr_db, rng):
    sig_power = np.mean(np.abs(iq) ** 2)
    noise_power = sig_power / (10 ** (snr_db / 10.0))
    noise = np.sqrt(noise_power / 2) * (rng.standard_normal(iq.shape) + 1j * rng.standard_normal(iq.shape))
    return iq + noise


def quantize_adc(iq):
    max_code = 2 ** CFG.adc_bits - 1
    center = 2 ** (CFG.adc_bits - 1)
    peak = np.max(np.abs(np.concatenate([np.real(iq), np.imag(iq)])))
    if peak <= 0:
        peak = 1.0
    scale = CFG.adc_target_peak_v * center / peak
    i_adc = np.rint(center + np.real(iq) * scale)
    q_adc = np.rint(center + np.imag(iq) * scale)
    return np.clip(i_adc, 0, max_code).astype(np.int32), np.clip(q_adc, 0, max_code).astype(np.int32)


def synth_case(case_cfg):
    rng = np.random.default_rng(case_cfg["seed"])
    n = int(round(CFG.duration_s * CFG.sample_rate_hz))
    t = np.arange(n, dtype=np.float64) / CFG.sample_rate_hz
    change_index = int(round(CFG.change_time_s * CFG.sample_rate_hz))
    iq_a = synth_mod(case_cfg["mod_a"], case_cfg["low_if_a_hz"], t)
    if case_cfg["has_change"]:
        iq_b = synth_mod(case_cfg["mod_b"], case_cfg["low_if_b_hz"], t)
        iq = iq_a.copy()
        iq[change_index:] = iq_b[change_index:]
    else:
        iq = iq_a
    drift_phase = 2 * np.pi * (case_cfg["profile"]["drift_hz"] / CFG.duration_s) * (t ** 2) / 2.0
    iq = iq * np.exp(1j * drift_phase)
    iq = apply_frontend(iq, t, case_cfg["profile"])
    iq = add_awgn(iq, case_cfg["snr_db"], rng)
    return (*quantize_adc(iq), change_index / CFG.sample_rate_hz)


def extract_features(i_block, q_block):
    center = 2 ** (CFG.adc_bits - 1)
    short_lag = CFG.decim
    long_lag = CFG.decim * 4
    idx = np.arange(long_lag, len(i_block), CFG.decim)
    if len(idx) == 0:
        return dict(mean_power=0, env_cv_pm=0, short_phase_disp_pm=0, long_phase_disp_pm=0, low_if_hz=0)
    i0 = i_block[idx].astype(np.float64) - center
    q0 = q_block[idx].astype(np.float64) - center
    is_ = i_block[idx - short_lag].astype(np.float64) - center
    qs = q_block[idx - short_lag].astype(np.float64) - center
    il = i_block[idx - long_lag].astype(np.float64) - center
    ql = q_block[idx - long_lag].astype(np.float64) - center
    power = i0 * i0 + q0 * q0
    power_sum = np.sum(power)
    if power_sum <= 0:
        return dict(mean_power=0, env_cv_pm=0, short_phase_disp_pm=0, long_phase_disp_pm=0, low_if_hz=0)
    mean_power = power_sum / len(idx)
    var_power = max(0.0, np.mean(power * power) - mean_power * mean_power)
    if_re = np.sum(i0 * is_ + q0 * qs)
    if_im = np.sum(q0 * is_ - i0 * qs)
    short_re = if_re
    short_im = if_im
    long_re = np.sum(i0 * il + q0 * ql)
    long_im = np.sum(q0 * il - i0 * ql)
    phase = math.atan2(if_im, if_re) if if_re != 0 or if_im != 0 else 0.0
    low_if_hz = phase * CFG.sample_rate_hz / (2 * math.pi * short_lag)
    short_coh = abs(short_re) + abs(short_im)
    long_coh = abs(long_re) + abs(long_im)
    return dict(
        mean_power=math.floor(mean_power),
        env_cv_pm=math.floor(var_power * 1000.0 / (mean_power * mean_power)) if mean_power > 0 else 0,
        short_phase_disp_pm=max(0, math.floor((power_sum - min(short_coh, power_sum)) * 1000.0 / power_sum)),
        long_phase_disp_pm=max(0, math.floor((power_sum - min(long_coh, power_sum)) * 1000.0 / power_sum)),
        low_if_hz=round(low_if_hz),
    )


def average_feature_like_c(a, b):
    return dict(
        mean_power=(a["mean_power"] + b["mean_power"]) // 2,
        env_cv_pm=(a["env_cv_pm"] + b["env_cv_pm"]) // 2,
        short_phase_disp_pm=(a["short_phase_disp_pm"] + b["short_phase_disp_pm"]) // 2,
        long_phase_disp_pm=(a["long_phase_disp_pm"] + b["long_phase_disp_pm"]) // 2,
        low_if_hz=round((a["low_if_hz"] + b["low_if_hz"]) / 2),
    )


def score_features(now, baseline, initial_mode):
    score = 0
    reason = 0
    power_delta_pm = abs(now["mean_power"] - baseline["mean_power"]) * 1000.0 / baseline["mean_power"] if baseline["mean_power"] else 0
    env_delta_pm = abs(now["env_cv_pm"] - baseline["env_cv_pm"])
    short_delta_pm = abs(now["short_phase_disp_pm"] - baseline["short_phase_disp_pm"])
    long_delta_pm = abs(now["long_phase_disp_pm"] - baseline["long_phase_disp_pm"])
    low_if_delta_hz = abs(now["low_if_hz"] - baseline["low_if_hz"])
    if initial_mode in ("AM", "ASK", "PSK") and low_if_delta_hz >= CFG.low_if_delta_hz:
        score += 2
        reason |= 0x01
    if power_delta_pm >= CFG.power_delta_pm:
        score += 1
        reason |= 0x02
    if env_delta_pm >= CFG.env_cv_delta_pm:
        score += 1
        reason |= 0x04
    if short_delta_pm >= CFG.phase_delta_pm:
        score += 1
        reason |= 0x08
    if long_delta_pm >= CFG.phase_delta_pm:
        score += 1
        reason |= 0x10
    return score, reason


def run_monitor(i_adc, q_adc, initial_mode):
    baseline = None
    baseline_blocks = 0
    baseline_valid = False
    suspect_blocks = 0
    block_count = len(i_adc) // CFG.block_n
    for block_idx in range(block_count):
        start = block_idx * CFG.block_n
        stop = start + CFG.block_n
        now = extract_features(i_adc[start:stop], q_adc[start:stop])
        if not baseline_valid:
            baseline = now if baseline is None else average_feature_like_c(baseline, now)
            baseline_blocks += 1
            if baseline_blocks >= CFG.baseline_blocks:
                baseline_valid = True
            continue
        score, reason = score_features(now, baseline, initial_mode)
        suspect_blocks = suspect_blocks + 1 if score >= 2 else 0
        if suspect_blocks >= CFG.trigger_blocks:
            return dict(triggered=True, trigger_time_s=stop / CFG.sample_rate_hz, trigger_score=score, trigger_reason=reason)
    return dict(triggered=False, trigger_time_s=np.nan, trigger_score=0, trigger_reason=0)


def run_case(case_cfg):
    i_adc, q_adc, change_time_s = synth_case(case_cfg)
    monitor = run_monitor(i_adc, q_adc, case_cfg["mod_a"])
    latency_s = monitor["trigger_time_s"] - change_time_s
    valid_detection = bool(case_cfg["has_change"] and monitor["triggered"] and 0 <= latency_s <= CFG.valid_latency_s)
    false_alarm = bool((not case_cfg["has_change"] and monitor["triggered"]) or
                       (case_cfg["has_change"] and monitor["triggered"] and latency_s < 0))
    late_detection = bool(case_cfg["has_change"] and monitor["triggered"] and latency_s > CFG.valid_latency_s)
    miss = bool(case_cfg["has_change"] and not valid_detection and not false_alarm)
    return dict(
        case_id=case_cfg["case_id"],
        scenario=case_cfg["scenario"],
        profile=case_cfg["profile_name"],
        snr_db=case_cfg["snr_db"],
        mod_a=case_cfg["mod_a"],
        mod_b=case_cfg["mod_b"],
        freq_step_hz=case_cfg["freq_step_hz"],
        has_change=case_cfg["has_change"],
        triggered=monitor["triggered"],
        valid_detection=valid_detection,
        false_alarm=false_alarm,
        miss=miss,
        late_detection=late_detection,
        latency_ms=latency_s * 1000.0 if np.isfinite(latency_s) else np.nan,
        trigger_score=monitor["trigger_score"],
        trigger_reason=monitor["trigger_reason"],
    )


def rate(values):
    return float(np.mean(values)) if values else float("nan")


def summarize(rows, group, value):
    changed = [r for r in rows if r["has_change"]]
    unchanged = [r for r in rows if not r["has_change"]]
    valid_lat = [r["latency_ms"] for r in changed if r["valid_detection"] and np.isfinite(r["latency_ms"])]
    return dict(
        group=group,
        value=str(value),
        cases=len(rows),
        changed_cases=len(changed),
        valid_detection_rate=rate([r["valid_detection"] for r in changed]) if changed else float("nan"),
        false_alarm_rate=rate([r["false_alarm"] for r in unchanged]) if unchanged else rate([r["false_alarm"] for r in rows]),
        miss_rate=rate([r["miss"] for r in changed]) if changed else float("nan"),
        late_rate=rate([r["late_detection"] for r in changed]) if changed else float("nan"),
        median_latency_ms=float(np.median(valid_lat)) if valid_lat else float("nan"),
        p95_latency_ms=float(np.percentile(valid_lat, 95)) if valid_lat else float("nan"),
    )


def build_summary(rows):
    summaries = [summarize(rows, "ALL", "ALL")]
    for field, group in [("scenario", "scenario"), ("profile", "profile"), ("snr_db", "snr"), ("freq_step_hz", "freq_step")]:
        for value in sorted(set(r[field] for r in rows), key=lambda x: str(x)):
            summaries.append(summarize([r for r in rows if r[field] == value], group, value))
    return summaries


def build_pair_summary(rows):
    changed = [r for r in rows if r["has_change"]]
    pairs = sorted(set((r["mod_a"], r["mod_b"]) for r in changed))
    return [summarize([r for r in changed if (r["mod_a"], r["mod_b"]) == pair], "pair", f"{pair[0]}->{pair[1]}") for pair in pairs]


def write_csv(path, rows):
    if not rows:
        return
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def write_report(path, rows, summary, pair_summary):
    overall = summary[0]
    same = [r for r in rows if r["scenario"] == "mod_switch_same_freq" and r["has_change"]]
    diff = [r for r in rows if r["scenario"] == "freq_mod_switch" and r["has_change"]]
    no_change = [r for r in rows if r["scenario"] == "no_change"]
    weak = [r for r in pair_summary if np.isfinite(r["valid_detection_rate"]) and r["valid_detection_rate"] < 0.85]
    with open(path, "w", encoding="utf-8") as f:
        f.write("# Focused Runtime Modulation Switch Monitor Report\n\n")
        f.write("Date: 2026-05-27\n\n")
        f.write("Model mirrors current `DemodTask.c` low-compute monitor: block_n=4096, decim=32, baseline_blocks=4, trigger_blocks=2.\n\n")
        f.write("Normal parameters: AM 10kHz depth 50%, FM 10kHz dev 75kHz, ASK/FSK/PSK 10kbps, FSK separation 20kHz.\n\n")
        f.write("## Main Result\n\n")
        f.write(f"- Cases: {len(rows)}\n")
        f.write(f"- All changed valid detection rate: {overall['valid_detection_rate']*100:.2f}%\n")
        f.write(f"- Same-frequency modulation-switch detection: {rate([r['valid_detection'] for r in same])*100:.2f}%\n")
        f.write(f"- Different-frequency modulation-switch detection: {rate([r['valid_detection'] for r in diff])*100:.2f}%\n")
        f.write(f"- No-change false-alarm rate: {rate([r['false_alarm'] for r in no_change])*100:.2f}%\n")
        f.write(f"- Median valid latency: {overall['median_latency_ms']:.2f} ms\n\n")
        f.write("## Interpretation\n\n")
        f.write("- Different-frequency switching is not automatically reliable in the current firmware model. Low-IF delta is only a strong trigger for AM/ASK/PSK initial modes, not FM/FSK.\n")
        f.write("- Same-frequency modulation-only switching can be detected when envelope or phase-dispersion features change enough, but constant-envelope transitions remain weak.\n")
        f.write("- ASK as the initial mode is prone to early false triggers in this sweep; FM/FSK -> PSK is prone to miss or late detection.\n")
        f.write("- The current monitor is useful as a low-cost re-recognition trigger, but its thresholds should not be treated as final until hardware logs confirm false alarm behavior.\n\n")
        f.write("## Summary\n\n")
        f.write("| Group | Value | Cases | Valid | False | Miss | Late | Median ms |\n")
        f.write("|---|---|---:|---:|---:|---:|---:|---:|\n")
        for r in summary:
            f.write(f"| {r['group']} | {r['value']} | {r['cases']} | {r['valid_detection_rate']*100:.2f}% | {r['false_alarm_rate']*100:.2f}% | {r['miss_rate']*100:.2f}% | {r['late_rate']*100:.2f}% | {r['median_latency_ms']:.2f} |\n")
        f.write("\n## Weak Pairs Below 85%\n\n")
        if not weak:
            f.write("No weak pair below 85% in this sweep.\n")
        else:
            f.write("| Pair | Cases | Valid | Miss | Late | Median ms |\n")
            f.write("|---|---:|---:|---:|---:|---:|\n")
            for r in weak:
                f.write(f"| {r['value']} | {r['cases']} | {r['valid_detection_rate']*100:.2f}% | {r['miss_rate']*100:.2f}% | {r['late_rate']*100:.2f}% | {r['median_latency_ms']:.2f} |\n")


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    cases = build_cases()
    print(f"Focused runtime mod-switch simulation cases={len(cases)} output={OUTPUT_DIR}")
    rows = []
    for idx, case_cfg in enumerate(cases, 1):
        rows.append(run_case(case_cfg))
        if idx % 100 == 0:
            print(f"  {idx}/{len(cases)}")
    summary = build_summary(rows)
    pair_summary = build_pair_summary(rows)
    write_csv(os.path.join(OUTPUT_DIR, "runtime_mod_switch_focused_results.csv"), rows)
    write_csv(os.path.join(OUTPUT_DIR, "runtime_mod_switch_focused_summary.csv"), summary)
    write_csv(os.path.join(OUTPUT_DIR, "runtime_mod_switch_focused_pair_summary.csv"), pair_summary)
    write_report(os.path.join(OUTPUT_DIR, "runtime_mod_switch_focused_report.md"), rows, summary, pair_summary)
    print("Overall:", summary[0])


if __name__ == "__main__":
    main()
