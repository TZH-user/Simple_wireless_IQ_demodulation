% rf_iq_h743_low_compute_stage1_20260526.m
% Stage-1 low-compute simulation scaffold for STM32H743XIH6 demod work.
%
% This stage intentionally does not tune demod parameters from validation
% data. It freezes the dataset split, records available H743 resources, and
% builds a conservative cycle-budget model that can be calibrated by DWT logs.

clear; clc; close all;

cfg = struct();
cfg.sample_rate_hz = 2.048e6;
cfg.block_n = 4096;
cfg.cpu_hz = 480e6;
cfg.max_peak_utilization = 0.90;
cfg.peak_symbol_rate_hz = 40e3;
cfg.train_seed_base = 2026052601;
cfg.validation_seed_base = 2026052699;
cfg.output_dir = fullfile(fileparts(mfilename("fullpath")), ...
    "h743_low_compute_stage1_output_20260526");

cfg.train_profiles = ["ideal", "board_like_normal"];
cfg.validation_profiles = ["board_like_validation", "board_like_hard"];
cfg.train_snr_db = [Inf, 20];
cfg.validation_snr_db = [25, 15];
cfg.train_patterns = ["pn9", "loop01"];
cfg.validation_patterns = ["pn11", "bursty"];
cfg.train_rates_hz = [5e3, 10e3, 20e3, 40e3];
cfg.validation_rates_hz = [8e3, 12e3, 30e3];
cfg.low_if_hz = -5e3;

if ~exist(cfg.output_dir, "dir")
    mkdir(cfg.output_dir);
end

manifest = build_dataset_manifest(cfg);
assert_no_validation_leakage(manifest);

budget = build_budget_table(cfg);
calibration = build_calibration_table(cfg);
resources = build_resource_table();

manifest_csv = fullfile(cfg.output_dir, "h743_low_compute_dataset_manifest.csv");
budget_csv = fullfile(cfg.output_dir, "h743_low_compute_budget_model.csv");
calibration_csv = fullfile(cfg.output_dir, "h743_low_compute_perf_calibration_points.csv");
resources_csv = fullfile(cfg.output_dir, "h743_resource_inventory.csv");
report_md = fullfile(cfg.output_dir, "h743_low_compute_stage1_report.md");

writetable(manifest, manifest_csv);
writetable(budget, budget_csv);
writetable(calibration, calibration_csv);
writetable(resources, resources_csv);
write_stage1_report(report_md, manifest, budget, calibration, resources, cfg);

fprintf("H743 low-compute stage-1 scaffold complete.\n");
fprintf("Manifest: %s\n", manifest_csv);
fprintf("Budget model: %s\n", budget_csv);
fprintf("Calibration points: %s\n", calibration_csv);
fprintf("Resource inventory: %s\n", resources_csv);
fprintf("Report: %s\n", report_md);

split_summary = groupcounts(manifest, ["split", "modulation"]);
disp(split_summary);
disp(budget(:, ["modulation", "variant", "estimated_block_cycles", ...
    "util_of_full_budget_pct", "util_of_90pct_cap_pct", "pass_90pct_cap"]));

