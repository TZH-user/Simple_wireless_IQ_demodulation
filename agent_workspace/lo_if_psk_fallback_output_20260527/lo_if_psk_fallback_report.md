# LO Bias Sweep And PSK Phase-Jump Fallback Simulation

- Fs: 2.048 MS/s
- Symbol rate: 10.0 kbps
- Raw phase-jump knee: about Rs/4 = 2500.0 Hz
- Low IF sweep: 0, 500, 1000, 1500, 2000, 2250, 2500, 2750, 3000, 4000, 5000, 6000, 8000 Hz

## Conclusion

- ASK/FSK are not the limiting factor for low-IF selection. ASK is poor at exact zero-IF in this model because block DC removal distorts a unipolar envelope, but it becomes stable from about 1 kHz upward.
- The PSK fallback path tested here does not perform full carrier recovery. It only detects symbol-to-symbol phase jumps and toggles state on a detected reversal.
- For 10 kbps BPSK, an uncompensated low IF of Rs/4 = 2.5 kHz creates a 90 degree phase step per symbol, which is the decision knee. Above that point, same-symbol and flipped-symbol phase differences fold into each other after modulo 2*pi.
- The safe PSK fallback region is 0.5~1.5 kHz in this run. A practical shared low-IF target is 1 kHz; 1.5 kHz is still usable, 2 kHz is already marginal, and 5 kHz is unsuitable for this fallback path.

## ASK / FSK Firmware-Like BER Summary

| mod | low_if_hz | mean_ber | worst_ber | pass_rate |
|---|---:|---:|---:|---:|
| ASK | 0 | 0.417400 | 0.492958 | 0.000 |
| ASK | 500 | 0.004695 | 0.021127 | 0.750 |
| ASK | 1000 | 0.000440 | 0.003521 | 1.000 |
| ASK | 1500 | 0.000147 | 0.001761 | 1.000 |
| ASK | 2000 | 0.000000 | 0.000000 | 1.000 |
| ASK | 2250 | 0.000147 | 0.001761 | 1.000 |
| ASK | 2500 | 0.000000 | 0.000000 | 1.000 |
| ASK | 2750 | 0.000000 | 0.000000 | 1.000 |
| ASK | 3000 | 0.000000 | 0.000000 | 1.000 |
| ASK | 4000 | 0.000000 | 0.000000 | 1.000 |
| ASK | 5000 | 0.000000 | 0.000000 | 1.000 |
| ASK | 6000 | 0.000000 | 0.000000 | 1.000 |
| ASK | 8000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 0 | 0.000000 | 0.000000 | 1.000 |
| FSK | 500 | 0.000000 | 0.000000 | 1.000 |
| FSK | 1000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 1500 | 0.000000 | 0.000000 | 1.000 |
| FSK | 2000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 2250 | 0.000000 | 0.000000 | 1.000 |
| FSK | 2500 | 0.000000 | 0.000000 | 1.000 |
| FSK | 2750 | 0.000000 | 0.000000 | 1.000 |
| FSK | 3000 | 0.000147 | 0.001761 | 1.000 |
| FSK | 4000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 5000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 6000 | 0.000000 | 0.000000 | 1.000 |
| FSK | 8000 | 0.000000 | 0.000000 | 1.000 |

## PSK Phase-Jump Fallback Summary

| low_if_hz | mean_precision | mean_recall | mean_f1 | mean_false_alarm | mean_miss | worst_precision | worst_recall | worst_false_alarm |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1.000 | 1.000 | 1.000 | 0.000 | 0.000 | 1.000 | 1.000 | 0.000 |
| 500 | 1.000 | 1.000 | 1.000 | 0.000 | 0.000 | 1.000 | 1.000 | 0.000 |
| 1000 | 0.999 | 0.998 | 0.999 | 0.001 | 0.002 | 0.997 | 0.990 | 0.003 |
| 1500 | 0.981 | 0.984 | 0.982 | 0.021 | 0.016 | 0.943 | 0.952 | 0.063 |
| 2000 | 0.811 | 0.901 | 0.853 | 0.221 | 0.099 | 0.699 | 0.803 | 0.363 |
| 2250 | 0.669 | 0.782 | 0.721 | 0.402 | 0.218 | 0.588 | 0.656 | 0.498 |
| 2500 | 0.487 | 0.576 | 0.528 | 0.631 | 0.424 | 0.446 | 0.500 | 0.741 |
| 2750 | 0.308 | 0.354 | 0.329 | 0.827 | 0.646 | 0.249 | 0.290 | 0.876 |
| 3000 | 0.156 | 0.164 | 0.160 | 0.921 | 0.836 | 0.056 | 0.057 | 0.970 |
| 4000 | 0.000 | 0.000 | 0.000 | 0.999 | 1.000 | 0.000 | 0.000 | 1.000 |
| 5000 | 0.000 | 0.000 | 0.000 | 1.000 | 1.000 | 0.000 | 0.000 | 1.000 |
| 6000 | 0.001 | 0.001 | 0.001 | 0.999 | 0.999 | 0.000 | 0.000 | 1.000 |
| 8000 | 0.841 | 0.922 | 0.879 | 0.183 | 0.078 | 0.756 | 0.831 | 0.291 |

## Algorithm Note

- The fallback detector uses `angle(z_k * conj(z_{k-1}))` on adjacent symbol averages.
- It marks a reversal when the absolute wrapped phase difference is at least pi/2.
- With no carrier compensation, the usable low-IF range scales with symbol rate: keep `abs(low_if) < Rs/4`; leave margin for noise and drift.
