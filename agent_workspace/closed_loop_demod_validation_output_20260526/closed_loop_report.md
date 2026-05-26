# Closed-loop demod validation report

- Generated: 2026-05-26
- Fs: 2.048 MS/s
- RF source output peak: 100.0 mV, ADC target peak: 0.90 V
- Profiles: ideal, board_like_normal, board_like_hard
- SNR: Inf / 20 / 10 dB

## Scope

This run compares three demod paths on the same simulated received IQ data.
- oracle: true channel and modulation parameters are supplied to isolate the demodulator itself.
- fixed: expected mode is selected, but demod parameters stay at nominal defaults.
- identified: mode and parameters are estimated from the impaired IQ record before demod.

The attached DPSK reference project was used as algorithm guidance, not as copied code.
Relevant ideas retained here are: matched-filter style smoothing, eye-center timing from transition phase, square-law BPSK carrier estimation, lightweight decision-directed Costas tracking, and differential decoding as a future option when the transmitter is DPSK rather than ordinary 2PSK.

The identified path also records diagnostic discriminants: envelope depth/floor, envelope tone fraction, FM/FSK discriminator tone fraction, FSK harmonic ratio, FSK two-cluster score, and PSK phase-jump metric.

## Overall summary

| scheme | modulation | cases | pass | pass_rate | mean_ber | mean_corr | mean_nrmse | mode_accuracy |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| oracle | AM | 27 | 15 | 55.56% | NaN | 0.7605 | 0.5547 | 62.96% |
| oracle | FM | 27 | 9 | 33.33% | NaN | 0.6279 | 0.6637 | 66.67% |
| oracle | ASK | 72 | 50 | 69.44% | 0.04779 | NaN | NaN | 97.22% |
| oracle | FSK | 108 | 63 | 58.33% | 0.06254 | NaN | NaN | 66.67% |
| oracle | PSK | 144 | 140 | 97.22% | 0.007061 | NaN | NaN | 87.50% |
| fixed | AM | 27 | 15 | 55.56% | NaN | 0.7605 | 0.5547 | 62.96% |
| fixed | FM | 27 | 9 | 33.33% | NaN | 0.6279 | 0.6637 | 66.67% |
| fixed | ASK | 72 | 50 | 69.44% | 0.04405 | NaN | NaN | 97.22% |
| fixed | FSK | 108 | 57 | 52.78% | 0.1188 | NaN | NaN | 66.67% |
| fixed | PSK | 144 | 124 | 86.11% | 0.05496 | NaN | NaN | 87.50% |
| identified | AM | 27 | 13 | 48.15% | NaN | 0.8363 | 0.4625 | 62.96% |
| identified | FM | 27 | 9 | 33.33% | NaN | 0.739 | 0.5504 | 66.67% |
| identified | ASK | 72 | 47 | 65.28% | 0.07216 | NaN | NaN | 97.22% |
| identified | FSK | 108 | 38 | 35.19% | 0.4154 | NaN | NaN | 66.67% |
| identified | PSK | 144 | 126 | 87.50% | 0.125 | NaN | NaN | 87.50% |

## Practical operating subset

This subset excludes the intentionally hard front-end profile and 10 dB SNR. It is closer to the target bench condition where the signal source is adjusted to a clean 100 mV-level receive chain.

| scheme | modulation | cases | pass | pass_rate | mean_ber | mean_corr | mean_nrmse |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| oracle | AM | 12 | 10 | 83.33% | NaN | 0.8957 | 0.3449 |
| oracle | FM | 12 | 9 | 75.00% | NaN | 0.8295 | 0.4367 |
| oracle | ASK | 32 | 30 | 93.75% | 0.02114 | NaN | NaN |
| oracle | FSK | 48 | 47 | 97.92% | 0.004548 | NaN | NaN |
| oracle | PSK | 64 | 63 | 98.44% | 0.004526 | NaN | NaN |
| fixed | AM | 12 | 10 | 83.33% | NaN | 0.8957 | 0.3449 |
| fixed | FM | 12 | 9 | 75.00% | NaN | 0.8295 | 0.4367 |
| fixed | ASK | 32 | 30 | 93.75% | 0.01073 | NaN | NaN |
| fixed | FSK | 48 | 32 | 66.67% | 0.08997 | NaN | NaN |
| fixed | PSK | 64 | 64 | 100.00% | 0 | NaN | NaN |
| identified | AM | 12 | 8 | 66.67% | NaN | 0.9479 | 0.2566 |
| identified | FM | 12 | 9 | 75.00% | NaN | 0.8295 | 0.4367 |
| identified | ASK | 32 | 28 | 87.50% | 0.07323 | NaN | NaN |
| identified | FSK | 48 | 38 | 79.17% | 0.05231 | NaN | NaN |
| identified | PSK | 64 | 64 | 100.00% | 0 | NaN | NaN |

## Failure split

If oracle passes but identified fails, the simulated signal is theoretically decodable and the main risk is mode/parameter/timing identification. If oracle also fails, the demod path or the front-end condition itself is the limiting factor.

| modulation | cases | oracle_pass_identified_fail | oracle_fail |
| --- | ---: | ---: | ---: |
| AM | 12 | 2 | 2 |
| FM | 12 | 0 | 3 |
| ASK | 32 | 3 | 2 |
| FSK | 48 | 10 | 1 |
| PSK | 64 | 0 | 1 |

## Parameter-identification error highlights

- AM: mode_accuracy 62.96%, mean_symbol_rate_error NaN%, mean_fsk_sep_error NaN%
- FM: mode_accuracy 66.67%, mean_symbol_rate_error NaN%, mean_fsk_sep_error NaN%
- ASK: mode_accuracy 97.22%, mean_symbol_rate_error 84.21%, mean_fsk_sep_error NaN%
- FSK: mode_accuracy 66.67%, mean_symbol_rate_error 140.50%, mean_fsk_sep_error 19.95%
- PSK: mode_accuracy 87.50%, mean_symbol_rate_error 73.68%, mean_fsk_sep_error NaN%

## Notes

- The case set is deterministic but not tuned from one log file.
- Digital BER scoring allows polarity inversion because there is no preamble.
- FSK classification rejects ASK-like near-zero envelope cases before using phase/frequency evidence.
- FSK versus FM separation uses both two-cluster discriminator evidence and square-wave harmonic evidence.
- PSK demod includes a lightweight Costas-style residual phase tracker; static low-IF removal alone is not sufficient in board-like cases.
- A failure in the identified path can be caused by mode identification, parameter estimation, or demod logic.
- A failure in the fixed path mainly indicates that fixed 10 kbps / 20 kHz / low-IF assumptions are not portable.
