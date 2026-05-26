# Focused Runtime Modulation Switch Monitor Report

Date: 2026-05-27

Model mirrors current `DemodTask.c` low-compute monitor: block_n=4096, decim=32, baseline_blocks=4, trigger_blocks=2.

Normal parameters: AM 10kHz depth 50%, FM 10kHz dev 75kHz, ASK/FSK/PSK 10kbps, FSK separation 20kHz.

## Main Result

- Cases: 630
- All changed valid detection rate: 78.33%
- Same-frequency modulation-switch detection: 83.33%
- Different-frequency modulation-switch detection: 77.08%
- No-change false-alarm rate: 20.00%
- Median valid latency: 4.00 ms

## Interpretation

- Different-frequency switching is not automatically reliable in the current firmware model. Low-IF delta is only a strong trigger for AM/ASK/PSK initial modes, not FM/FSK.
- Same-frequency modulation-only switching can be detected when envelope or phase-dispersion features change enough, but constant-envelope transitions remain weak.
- ASK as the initial mode is prone to early false triggers in this sweep; FM/FSK -> PSK is prone to miss or late detection.
- The current monitor is useful as a low-cost re-recognition trigger, but its thresholds should not be treated as final until hardware logs confirm false alarm behavior.

## Summary

| Group | Value | Cases | Valid | False | Miss | Late | Median ms |
|---|---|---:|---:|---:|---:|---:|---:|
| ALL | ALL | 630 | 78.33% | 20.00% | 8.00% | 2.17% | 4.00 |
| scenario | freq_mod_switch | 480 | 77.08% | 14.17% | 8.75% | 2.29% | 4.00 |
| scenario | mod_switch_same_freq | 120 | 83.33% | 11.67% | 5.00% | 1.67% | 4.00 |
| scenario | no_change | 30 | nan% | 20.00% | nan% | nan% | nan |
| profile | board_like_hard | 210 | 78.00% | 20.00% | 8.50% | 2.00% | 4.00 |
| profile | board_like_normal | 210 | 78.50% | 20.00% | 8.50% | 2.00% | 4.00 |
| profile | ideal | 210 | 78.50% | 20.00% | 7.00% | 2.50% | 4.00 |
| snr | 10.0 | 315 | 83.00% | 20.00% | 9.33% | 3.00% | 4.00 |
| snr | 20.0 | 315 | 73.67% | 20.00% | 6.67% | 1.33% | 4.00 |
| freq_step | -10000.0 | 120 | 77.50% | 15.00% | 7.50% | 3.33% | 4.00 |
| freq_step | 0.0 | 150 | 83.33% | 20.00% | 5.00% | 1.67% | 4.00 |
| freq_step | 10000.0 | 120 | 79.17% | 13.33% | 7.50% | 3.33% | 4.00 |
| freq_step | 20000.0 | 120 | 73.33% | 15.00% | 11.67% | 1.67% | 4.00 |
| freq_step | 50000.0 | 120 | 78.33% | 13.33% | 8.33% | 0.83% | 4.00 |

## Weak Pairs Below 85%

| Pair | Cases | Valid | Miss | Late | Median ms |
|---|---:|---:|---:|---:|---:|
| ASK->AM | 30 | 33.33% | 0.00% | 0.00% | 4.00 |
| ASK->FM | 30 | 30.00% | 0.00% | 0.00% | 4.00 |
| ASK->FSK | 30 | 30.00% | 0.00% | 0.00% | 4.00 |
| ASK->PSK | 30 | 33.33% | 0.00% | 0.00% | 4.00 |
| FM->PSK | 30 | 30.00% | 70.00% | 36.67% | 6.00 |
| FSK->FM | 30 | 83.33% | 16.67% | 6.67% | 4.00 |
| FSK->PSK | 30 | 33.33% | 66.67% | 0.00% | 18.00 |
