# H743 Low-Compute Demod Stage-1 Report

Date: 2026-05-26

This is a scaffold step. It freezes the train/validation split and the cycle budget before any low-compute demod tuning is performed.

## Budget

- Sample rate: 2.048 MS/s
- Block size: 4096 samples
- Assumed SystemCoreClock: 480 MHz
- Full per-block budget: 960000 cycles
- Conservative 90% peak cap: 864000 cycles

## Dataset Split

- Train cases: 120
- Validation cases: 88
- Leakage guard: train and validation have disjoint parameter/profile/pattern keys.
- Validation must not be used for threshold or loop-constant tuning. If tuning changes after validation, regenerate a new validation split.

## Cycle Model

| Modulation | Variant | Basis | Est cycles/block | Full budget util | 90% cap util | Pass cap |
|---|---|---|---:|---:|---:|---|
| AM | current_iq_sqrt_envelope | model | 562960 | 58.6% | 65.2% | yes |
| AM | low_abs_approx_envelope | candidate | 274240 | 28.6% | 31.7% | yes |
| ASK | current_envelope_cluster | model | 627920 | 65.4% | 72.7% | yes |
| ASK | low_abs_approx_cluster | candidate | 329600 | 34.3% | 38.1% | yes |
| FM | current_integer_discriminator | model | 788240 | 82.1% | 91.2% | yes |
| FM | low_decim2_discriminator | candidate | 442080 | 46.0% | 51.2% | yes |
| FSK | current_decim4_measured | measured | 723838 | 75.4% | 83.8% | yes |
| FSK | low_decim4_no_exact_division | candidate | 388160 | 40.4% | 44.9% | yes |
| PSK | current_q15_nco_symbol_trig | model | 773400 | 80.6% | 89.5% | yes |
| PSK | low_q15_axis_lut | candidate | 424832 | 44.3% | 49.2% | yes |

## Calibration Points

| Source | Mode | last max | budget | util | trust | Note |
|---|---|---:|---:|---:|---|---|
| fsk_output_202605251946 | FSK | 723838 | 960000 | 75.4% | high | cycles_last is below 90pct cap; cycles_max is polluted by startup/mode-switch history |
| serial_auto_20260525_010406 | AM | 2509750 | 960000 | 261.4% | low_stale | old/automatic log; use only as warning that current AM needs fresh per-mode DWT capture |
| serial_auto_20260525_010406 | FM | 18414426 | 960000 | 1918.2% | low_stale | old/automatic log; current visual FM is good but needs fresh per-mode DWT capture |

## Resource Inventory

| Resource | Current state | Simulation implication | Source |
|---|---|---|---|
| CPU | STM32H743XIH6 Cortex-M7, hard-float ABI | cycle model assumes 480 MHz SystemCoreClock; confirm from demod_perf budget field | cmake/gcc-arm-none-eabi.cmake; Core/Src/main.c |
| Compiler | Debug -O0, Release -Os | Debug DWT is conservative but not final performance acceptance | cmake/gcc-arm-none-eabi.cmake |
| ADC input | ADC1 PC4 and ADC2 PB1, 14-bit, TIM2 TRGO, DMA circular | simulation should include I/Q DC, gain/phase imbalance, ADC quantization | Core/Src/adc.c; Core/Inc/adc.h |
| DAC output | DAC1_OUT2 PA5, TIM2 TRGO, DMA circular, output buffer enabled | watch DAC late counter and 100mVpp/50ohm normalization | Core/Src/dac.c; Core/App/Tasks/DemodTask.c |
| Timing | TIM2 external clock mode on PA15/TIM2_ETR, TRGO update | external sample clock quality matters; include sampling-rate error/jitter in validation | Core/Src/tim.c |
| Cache/DMA | ICache/DCache enabled, DMA buffers in .dma_buffer | DCache clean before DAC DMA remains part of timing budget | Core/Src/main.c; Core/App/Tasks/DemodTask.c |
| DSP library | CMSIS-DSP linked, ARM_MATH_CM7 defined | FM task currently uses integer path; CMSIS atan2 FM path exists but is not dispatched | CMakeLists.txt; Core/App/DEMODE/rx_demod.c |
| Potential hardware | FPU and DSP instructions are active; CORDIC is not assumed enabled | do not base low-compute plan on unverified CORDIC availability | project scan |

## Next Step

1. Capture fresh `demod_perf` logs for AM/FM/ASK/FSK/PSK on the same firmware build.
2. Calibrate the cycle model with those logs, especially AM/FM/PSK where current trustworthy DWT logs are missing.
3. Implement one low-compute candidate at a time in MATLAB and tune only on the train split.
4. Run the frozen validation split once the train split passes quality and 90% peak budget.