function manifest = build_dataset_manifest(cfg)
    rows = repmat(empty_case(), 0, 1);
    case_id = 0;

    train_am = [
        5e3, 0.20
        10e3, 0.50
        15e3, 0.80
    ];
    train_fm = [
        5e3, 25e3
        10e3, 75e3
        15e3, 100e3
    ];
    train_fsk = [
        5e3, 10e3
        10e3, 20e3
        20e3, 40e3
        40e3, 80e3
    ];

    validation_am = [
        7e3, 0.35
        12e3, 0.65
    ];
    validation_fm = [
        7e3, 50e3
        12e3, 90e3
    ];
    validation_fsk = [
        8e3, 15e3
        12e3, 30e3
        30e3, 60e3
    ];

    for profile = cfg.train_profiles
        for snr_db = cfg.train_snr_db
            for k = 1:size(train_am, 1)
                [rows, case_id] = add_case(rows, case_id, cfg.train_seed_base, ...
                    "train", "AM", profile, snr_db, cfg.low_if_hz, ...
                    train_am(k, 1), train_am(k, 2), NaN, NaN, NaN, ...
                    "", "training grid");
            end
            for k = 1:size(train_fm, 1)
                [rows, case_id] = add_case(rows, case_id, cfg.train_seed_base, ...
                    "train", "FM", profile, snr_db, cfg.low_if_hz, ...
                    train_fm(k, 1), NaN, train_fm(k, 2), NaN, NaN, ...
                    "", "training grid");
            end
            for rate_hz = cfg.train_rates_hz
                for pattern = cfg.train_patterns
                    [rows, case_id] = add_case(rows, case_id, cfg.train_seed_base, ...
                        "train", "ASK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, rate_hz, NaN, pattern, "training grid");
                    [rows, case_id] = add_case(rows, case_id, cfg.train_seed_base, ...
                        "train", "PSK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, rate_hz, NaN, pattern, "training grid");
                end
            end
            for k = 1:size(train_fsk, 1)
                for pattern = cfg.train_patterns
                    [rows, case_id] = add_case(rows, case_id, cfg.train_seed_base, ...
                        "train", "FSK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, train_fsk(k, 1), train_fsk(k, 2), ...
                        pattern, "training grid");
                end
            end
        end
    end

    for profile = cfg.validation_profiles
        for snr_db = cfg.validation_snr_db
            for k = 1:size(validation_am, 1)
                [rows, case_id] = add_case(rows, case_id, cfg.validation_seed_base, ...
                    "validation", "AM", profile, snr_db, cfg.low_if_hz, ...
                    validation_am(k, 1), validation_am(k, 2), NaN, NaN, NaN, ...
                    "", "held-out AM frequency/depth/profile/SNR");
            end
            for k = 1:size(validation_fm, 1)
                [rows, case_id] = add_case(rows, case_id, cfg.validation_seed_base, ...
                    "validation", "FM", profile, snr_db, cfg.low_if_hz, ...
                    validation_fm(k, 1), NaN, validation_fm(k, 2), NaN, NaN, ...
                    "", "held-out FM frequency/deviation/profile/SNR");
            end
            for rate_hz = cfg.validation_rates_hz
                for pattern = cfg.validation_patterns
                    [rows, case_id] = add_case(rows, case_id, cfg.validation_seed_base, ...
                        "validation", "ASK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, rate_hz, NaN, pattern, ...
                        "held-out digital rate/pattern/profile/SNR");
                    [rows, case_id] = add_case(rows, case_id, cfg.validation_seed_base, ...
                        "validation", "PSK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, rate_hz, NaN, pattern, ...
                        "held-out digital rate/pattern/profile/SNR");
                end
            end
            for k = 1:size(validation_fsk, 1)
                for pattern = cfg.validation_patterns
                    [rows, case_id] = add_case(rows, case_id, cfg.validation_seed_base, ...
                        "validation", "FSK", profile, snr_db, cfg.low_if_hz, ...
                        NaN, NaN, NaN, validation_fsk(k, 1), validation_fsk(k, 2), ...
                        pattern, "held-out FSK rate/separation/profile/SNR");
                end
            end
        end
    end

    manifest = struct2table(rows);
end

function s = empty_case()
    s = struct( ...
        "case_id", 0, ...
        "split", "", ...
        "seed", 0, ...
        "modulation", "", ...
        "profile", "", ...
        "snr_db", NaN, ...
        "low_if_hz", NaN, ...
        "msg_freq_hz", NaN, ...
        "am_depth", NaN, ...
        "fm_deviation_hz", NaN, ...
        "symbol_rate_hz", NaN, ...
        "fsk_sep_hz", NaN, ...
        "pattern", "", ...
        "holdout_note", "");
end

function [rows, case_id] = add_case(rows, case_id, seed_base, split, modulation, ...
    profile, snr_db, low_if_hz, msg_freq_hz, am_depth, fm_deviation_hz, ...
    symbol_rate_hz, fsk_sep_hz, pattern, holdout_note)

    case_id = case_id + 1;
    s = empty_case();
    s.case_id = case_id;
    s.split = string(split);
    s.seed = seed_base + case_id * 17;
    s.modulation = string(modulation);
    s.profile = string(profile);
    s.snr_db = snr_db;
    s.low_if_hz = low_if_hz;
    s.msg_freq_hz = msg_freq_hz;
    s.am_depth = am_depth;
    s.fm_deviation_hz = fm_deviation_hz;
    s.symbol_rate_hz = symbol_rate_hz;
    s.fsk_sep_hz = fsk_sep_hz;
    s.pattern = string(pattern);
    s.holdout_note = string(holdout_note);
    rows(end + 1) = s; %#ok<AGROW>
end

function assert_no_validation_leakage(manifest)
    train = manifest(manifest.split == "train", :);
    validation = manifest(manifest.split == "validation", :);
    train_keys = make_leakage_keys(train);
    validation_keys = make_leakage_keys(validation);
    overlap = intersect(train_keys, validation_keys);
    if ~isempty(overlap)
        error("Validation leakage detected. First overlapping key: %s", overlap(1));
    end
end

function keys = make_leakage_keys(t)
    keys = strings(height(t), 1);
    for i = 1:height(t)
        keys(i) = sprintf("%s|%s|snr=%.12g|low_if=%.12g|msg=%.12g|am=%.12g|fm=%.12g|sym=%.12g|fsk=%.12g|pat=%s", ...
            t.modulation(i), t.profile(i), t.snr_db(i), t.low_if_hz(i), ...
            t.msg_freq_hz(i), t.am_depth(i), t.fm_deviation_hz(i), ...
            t.symbol_rate_hz(i), t.fsk_sep_hz(i), t.pattern(i));
    end
end

function budget = build_budget_table(cfg)
    specs = repmat(empty_budget_row(), 0, 1);
    peak_symbols = ceil(cfg.block_n * cfg.peak_symbol_rate_hz / cfg.sample_rate_hz);

    specs(end + 1) = add_budget_row(cfg, peak_symbols, "AM", ...
        "current_iq_sqrt_envelope", "model", 135, 0, 10000, NaN, ...
        "sqrt envelope cost is kept until measured AM DWT logs are available");
    specs(end + 1) = add_budget_row(cfg, peak_symbols, "AM", ...
        "low_abs_approx_envelope", "candidate", 65, 0, 8000, NaN, ...
        "replace sqrt with max(absI,absQ)+k*min(absI,absQ); tune only on train split");

    specs(end + 1) = add_budget_row(cfg, peak_symbols, "ASK", ...
        "current_envelope_cluster", "model", 145, 300, 10000, NaN, ...
        "envelope plus dynamic cluster; polarity-invariant BER scoring required");
    specs(end + 1) = add_budget_row(cfg, peak_symbols, "ASK", ...
        "low_abs_approx_cluster", "candidate", 75, 180, 8000, NaN, ...
        "low-cost envelope and symbol accumulator; train threshold only on train split");

    specs(end + 1) = add_budget_row(cfg, peak_symbols, "FM", ...
        "current_integer_discriminator", "model", 190, 0, 10000, NaN, ...
        "current task calls integer FM path, not CMSIS atan2 path");
    specs(end + 1) = add_budget_row(cfg, peak_symbols, "FM", ...
        "low_decim2_discriminator", "candidate", 105, 0, 12000, NaN, ...
        "candidate for high-rate stress only; do not replace working FM until DWT proves need");

    specs(end + 1) = add_budget_row(cfg, peak_symbols, "FSK", ...
        "current_decim4_measured", "measured", 0, 0, 0, 723838, ...
        "from fsk output log 20260525 19:46: cycles_last max 723838");
    specs(end + 1) = add_budget_row(cfg, peak_symbols, "FSK", ...
        "low_decim4_no_exact_division", "candidate", 85, 350, 12000, NaN, ...
        "keep decim4 and symbol loop; replace exact normalization/division only if quality holds");

    specs(end + 1) = add_budget_row(cfg, peak_symbols, "PSK", ...
        "current_q15_nco_symbol_trig", "model", 150, 1800, 15000, NaN, ...
        "per-sample Q15 NCO is OK; atan2/cos/sin remain at symbol boundary");
    specs(end + 1) = add_budget_row(cfg, peak_symbols, "PSK", ...
        "low_q15_axis_lut", "candidate", 92, 450, 12000, NaN, ...
        "replace symbol-boundary trig with small LUT or incremental axis vector after train validation");

    budget = struct2table(specs);
end

function s = empty_budget_row()
    s = struct( ...
        "modulation", "", ...
        "variant", "", ...
        "basis", "", ...
        "per_sample_cycles", NaN, ...
        "per_symbol_cycles_at_peak", NaN, ...
        "fixed_block_cycles", NaN, ...
        "measured_override_cycles", NaN, ...
        "estimated_block_cycles", NaN, ...
        "budget_cycles", NaN, ...
        "cap_90pct_cycles", NaN, ...
        "util_of_full_budget_pct", NaN, ...
        "util_of_90pct_cap_pct", NaN, ...
        "pass_90pct_cap", false, ...
        "notes", "");
end

function s = add_budget_row(cfg, peak_symbols, modulation, variant, basis, ...
    per_sample_cycles, per_symbol_cycles, fixed_block_cycles, measured_override_cycles, notes)

    full_budget = cfg.cpu_hz * cfg.block_n / cfg.sample_rate_hz;
    cap_cycles = full_budget * cfg.max_peak_utilization;
    if isnan(measured_override_cycles)
        estimated = fixed_block_cycles + ...
            per_sample_cycles * cfg.block_n + ...
            per_symbol_cycles * peak_symbols;
    else
        estimated = measured_override_cycles;
    end

    s = empty_budget_row();
    s.modulation = string(modulation);
    s.variant = string(variant);
    s.basis = string(basis);
    s.per_sample_cycles = per_sample_cycles;
    s.per_symbol_cycles_at_peak = per_symbol_cycles;
    s.fixed_block_cycles = fixed_block_cycles;
    s.measured_override_cycles = measured_override_cycles;
    s.estimated_block_cycles = estimated;
    s.budget_cycles = full_budget;
    s.cap_90pct_cycles = cap_cycles;
    s.util_of_full_budget_pct = 100.0 * estimated / full_budget;
    s.util_of_90pct_cap_pct = 100.0 * estimated / cap_cycles;
    s.pass_90pct_cap = estimated <= cap_cycles;
    s.notes = string(notes);
end

function calibration = build_calibration_table(cfg)
    full_budget = cfg.cpu_hz * cfg.block_n / cfg.sample_rate_hz;
    rows = repmat(empty_calibration_row(), 0, 1);

    rows(end + 1) = add_calibration_row("fsk_output_202605251946", "FSK", ...
        699427, 704327, 723838, 17074337, full_budget, "high", ...
        "cycles_last is below 90pct cap; cycles_max is polluted by startup/mode-switch history");
    rows(end + 1) = add_calibration_row("serial_auto_20260525_010406", "AM", ...
        2504080, 2506915, 2509750, 18753738, full_budget, "low_stale", ...
        "old/automatic log; use only as warning that current AM needs fresh per-mode DWT capture");
    rows(end + 1) = add_calibration_row("serial_auto_20260525_010406", "FM", ...
        2186530, 7596023, 18414426, 18490406, full_budget, "low_stale", ...
        "old/automatic log; current visual FM is good but needs fresh per-mode DWT capture");

    calibration = struct2table(rows);
end

function s = empty_calibration_row()
    s = struct( ...
        "source", "", ...
        "modulation", "", ...
        "cycles_last_min", NaN, ...
        "cycles_last_avg", NaN, ...
        "cycles_last_max", NaN, ...
        "cycles_max_seen", NaN, ...
        "budget_cycles", NaN, ...
        "util_last_max_pct", NaN, ...
        "trust_level", "", ...
        "notes", "");
end

function s = add_calibration_row(source, modulation, lmin, lavg, lmax, xmax, budget, trust, notes)
    s = empty_calibration_row();
    s.source = string(source);
    s.modulation = string(modulation);
    s.cycles_last_min = lmin;
    s.cycles_last_avg = lavg;
    s.cycles_last_max = lmax;
    s.cycles_max_seen = xmax;
    s.budget_cycles = budget;
    s.util_last_max_pct = 100.0 * lmax / budget;
    s.trust_level = string(trust);
    s.notes = string(notes);
end

function resources = build_resource_table()
    rows = [
        resource_row("CPU", "STM32H743XIH6 Cortex-M7, hard-float ABI", "cycle model assumes 480 MHz SystemCoreClock; confirm from demod_perf budget field", "cmake/gcc-arm-none-eabi.cmake; Core/Src/main.c")
        resource_row("Compiler", "Debug -O0, Release -Os", "Debug DWT is conservative but not final performance acceptance", "cmake/gcc-arm-none-eabi.cmake")
        resource_row("ADC input", "ADC1 PC4 and ADC2 PB1, 14-bit, TIM2 TRGO, DMA circular", "simulation should include I/Q DC, gain/phase imbalance, ADC quantization", "Core/Src/adc.c; Core/Inc/adc.h")
        resource_row("DAC output", "DAC1_OUT2 PA5, TIM2 TRGO, DMA circular, output buffer enabled", "watch DAC late counter and 100mVpp/50ohm normalization", "Core/Src/dac.c; Core/App/Tasks/DemodTask.c")
        resource_row("Timing", "TIM2 external clock mode on PA15/TIM2_ETR, TRGO update", "external sample clock quality matters; include sampling-rate error/jitter in validation", "Core/Src/tim.c")
        resource_row("Cache/DMA", "ICache/DCache enabled, DMA buffers in .dma_buffer", "DCache clean before DAC DMA remains part of timing budget", "Core/Src/main.c; Core/App/Tasks/DemodTask.c")
        resource_row("DSP library", "CMSIS-DSP linked, ARM_MATH_CM7 defined", "FM task currently uses integer path; CMSIS atan2 FM path exists but is not dispatched", "CMakeLists.txt; Core/App/DEMODE/rx_demod.c")
        resource_row("Potential hardware", "FPU and DSP instructions are active; CORDIC is not assumed enabled", "do not base low-compute plan on unverified CORDIC availability", "project scan")
    ];
    resources = struct2table(rows);
end

function s = resource_row(resource, current_state, implication, source)
    s = struct( ...
        "resource", string(resource), ...
        "current_state", string(current_state), ...
        "simulation_implication", string(implication), ...
        "source", string(source));
end

function write_stage1_report(path, manifest, budget, calibration, resources, cfg)
    fid = fopen(path, "w", "n", "UTF-8");
    if fid < 0
        error("Cannot write report: %s", path);
    end
    cleanup = onCleanup(@() fclose(fid));

    full_budget = cfg.cpu_hz * cfg.block_n / cfg.sample_rate_hz;
    cap_cycles = full_budget * cfg.max_peak_utilization;
    train_count = sum(manifest.split == "train");
    validation_count = sum(manifest.split == "validation");

    fprintf(fid, "# H743 Low-Compute Demod Stage-1 Report\n\n");
    fprintf(fid, "Date: 2026-05-26\n\n");
    fprintf(fid, "This is a scaffold step. It freezes the train/validation split and the cycle budget before any low-compute demod tuning is performed.\n\n");

    fprintf(fid, "## Budget\n\n");
    fprintf(fid, "- Sample rate: %.3f MS/s\n", cfg.sample_rate_hz / 1e6);
    fprintf(fid, "- Block size: %u samples\n", cfg.block_n);
    fprintf(fid, "- Assumed SystemCoreClock: %.0f MHz\n", cfg.cpu_hz / 1e6);
    fprintf(fid, "- Full per-block budget: %.0f cycles\n", full_budget);
    fprintf(fid, "- Conservative 90%% peak cap: %.0f cycles\n\n", cap_cycles);

    fprintf(fid, "## Dataset Split\n\n");
    fprintf(fid, "- Train cases: %d\n", train_count);
    fprintf(fid, "- Validation cases: %d\n", validation_count);
    fprintf(fid, "- Leakage guard: train and validation have disjoint parameter/profile/pattern keys.\n");
    fprintf(fid, "- Validation must not be used for threshold or loop-constant tuning. If tuning changes after validation, regenerate a new validation split.\n\n");

    fprintf(fid, "## Cycle Model\n\n");
    fprintf(fid, "| Modulation | Variant | Basis | Est cycles/block | Full budget util | 90%% cap util | Pass cap |\n");
    fprintf(fid, "|---|---|---|---:|---:|---:|---|\n");
    for i = 1:height(budget)
        fprintf(fid, "| %s | %s | %s | %.0f | %.1f%% | %.1f%% | %s |\n", ...
            char(budget.modulation(i)), char(budget.variant(i)), char(budget.basis(i)), ...
            budget.estimated_block_cycles(i), budget.util_of_full_budget_pct(i), ...
            budget.util_of_90pct_cap_pct(i), bool_text(budget.pass_90pct_cap(i)));
    end

    fprintf(fid, "\n## Calibration Points\n\n");
    fprintf(fid, "| Source | Mode | last max | budget | util | trust | Note |\n");
    fprintf(fid, "|---|---|---:|---:|---:|---|---|\n");
    for i = 1:height(calibration)
        fprintf(fid, "| %s | %s | %.0f | %.0f | %.1f%% | %s | %s |\n", ...
            char(calibration.source(i)), char(calibration.modulation(i)), ...
            calibration.cycles_last_max(i), calibration.budget_cycles(i), ...
            calibration.util_last_max_pct(i), char(calibration.trust_level(i)), ...
            char(calibration.notes(i)));
    end

    fprintf(fid, "\n## Resource Inventory\n\n");
    fprintf(fid, "| Resource | Current state | Simulation implication | Source |\n");
    fprintf(fid, "|---|---|---|---|\n");
    for i = 1:height(resources)
        fprintf(fid, "| %s | %s | %s | %s |\n", ...
            char(resources.resource(i)), char(resources.current_state(i)), ...
            char(resources.simulation_implication(i)), char(resources.source(i)));
    end

    fprintf(fid, "\n## Next Step\n\n");
    fprintf(fid, "1. Capture fresh `demod_perf` logs for AM/FM/ASK/FSK/PSK on the same firmware build.\n");
    fprintf(fid, "2. Calibrate the cycle model with those logs, especially AM/FM/PSK where current trustworthy DWT logs are missing.\n");
    fprintf(fid, "3. Implement one low-compute candidate at a time in MATLAB and tune only on the train split.\n");
    fprintf(fid, "4. Run the frozen validation split once the train split passes quality and 90%% peak budget.\n");
end

function txt = bool_text(value)
    if value
        txt = "yes";
    else
        txt = "no";
    end
end
