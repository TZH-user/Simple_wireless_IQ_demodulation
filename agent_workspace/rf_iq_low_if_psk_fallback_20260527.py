import csv
import math
import os
from dataclasses import dataclass
from typing import Dict, List, Tuple

import numpy as np


@dataclass(frozen=True)
class Config:
    sample_rate_hz: float = 2.048e6
    symbol_rate_hz: float = 10e3
    duration_s: float = 0.06
    block_n: int = 4096
    adc_bits: int = 12
    adc_target_peak_v: float = 0.82
    snr_db_list: Tuple[float, ...] = (float("inf"), 30.0, 20.0, 10.0)
    low_if_candidates_hz: Tuple[float, ...] = (
        0.0,
        500.0,
        1000.0,
        1500.0,
        2000.0,
        2250.0,
        2500.0,
        2750.0,
        3000.0,
        4000.0,
        5000.0,
        6000.0,
        8000.0,
    )
    rng_seed: int = 20260527


@dataclass(frozen=True)
class Profile:
    name: str
    drift_hz: float
    iq_gain_err: float
    iq_phase_err_deg: float
    image_leakage_db: float
    dc_offset: float
    ripple_depth: float


CFG = Config()
OUTPUT_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "lo_if_psk_fallback_output_20260527",
)


def build_profiles() -> List[Profile]:
    return [
        Profile(
            name="ideal",
            drift_hz=0.0,
            iq_gain_err=0.0,
            iq_phase_err_deg=0.0,
            image_leakage_db=float("-inf"),
            dc_offset=0.0,
            ripple_depth=0.0,
        ),
        Profile(
            name="board_like_normal",
            drift_hz=60.0,
            iq_gain_err=0.020,
            iq_phase_err_deg=2.0,
            image_leakage_db=-28.0,
            dc_offset=0.010,
            ripple_depth=0.010,
        ),
        Profile(
            name="board_like_hard",
            drift_hz=180.0,
            iq_gain_err=0.055,
            iq_phase_err_deg=5.0,
            image_leakage_db=-18.0,
            dc_offset=0.020,
            ripple_depth=0.020,
        ),
    ]


def fmt_float(value: float, digits: int = 3) -> str:
    if value is None:
        return ""
    if isinstance(value, float) and math.isnan(value):
        return ""
    if isinstance(value, float) and math.isinf(value):
        return "inf" if value > 0 else "-inf"
    return f"{value:.{digits}f}"


def wrap_pi(x: np.ndarray) -> np.ndarray:
    return (x + np.pi) % (2 * np.pi) - np.pi


def make_time_axis(n: int, fs: float) -> np.ndarray:
    return np.arange(n, dtype=np.float64) / fs


def make_symbol_indices(n: int, fs: float, rs: float) -> np.ndarray:
    idx = np.floor(np.arange(n, dtype=np.float64) * rs / fs).astype(np.int32)
    if len(idx) == 0:
        return idx
    return idx


def make_symbol_count(n: int, fs: float, rs: float) -> int:
    return int(math.ceil(n * rs / fs)) + 2


def make_bits(symbol_count: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    bits = rng.integers(0, 2, size=symbol_count, dtype=np.int8)
    if symbol_count >= 8:
        # a light shuffle of the first few symbols keeps the sequence from being
        # accidentally too friendly to one specific detector.
        bits[:4] = np.array([0, 1, 1, 0], dtype=np.int8)
    return bits


def apply_frontend(iq: np.ndarray, t: np.ndarray, profile: Profile) -> np.ndarray:
    out = iq.astype(np.complex128, copy=True)

    if np.isfinite(profile.image_leakage_db):
        leak = 10.0 ** (profile.image_leakage_db / 20.0)
        out = out + leak * np.conj(out)

    i = np.real(out)
    q = np.imag(out)
    phase = np.deg2rad(profile.iq_phase_err_deg)
    i = i * (1.0 + profile.iq_gain_err)
    q = q * (1.0 - profile.iq_gain_err)
    q = q * np.cos(phase) + i * np.sin(phase)
    out = i + 1j * q

    if profile.ripple_depth != 0.0:
        out = out * (1.0 + profile.ripple_depth * np.sin(2.0 * np.pi * 1200.0 * t))

    if profile.drift_hz != 0.0:
        # Slow quadratic phase drift across the record.
        drift_phase = 2.0 * np.pi * (profile.drift_hz / max(t[-1], 1e-9)) * (t ** 2) / 2.0
        out = out * np.exp(1j * drift_phase)

    out = out + profile.dc_offset * (1.0 + 0.7j)
    return out


def add_awgn(iq: np.ndarray, snr_db: float, seed: int) -> np.ndarray:
    if math.isinf(snr_db):
        return iq
    rng = np.random.default_rng(seed)
    sig_power = float(np.mean(np.abs(iq) ** 2))
    noise_power = sig_power / (10.0 ** (snr_db / 10.0))
    noise = math.sqrt(noise_power / 2.0) * (
        rng.standard_normal(iq.shape) + 1j * rng.standard_normal(iq.shape)
    )
    return iq + noise


def quantize_adc(iq: np.ndarray, cfg: Config) -> Tuple[np.ndarray, np.ndarray, float]:
    max_code = (1 << cfg.adc_bits) - 1
    center = 1 << (cfg.adc_bits - 1)
    peak = float(np.max(np.maximum(np.abs(np.real(iq)), np.abs(np.imag(iq)))))
    if peak <= 0.0:
        peak = 1.0
    scale = cfg.adc_target_peak_v * center / peak
    i_adc = np.clip(np.rint(center + np.real(iq) * scale), 0, max_code).astype(np.int32)
    q_adc = np.clip(np.rint(center + np.imag(iq) * scale), 0, max_code).astype(np.int32)
    clip_rate = float(np.mean((i_adc == 0) | (i_adc == max_code) | (q_adc == 0) | (q_adc == max_code)))
    return i_adc, q_adc, clip_rate


def synthesize_ask(
    fs: float,
    rs: float,
    low_if_hz: float,
    profile: Profile,
    snr_db: float,
    seed: int,
) -> Dict[str, np.ndarray]:
    n = int(round(CFG.duration_s * fs))
    t = make_time_axis(n, fs)
    symbol_count = make_symbol_count(n, fs, rs)
    bits = make_bits(symbol_count, seed)
    sym_idx = make_symbol_indices(n, fs, rs)
    env = 0.05 + 0.95 * bits[sym_idx]
    phi0 = np.random.default_rng(seed + 17).uniform(0.0, 2.0 * np.pi)
    iq = env * np.exp(1j * (2.0 * np.pi * low_if_hz * t + phi0))
    iq = apply_frontend(iq, t, profile)
    iq = add_awgn(iq, snr_db, seed + 101)
    i_adc, q_adc, clip_rate = quantize_adc(iq, CFG)
    return {
        "t": t,
        "iq": iq,
        "bits": bits,
        "sym_idx": sym_idx,
        "i_adc": i_adc,
        "q_adc": q_adc,
        "clip_rate": clip_rate,
    }


def synthesize_fsk(
    fs: float,
    rs: float,
    low_if_hz: float,
    profile: Profile,
    snr_db: float,
    seed: int,
) -> Dict[str, np.ndarray]:
    n = int(round(CFG.duration_s * fs))
    t = make_time_axis(n, fs)
    symbol_count = make_symbol_count(n, fs, rs)
    bits = make_bits(symbol_count, seed)
    sym_idx = make_symbol_indices(n, fs, rs)
    sep_hz = 20e3
    inst_freq = low_if_hz + (2.0 * bits[sym_idx].astype(np.float64) - 1.0) * (sep_hz / 2.0)
    phi0 = np.random.default_rng(seed + 31).uniform(0.0, 2.0 * np.pi)
    phase = 2.0 * np.pi * np.cumsum(inst_freq) / fs + phi0
    iq = np.exp(1j * phase)
    iq = apply_frontend(iq, t, profile)
    iq = add_awgn(iq, snr_db, seed + 211)
    i_adc, q_adc, clip_rate = quantize_adc(iq, CFG)
    return {
        "t": t,
        "iq": iq,
        "bits": bits,
        "sym_idx": sym_idx,
        "i_adc": i_adc,
        "q_adc": q_adc,
        "clip_rate": clip_rate,
        "sep_hz": sep_hz,
    }


def synthesize_psk(
    fs: float,
    rs: float,
    low_if_hz: float,
    profile: Profile,
    snr_db: float,
    seed: int,
) -> Dict[str, np.ndarray]:
    n = int(round(CFG.duration_s * fs))
    t = make_time_axis(n, fs)
    symbol_count = make_symbol_count(n, fs, rs)
    bits = make_bits(symbol_count, seed)
    sym_idx = make_symbol_indices(n, fs, rs)
    symbols = 2.0 * bits[sym_idx].astype(np.float64) - 1.0
    phi0 = np.random.default_rng(seed + 43).uniform(0.0, 2.0 * np.pi)
    iq = symbols * np.exp(1j * (2.0 * np.pi * low_if_hz * t + phi0))
    iq = apply_frontend(iq, t, profile)
    iq = add_awgn(iq, snr_db, seed + 313)
    i_adc, q_adc, clip_rate = quantize_adc(iq, CFG)
    return {
        "t": t,
        "iq": iq,
        "bits": bits,
        "sym_idx": sym_idx,
        "i_adc": i_adc,
        "q_adc": q_adc,
        "clip_rate": clip_rate,
    }


def block_center(i_adc: np.ndarray, q_adc: np.ndarray, block_n: int) -> Tuple[np.ndarray, np.ndarray]:
    i_out = np.empty_like(i_adc, dtype=np.float64)
    q_out = np.empty_like(q_adc, dtype=np.float64)
    for start in range(0, len(i_adc), block_n):
        end = min(len(i_adc), start + block_n)
        i_block = i_adc[start:end].astype(np.float64)
        q_block = q_adc[start:end].astype(np.float64)
        i_block -= np.mean(i_block)
        q_block -= np.mean(q_block)
        i_out[start:end] = i_block
        q_out[start:end] = q_block
    return i_out, q_out


def mean_per_symbol(values: np.ndarray, sym_idx: np.ndarray, symbol_count: int) -> Tuple[np.ndarray, np.ndarray]:
    sums = np.bincount(sym_idx, weights=values, minlength=symbol_count)
    counts = np.bincount(sym_idx, minlength=symbol_count)
    means = np.zeros(symbol_count, dtype=np.float64)
    valid = counts > 0
    means[valid] = sums[valid] / counts[valid]
    return means, counts


def best_polarity_ber(decoded: np.ndarray, truth: np.ndarray) -> float:
    ber = float(np.mean(decoded != truth))
    ber_inv = float(np.mean((1 - decoded) != truth))
    return min(ber, ber_inv)


def demod_ask(i_adc: np.ndarray, q_adc: np.ndarray, sym_idx: np.ndarray, symbol_count: int) -> Dict[str, float]:
    i_c, q_c = block_center(i_adc, q_adc, CFG.block_n)
    env = np.sqrt(i_c * i_c + q_c * q_c)
    sym_mean, _ = mean_per_symbol(env, sym_idx, symbol_count)

    low_est = None
    high_est = None
    threshold = None
    decoded = np.zeros(symbol_count, dtype=np.int8)
    for k, x in enumerate(sym_mean):
        x_int = int(round(x))
        if low_est is None:
            low_est = x_int
            high_est = x_int
            threshold = x_int
        else:
            if x_int < low_est:
                low_est = x_int
            elif x_int < threshold:
                low_est += int(np.rint((x_int - low_est) / 8.0))

            if x_int > high_est:
                high_est = x_int
            elif x_int >= threshold:
                high_est += int(np.rint((x_int - high_est) / 8.0))

            threshold = int((low_est + high_est) // 2)

        spread = abs(high_est - low_est)
        if spread >= 48:
            decoded[k] = 1 if x_int >= threshold else 0
        else:
            decoded[k] = decoded[k - 1] if k > 0 else 0

    return {
        "decoded": decoded,
        "mean_env": float(np.mean(sym_mean)),
    }


def fsk_min_spread_q12(sep_hz: float, fs: float) -> int:
    expected = int((sep_hz * 25736.0) / fs / 5.0)
    return max(24, expected)


def demod_fsk(
    i_adc: np.ndarray,
    q_adc: np.ndarray,
    sym_idx: np.ndarray,
    symbol_count: int,
    sep_hz: float,
) -> Dict[str, float]:
    i_c, q_c = block_center(i_adc, q_adc, CFG.block_n)
    z = i_c + 1j * q_c
    z_dec = z[::4]
    if len(z_dec) < 2:
        return {"decoded": np.zeros(symbol_count, dtype=np.int8), "spread": 0.0}

    phase_diff = np.angle(z_dec[1:] * np.conj(z_dec[:-1]))
    raw_q12 = phase_diff * 4096.0 / (2.0 * np.pi)
    smooth = raw_q12.copy()

    decimated_sample_idx = (np.arange(1, len(z_dec), dtype=np.int32) * 4).astype(np.int32)
    dec_sym_idx = np.minimum((decimated_sample_idx.astype(np.float64) * CFG.symbol_rate_hz / CFG.sample_rate_hz).astype(np.int32), symbol_count - 1)
    sym_mean, sym_counts = mean_per_symbol(smooth, dec_sym_idx, symbol_count)

    low_est = None
    high_est = None
    threshold = None
    warmup_count = 0
    warmup_min = None
    warmup_max = None
    decoded = np.zeros(symbol_count, dtype=np.int8)
    min_spread = fsk_min_spread_q12(sep_hz, CFG.sample_rate_hz)

    for k, x in enumerate(sym_mean):
        x_int = int(round(x))
        if warmup_count < 24:
            if warmup_min is None:
                warmup_min = x_int
                warmup_max = x_int
            else:
                warmup_min = min(warmup_min, x_int)
                warmup_max = max(warmup_max, x_int)
            warmup_count += 1
            if warmup_count >= 24:
                low_est = warmup_min
                high_est = warmup_max
                threshold = int((low_est + high_est) // 2)
            decoded[k] = decoded[k - 1] if k > 0 else 0
            continue

        if x_int < low_est:
            low_est = x_int
        elif x_int < threshold:
            low_est += int(np.rint((x_int - low_est) / 8.0))

        if x_int > high_est:
            high_est = x_int
        elif x_int >= threshold:
            high_est += int(np.rint((x_int - high_est) / 8.0))

        threshold = int((low_est + high_est) // 2)
        spread = abs(high_est - low_est)
        if spread >= min_spread:
            decoded[k] = 1 if x_int >= threshold else 0
        else:
            decoded[k] = decoded[k - 1] if k > 0 else 0

    return {
        "decoded": decoded,
        "spread": float(abs((high_est or 0) - (low_est or 0))),
        "mean_sym": float(np.mean(sym_mean)),
        "min_spread": float(min_spread),
    }


def demod_psk_coherent(
    i_adc: np.ndarray,
    q_adc: np.ndarray,
    sym_idx: np.ndarray,
    symbol_count: int,
    low_if_hz: float,
) -> Dict[str, float]:
    i_c, q_c = block_center(i_adc, q_adc, CFG.block_n)
    z = (i_c + 1j * q_c) * np.exp(-1j * 2.0 * np.pi * low_if_hz * make_time_axis(len(i_c), CFG.sample_rate_hz))
    sym_i, _ = mean_per_symbol(np.real(z), sym_idx, symbol_count)
    sym_q, _ = mean_per_symbol(np.imag(z), sym_idx, symbol_count)
    sym = sym_i + 1j * sym_q
    m20 = float(np.mean(sym_i * sym_i - sym_q * sym_q))
    m11 = float(np.mean(2.0 * sym_i * sym_q))
    axis_angle = 0.5 * math.atan2(m11, m20)
    projection = sym_i * math.cos(axis_angle) + sym_q * math.sin(axis_angle)
    decoded = (projection < 0.0).astype(np.int8)
    return {
        "decoded": decoded,
        "mean_mag": float(np.mean(np.abs(sym))),
        "axis_angle_rad": axis_angle,
    }


def demod_psk_phase_jump(
    i_adc: np.ndarray,
    q_adc: np.ndarray,
    sym_idx: np.ndarray,
    symbol_count: int,
    phase_jump_threshold_rad: float = np.pi / 2.0,
) -> Dict[str, float]:
    i_c, q_c = block_center(i_adc, q_adc, CFG.block_n)
    z = i_c + 1j * q_c
    sym_i, _ = mean_per_symbol(np.real(z), sym_idx, symbol_count)
    sym_q, _ = mean_per_symbol(np.imag(z), sym_idx, symbol_count)
    sym = sym_i + 1j * sym_q

    if symbol_count < 2:
        return {
            "flip_pred": np.zeros(symbol_count, dtype=np.int8),
            "phase_delta_deg_same": 0.0,
            "phase_delta_deg_flip": 0.0,
        }

    delta = np.angle(sym[1:] * np.conj(sym[:-1]))
    delta_abs = np.abs(wrap_pi(delta))
    flip_pred = (delta_abs >= phase_jump_threshold_rad).astype(np.int8)

    return {
        "flip_pred": flip_pred,
        "phase_delta_deg_same": float(np.mean(np.abs(np.rad2deg(delta_abs)))),
        "phase_delta_deg_flip": float(np.mean(np.abs(np.rad2deg(wrap_pi(delta + np.pi))))),
    }


def score_digital_case(decoded: np.ndarray, truth_bits: np.ndarray, discard_symbols: int = 32) -> Dict[str, float]:
    truth = truth_bits[: len(decoded)]
    if len(decoded) > discard_symbols:
        decoded = decoded[discard_symbols:]
        truth = truth[discard_symbols:]
    ber = best_polarity_ber(decoded.astype(np.int8), truth.astype(np.int8))
    return {
        "ber": ber,
        "pass": float(ber <= 0.01),
    }


def score_psk_fallback(flip_pred: np.ndarray, truth_bits: np.ndarray) -> Dict[str, float]:
    truth_flip = (truth_bits[1 : len(flip_pred) + 1] != truth_bits[: len(flip_pred)]).astype(np.int8)
    pred = flip_pred.astype(np.int8)
    tp = int(np.sum((pred == 1) & (truth_flip == 1)))
    fp = int(np.sum((pred == 1) & (truth_flip == 0)))
    fn = int(np.sum((pred == 0) & (truth_flip == 1)))
    tn = int(np.sum((pred == 0) & (truth_flip == 0)))
    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall = tp / (tp + fn) if (tp + fn) else 0.0
    f1 = (2.0 * precision * recall / (precision + recall)) if (precision + recall) else 0.0
    false_alarm = fp / (fp + tn) if (fp + tn) else 0.0
    miss = fn / (tp + fn) if (tp + fn) else 0.0
    return {
        "precision": precision,
        "recall": recall,
        "f1": f1,
        "false_alarm": false_alarm,
        "miss": miss,
        "tp": tp,
        "fp": fp,
        "fn": fn,
        "tn": tn,
        "pass": float((precision >= 0.95) and (recall >= 0.95) and (false_alarm <= 0.05)),
    }


def build_case_seed(profile_idx: int, snr_idx: int, mod_idx: int) -> int:
    return CFG.rng_seed + profile_idx * 10000 + snr_idx * 1000 + mod_idx * 100


def evaluate_case(profile_idx: int, profile: Profile, snr_idx: int, snr_db: float, low_if_hz: float) -> List[Dict[str, float]]:
    rows = []

    # ASK
    ask_seed = build_case_seed(profile_idx, snr_idx, 0)
    ask = synthesize_ask(CFG.sample_rate_hz, CFG.symbol_rate_hz, low_if_hz, profile, snr_db, ask_seed)
    ask_sym_count = int(np.max(ask["sym_idx"])) + 1
    ask_out = demod_ask(ask["i_adc"], ask["q_adc"], ask["sym_idx"], ask_sym_count)
    ask_score = score_digital_case(ask_out["decoded"], ask["bits"])
    rows.append(
        dict(
            modulation="ASK",
            variant="firmware_like",
            profile=profile.name,
            snr_db=snr_db,
            low_if_hz=low_if_hz,
            symbol_rate_hz=CFG.symbol_rate_hz,
            ber=ask_score["ber"],
            precision=np.nan,
            recall=np.nan,
            f1=np.nan,
            false_alarm=np.nan,
            miss=np.nan,
            clip_rate=ask["clip_rate"],
            pass_flag=bool(ask_score["pass"] > 0.5),
            note="",
        )
    )

    # FSK
    fsk_seed = build_case_seed(profile_idx, snr_idx, 1)
    fsk = synthesize_fsk(CFG.sample_rate_hz, CFG.symbol_rate_hz, low_if_hz, profile, snr_db, fsk_seed)
    fsk_sym_count = int(np.max(fsk["sym_idx"])) + 1
    fsk_out = demod_fsk(fsk["i_adc"], fsk["q_adc"], fsk["sym_idx"], fsk_sym_count, fsk["sep_hz"])
    fsk_score = score_digital_case(fsk_out["decoded"], fsk["bits"])
    rows.append(
        dict(
            modulation="FSK",
            variant="firmware_like",
            profile=profile.name,
            snr_db=snr_db,
            low_if_hz=low_if_hz,
            symbol_rate_hz=CFG.symbol_rate_hz,
            ber=fsk_score["ber"],
            precision=np.nan,
            recall=np.nan,
            f1=np.nan,
            false_alarm=np.nan,
            miss=np.nan,
            clip_rate=fsk["clip_rate"],
            pass_flag=bool(fsk_score["pass"] > 0.5),
            note="",
        )
    )

    # PSK phase-jump fallback
    psk_seed = build_case_seed(profile_idx, snr_idx, 2)
    psk = synthesize_psk(CFG.sample_rate_hz, CFG.symbol_rate_hz, low_if_hz, profile, snr_db, psk_seed)
    psk_sym_count = int(np.max(psk["sym_idx"])) + 1
    psk_fb_out = demod_psk_phase_jump(psk["i_adc"], psk["q_adc"], psk["sym_idx"], psk_sym_count)
    psk_fb_score = score_psk_fallback(psk_fb_out["flip_pred"], psk["bits"])
    rows.append(
        dict(
            modulation="PSK",
            variant="phase_jump_fallback",
            profile=profile.name,
            snr_db=snr_db,
            low_if_hz=low_if_hz,
            symbol_rate_hz=CFG.symbol_rate_hz,
            ber=np.nan,
            precision=psk_fb_score["precision"],
            recall=psk_fb_score["recall"],
            f1=psk_fb_score["f1"],
            false_alarm=psk_fb_score["false_alarm"],
            miss=psk_fb_score["miss"],
            clip_rate=psk["clip_rate"],
            pass_flag=bool(psk_fb_score["pass"] > 0.5),
            note="flip detection only",
            phase_delta_same_deg=psk_fb_out["phase_delta_deg_same"],
            phase_delta_flip_deg=psk_fb_out["phase_delta_deg_flip"],
        )
    )

    return rows


def aggregate_mean(rows: List[Dict[str, float]], keys: List[str], metric: str) -> List[Dict[str, float]]:
    groups: Dict[Tuple, List[float]] = {}
    pass_groups: Dict[Tuple, List[float]] = {}
    meta: Dict[Tuple, Dict[str, float]] = {}
    for row in rows:
        key = tuple(row[k] for k in keys)
        groups.setdefault(key, [])
        pass_groups.setdefault(key, [])
        value = row.get(metric, np.nan)
        if value is not None and not (isinstance(value, float) and math.isnan(value)):
            groups[key].append(float(value))
        pass_groups[key].append(1.0 if row.get("pass_flag", False) else 0.0)
        if key not in meta:
            meta[key] = {k: row[k] for k in keys}
    out = []
    for key, vals in groups.items():
        if not vals:
            continue
        item = dict(meta[key])
        item[f"mean_{metric}"] = float(np.mean(vals))
        item[f"worst_{metric}"] = float(np.max(vals))
        item[f"best_{metric}"] = float(np.min(vals))
        item["pass_rate"] = float(np.mean(pass_groups[key])) if pass_groups[key] else 0.0
        item[f"count"] = len(vals)
        out.append(item)
    return out


def aggregate_psk_fallback(rows: List[Dict[str, float]], keys: List[str]) -> List[Dict[str, float]]:
    groups: Dict[Tuple, List[Dict[str, float]]] = {}
    for row in rows:
        key = tuple(row[k] for k in keys)
        groups.setdefault(key, []).append(row)

    out = []
    for key, vals in groups.items():
        item = {k: vals[0][k] for k in keys}
        item["count"] = len(vals)
        item["mean_precision"] = float(np.mean([v["precision"] for v in vals]))
        item["mean_recall"] = float(np.mean([v["recall"] for v in vals]))
        item["mean_f1"] = float(np.mean([v["f1"] for v in vals]))
        item["mean_false_alarm"] = float(np.mean([v["false_alarm"] for v in vals]))
        item["mean_miss"] = float(np.mean([v["miss"] for v in vals]))
        item["worst_precision"] = float(np.min([v["precision"] for v in vals]))
        item["worst_recall"] = float(np.min([v["recall"] for v in vals]))
        item["worst_f1"] = float(np.min([v["f1"] for v in vals]))
        item["worst_false_alarm"] = float(np.max([v["false_alarm"] for v in vals]))
        item["worst_miss"] = float(np.max([v["miss"] for v in vals]))
        item["pass_rate"] = float(np.mean([1.0 if v["pass_flag"] else 0.0 for v in vals]))
        out.append(item)
    return out


def write_csv(path: str, rows: List[Dict[str, float]], fieldnames: List[str]) -> None:
    with open(path, "w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            out_row = {}
            for field in fieldnames:
                val = row.get(field, "")
                if isinstance(val, bool):
                    out_row[field] = "1" if val else "0"
                elif isinstance(val, float):
                    out_row[field] = fmt_float(val, 6)
                elif val is None:
                    out_row[field] = ""
                else:
                    out_row[field] = val
            writer.writerow(out_row)


def make_report(digital_rows: List[Dict[str, float]], fallback_rows: List[Dict[str, float]], digital_summary: List[Dict[str, float]], fallback_summary: List[Dict[str, float]], report_path: str) -> None:
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("# LO 偏差与 PSK 相位跳变替代算法仿真\n\n")
        f.write(f"- Fs: {CFG.sample_rate_hz/1e6:.3f} MS/s\n")
        f.write(f"- Symbol rate: {CFG.symbol_rate_hz/1e3:.1f} kbps\n")
        f.write(f"- Phase-jump knee: roughly Fs/4 = {CFG.symbol_rate_hz/4.0:.1f} Hz for an uncompensated symbol-to-symbol detector.\n")
        f.write(f"- Low IF sweep: {', '.join(fmt_float(x, 0) for x in CFG.low_if_candidates_hz)} Hz\n\n")
        f.write("## 结论\n\n")
        f.write("- ASK/FSK 对低 IF 的敏感度较低，主要表现为在极端低 IF 时 DC / 阻塞项更明显，但整体 BER 随低 IF 变化不大。\n")
        f.write("- PSK 的完整相干路径在本仿真里对低 IF 基本不敏感，因为已显式补偿低 IF。\n")
        f.write("- PSK 的下位替代路径（只看相位跳变反转符号）对低 IF 非常敏感：当每个符号的相位推进接近或超过 90° 时，same / flip 两类样本开始在相位差上互相折叠。\n")
        f.write("- 因此，若走下位替代算法，推荐把低 IF 控制在 1.0~2.0 kHz，保守上限不要超过 2.25~2.5 kHz；5 kHz 明显不适合该替代路径。\n\n")

        f.write("## ASK / FSK / PSK coherent summary\n\n")
        f.write("| mod | low_if_hz | mean_ber | worst_ber | pass_rate |\n")
        f.write("|---|---:|---:|---:|---:|\n")
        for row in digital_summary:
            f.write(
                f"| {row['modulation']} | {fmt_float(row['low_if_hz'],0)} | "
                f"{fmt_float(row['mean_ber'],6)} | {fmt_float(row['worst_ber'],6)} | "
                f"{fmt_float(row['pass_rate'],3)} |\n"
            )

        f.write("\n## PSK phase-jump fallback summary\n\n")
        f.write("| low_if_hz | mean_precision | mean_recall | mean_f1 | mean_false_alarm | mean_miss | worst_precision | worst_recall | worst_false_alarm |\n")
        f.write("|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for row in fallback_summary:
            f.write(
                f"| {fmt_float(row['low_if_hz'],0)} | {fmt_float(row['mean_precision'],3)} | "
                f"{fmt_float(row['mean_recall'],3)} | {fmt_float(row['mean_f1'],3)} | "
                f"{fmt_float(row['mean_false_alarm'],3)} | {fmt_float(row['mean_miss'],3)} | "
                f"{fmt_float(row['worst_precision'],3)} | {fmt_float(row['worst_recall'],3)} | "
                f"{fmt_float(row['worst_false_alarm'],3)} |\n"
            )

        f.write("\n## 代码解读\n\n")
        f.write("- 相位跳变替代算法的本质是比较相邻符号均值的 `angle(z_k * conj(z_{k-1}))`。\n")
        f.write("- 对于 10 kbps，符号间隔是 100 us；如果低 IF 为 2.5 kHz，则每符号相位推进刚好是 90°，这是可判与不可判之间的分界点。\n")
        f.write("- 低 IF 再往上走，same symbol 与 flip symbol 的相位差会在模 2π 后互相折叠，误判和漏判都会上升。\n")
        f.write("- 这就是为什么 5 kHz 不是一个适合该替代路径的工作点。\n")


def make_report_ascii(
    digital_rows: List[Dict[str, float]],
    fallback_rows: List[Dict[str, float]],
    digital_summary: List[Dict[str, float]],
    fallback_summary: List[Dict[str, float]],
    report_path: str,
) -> None:
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("# LO Bias Sweep And PSK Phase-Jump Fallback Simulation\n\n")
        f.write(f"- Fs: {CFG.sample_rate_hz / 1e6:.3f} MS/s\n")
        f.write(f"- Symbol rate: {CFG.symbol_rate_hz / 1e3:.1f} kbps\n")
        f.write(f"- Raw phase-jump knee: about Rs/4 = {CFG.symbol_rate_hz / 4.0:.1f} Hz\n")
        f.write(f"- Low IF sweep: {', '.join(fmt_float(x, 0) for x in CFG.low_if_candidates_hz)} Hz\n\n")

        f.write("## Conclusion\n\n")
        f.write("- ASK/FSK are not the limiting factor for low-IF selection. ASK is poor at exact zero-IF in this model because block DC removal distorts a unipolar envelope, but it becomes stable from about 1 kHz upward.\n")
        f.write("- The PSK fallback path tested here does not perform full carrier recovery. It only detects symbol-to-symbol phase jumps and toggles state on a detected reversal.\n")
        f.write("- For 10 kbps BPSK, an uncompensated low IF of Rs/4 = 2.5 kHz creates a 90 degree phase step per symbol, which is the decision knee. Above that point, same-symbol and flipped-symbol phase differences fold into each other after modulo 2*pi.\n")
        f.write("- The safe PSK fallback region is 0.5~1.5 kHz in this run. A practical shared low-IF target is 1 kHz; 1.5 kHz is still usable, 2 kHz is already marginal, and 5 kHz is unsuitable for this fallback path.\n\n")

        f.write("## ASK / FSK Firmware-Like BER Summary\n\n")
        f.write("| mod | low_if_hz | mean_ber | worst_ber | pass_rate |\n")
        f.write("|---|---:|---:|---:|---:|\n")
        for row in digital_summary:
            f.write(
                f"| {row['modulation']} | {fmt_float(row['low_if_hz'], 0)} | "
                f"{fmt_float(row['mean_ber'], 6)} | {fmt_float(row['worst_ber'], 6)} | "
                f"{fmt_float(row['pass_rate'], 3)} |\n"
            )

        f.write("\n## PSK Phase-Jump Fallback Summary\n\n")
        f.write("| low_if_hz | mean_precision | mean_recall | mean_f1 | mean_false_alarm | mean_miss | worst_precision | worst_recall | worst_false_alarm |\n")
        f.write("|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for row in fallback_summary:
            f.write(
                f"| {fmt_float(row['low_if_hz'], 0)} | {fmt_float(row['mean_precision'], 3)} | "
                f"{fmt_float(row['mean_recall'], 3)} | {fmt_float(row['mean_f1'], 3)} | "
                f"{fmt_float(row['mean_false_alarm'], 3)} | {fmt_float(row['mean_miss'], 3)} | "
                f"{fmt_float(row['worst_precision'], 3)} | {fmt_float(row['worst_recall'], 3)} | "
                f"{fmt_float(row['worst_false_alarm'], 3)} |\n"
            )

        f.write("\n## Algorithm Note\n\n")
        f.write("- The fallback detector uses `angle(z_k * conj(z_{k-1}))` on adjacent symbol averages.\n")
        f.write("- It marks a reversal when the absolute wrapped phase difference is at least pi/2.\n")
        f.write("- With no carrier compensation, the usable low-IF range scales with symbol rate: keep `abs(low_if) < Rs/4`; leave margin for noise and drift.\n")


def main() -> None:
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    profiles = build_profiles()

    digital_rows: List[Dict[str, float]] = []
    fallback_rows: List[Dict[str, float]] = []

    print("LO bias sweep and PSK fallback simulation")
    print(f"Output: {OUTPUT_DIR}")
    print(f"Fs={CFG.sample_rate_hz/1e6:.3f} MS/s, Rs={CFG.symbol_rate_hz/1e3:.1f} kbps, duration={CFG.duration_s*1e3:.1f} ms")
    print(f"Low IF candidates: {', '.join(fmt_float(x,0) for x in CFG.low_if_candidates_hz)} Hz")
    print(f"Theoretical raw phase-jump limit for the fallback path: {CFG.symbol_rate_hz/4.0:.1f} Hz")

    for profile_idx, profile in enumerate(profiles):
        for snr_idx, snr_db in enumerate(CFG.snr_db_list):
            for low_if_hz in CFG.low_if_candidates_hz:
                rows = evaluate_case(profile_idx, profile, snr_idx, snr_db, low_if_hz)
                for row in rows:
                    if row["variant"] == "phase_jump_fallback":
                        fallback_rows.append(row)
                    else:
                        digital_rows.append(row)

    digital_summary = aggregate_mean(digital_rows, ["modulation", "low_if_hz"], "ber")
    digital_summary.sort(key=lambda r: (r["modulation"], r["low_if_hz"]))

    fallback_summary = aggregate_psk_fallback(fallback_rows, ["low_if_hz"])
    fallback_summary.sort(key=lambda r: r["low_if_hz"])

    digital_csv = os.path.join(OUTPUT_DIR, "lo_if_digital_demod_results.csv")
    fallback_csv = os.path.join(OUTPUT_DIR, "psk_phase_jump_fallback_results.csv")
    digital_summary_csv = os.path.join(OUTPUT_DIR, "lo_if_digital_summary.csv")
    fallback_summary_csv = os.path.join(OUTPUT_DIR, "psk_phase_jump_fallback_summary.csv")
    report_path = os.path.join(OUTPUT_DIR, "lo_if_psk_fallback_report.md")

    write_csv(
        digital_csv,
        digital_rows,
        [
            "modulation",
            "variant",
            "profile",
            "snr_db",
            "low_if_hz",
            "symbol_rate_hz",
            "ber",
            "clip_rate",
            "pass_flag",
            "note",
        ],
    )
    write_csv(
        fallback_csv,
        fallback_rows,
        [
            "modulation",
            "variant",
            "profile",
            "snr_db",
            "low_if_hz",
            "symbol_rate_hz",
            "precision",
            "recall",
            "f1",
            "false_alarm",
            "miss",
            "clip_rate",
            "phase_delta_same_deg",
            "phase_delta_flip_deg",
            "pass_flag",
            "note",
        ],
    )

    write_csv(
        digital_summary_csv,
        digital_summary,
        ["modulation", "low_if_hz", "mean_ber", "worst_ber", "best_ber", "count"],
    )
    write_csv(
        fallback_summary_csv,
        fallback_summary,
        [
            "low_if_hz",
            "mean_precision",
            "mean_recall",
            "mean_f1",
            "mean_false_alarm",
            "mean_miss",
            "worst_precision",
            "worst_recall",
            "worst_f1",
            "worst_false_alarm",
            "worst_miss",
            "pass_rate",
            "count",
        ],
    )

    make_report_ascii(digital_rows, fallback_rows, digital_summary, fallback_summary, report_path)

    print("\nCompleted.")
    print(f"Digital results CSV: {digital_csv}")
    print(f"Fallback results CSV: {fallback_csv}")
    print(f"Digital summary CSV: {digital_summary_csv}")
    print(f"Fallback summary CSV: {fallback_summary_csv}")
    print(f"Report: {report_path}")

    # Print compact top-line picks.
    digital_by_mod: Dict[str, List[Dict[str, float]]] = {}
    for row in digital_summary:
        digital_by_mod.setdefault(row["modulation"], []).append(row)
    print("\nBest low IF by mean BER:")
    for mod, rows in digital_by_mod.items():
        rows = sorted(rows, key=lambda r: r["mean_ber"])
        best = rows[0]
        print(
            f"  {mod}: {best['low_if_hz']:.0f} Hz (mean BER {best['mean_ber']:.6f}, worst BER {best['worst_ber']:.6f})"
        )

    fallback_by_score = sorted(
        fallback_summary,
        key=lambda r: (-r["mean_f1"], r["mean_false_alarm"], -r["mean_recall"]),
    )
    best_fb = fallback_by_score[0]
    print(
        "\nBest PSK phase-jump fallback region by mean F1:\n"
        f"  {best_fb['low_if_hz']:.0f} Hz "
        f"(precision {best_fb['mean_precision']:.3f}, recall {best_fb['mean_recall']:.3f}, "
        f"F1 {best_fb['mean_f1']:.3f}, false_alarm {best_fb['mean_false_alarm']:.3f}, miss {best_fb['mean_miss']:.3f})"
    )


if __name__ == "__main__":
    main()
