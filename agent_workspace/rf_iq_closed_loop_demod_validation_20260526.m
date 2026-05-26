% rf_iq_closed_loop_demod_validation_20260526.m
% Closed-loop RF IQ simulation:
%   realistic front-end impairments -> lightweight parameter identification
%   -> fixed demod path and identified-parameter demod path.
%
% This script is self-contained on purpose. It does not modify firmware and
% does not write outside the current project agent_workspace.

clear; clc; close all;

cfg = struct();
cfg.seed = 20260526;
cfg.sample_rate_hz = 2.048e6;
cfg.duration_s = 0.018;
cfg.adc_bits = 12;
cfg.adc_full_scale_peak_v = 2.0;
cfg.rf_source_output_peak_v = 100e-3;
cfg.adc_target_peak_v = 0.90;
cfg.source_to_adc_gain = cfg.adc_target_peak_v / cfg.rf_source_output_peak_v;
cfg.nominal_low_if_hz = -5e3;
cfg.nominal_am_freq_hz = 10e3;
cfg.nominal_fm_freq_hz = 10e3;
cfg.nominal_fm_deviation_hz = 75e3;
cfg.nominal_symbol_rate_hz = 10e3;
cfg.nominal_fsk_separation_hz = 20e3;
cfg.digital_rate_candidates_hz = [5e3, 10e3, 20e3, 40e3];
cfg.digital_patterns = ["pn9", "loop01"];
cfg.snr_db_list = [Inf, 20, 10];
cfg.profile_names = ["ideal", "board_like_normal", "board_like_hard"];
cfg.output_dir = fullfile(fileparts(mfilename("fullpath")), ...
    "closed_loop_demod_validation_output_20260526");
cfg.max_plot_cases = 6;

if ~exist(cfg.output_dir, "dir")
    mkdir(cfg.output_dir);
end

rng(cfg.seed);
cases = build_case_list(cfg);
rows = repmat(empty_result_row(), 0, 1);
plot_bank = repmat(empty_plot_item(), 0, 1);

fprintf("Closed-loop demod validation\n");
fprintf("Fs %.3f MS/s, duration %.1f ms, cases %d\n", ...
    cfg.sample_rate_hz / 1e6, cfg.duration_s * 1e3, numel(cases));
fprintf("Output directory: %s\n", cfg.output_dir);

for case_index = 1:numel(cases)
    case_cfg = cases(case_index);
    [iq_adc_v, truth, rx_meta] = synthesize_case_iq(case_cfg, cfg);

    oracle_cfg = make_oracle_demod_config(case_cfg, rx_meta);
    fixed_cfg = make_fixed_demod_config(case_cfg, cfg);
    identified = identify_signal_params(iq_adc_v, cfg);
    dynamic_cfg = make_dynamic_demod_config(identified, cfg);

    oracle_metrics = run_demod_and_score(iq_adc_v, truth, oracle_cfg, cfg);
    fixed_metrics = run_demod_and_score(iq_adc_v, truth, fixed_cfg, cfg);
    dynamic_metrics = run_demod_and_score(iq_adc_v, truth, dynamic_cfg, cfg);

    rows(end + 1) = make_result_row(case_cfg, rx_meta, identified, ...
        oracle_cfg, fixed_cfg, dynamic_cfg, oracle_metrics, fixed_metrics, dynamic_metrics); %#ok<SAGROW>

    if numel(plot_bank) < cfg.max_plot_cases && should_keep_plot_case(case_cfg)
        item = empty_plot_item();
        item.case_cfg = case_cfg;
        item.truth = truth;
        item.iq_adc_v = iq_adc_v;
        item.oracle_cfg = oracle_cfg;
        item.fixed_cfg = fixed_cfg;
        item.dynamic_cfg = dynamic_cfg;
        item.oracle_metrics = oracle_metrics;
        item.fixed_metrics = fixed_metrics;
        item.dynamic_metrics = dynamic_metrics;
        plot_bank(end + 1) = item; %#ok<SAGROW>
    end
end

results = struct2table(rows);
result_csv = fullfile(cfg.output_dir, "closed_loop_case_results.csv");
writetable(results, result_csv);

summary = make_summary_table(results);
summary_csv = fullfile(cfg.output_dir, "closed_loop_summary.csv");
writetable(summary, summary_csv);

report_path = fullfile(cfg.output_dir, "closed_loop_report.md");
write_report(report_path, results, summary, cfg);

plot_summary(summary, cfg.output_dir);
plot_examples(plot_bank, cfg);

fprintf("\nClosed-loop validation complete.\n");
fprintf("Results CSV: %s\n", result_csv);
fprintf("Summary CSV: %s\n", summary_csv);
fprintf("Report: %s\n", report_path);
top_summary = summary(string(summary.profile) == "ALL" & string(summary.snr_db) == "ALL", :);
disp(top_summary(:, ["scheme", "modulation", "total_count", "pass_count", ...
    "pass_rate", "mean_ber", "mean_corr", "mean_nrmse"]));

function cases = build_case_list(cfg)
    cases = repmat(empty_case(), 0, 1);
    case_id = 0;

    analog_am = [
        5e3, 0.20
        10e3, 0.50
        15e3, 0.80
    ];
    analog_fm = [
        5e3, 25e3
        10e3, 75e3
        15e3, 100e3
    ];
    fsk_pairs = [
        5e3, 10e3
        10e3, 20e3
        20e3, 40e3
        40e3, 80e3
        10e3, 10e3
        20e3, 20e3
    ];

    for profile = cfg.profile_names
        for snr_db = cfg.snr_db_list
            for k = 1:size(analog_am, 1)
                case_id = case_id + 1;
                c = empty_case();
                c.case_id = case_id;
                c.modulation = "AM";
                c.profile = profile;
                c.snr_db = snr_db;
                c.low_if_hz = cfg.nominal_low_if_hz;
                c.analog_freq_hz = analog_am(k, 1);
                c.am_depth = analog_am(k, 2);
                c.seed = cfg.seed + case_id * 17;
                cases(end + 1) = c; %#ok<AGROW>
            end

            for k = 1:size(analog_fm, 1)
                case_id = case_id + 1;
                c = empty_case();
                c.case_id = case_id;
                c.modulation = "FM";
                c.profile = profile;
                c.snr_db = snr_db;
                c.low_if_hz = cfg.nominal_low_if_hz;
                c.analog_freq_hz = analog_fm(k, 1);
                c.fm_deviation_hz = analog_fm(k, 2);
                c.seed = cfg.seed + case_id * 17;
                cases(end + 1) = c; %#ok<AGROW>
            end

            for pattern = cfg.digital_patterns
                for rate = cfg.digital_rate_candidates_hz
                    case_id = case_id + 1;
                    c = empty_case();
                    c.case_id = case_id;
                    c.modulation = "ASK";
                    c.profile = profile;
                    c.snr_db = snr_db;
                    c.low_if_hz = cfg.nominal_low_if_hz;
                    c.symbol_rate_hz = rate;
                    c.digital_pattern = pattern;
                    c.seed = cfg.seed + case_id * 17;
                    cases(end + 1) = c; %#ok<AGROW>
                end
            end

            for pattern = cfg.digital_patterns
                for k = 1:size(fsk_pairs, 1)
                    case_id = case_id + 1;
                    c = empty_case();
                    c.case_id = case_id;
                    c.modulation = "FSK";
                    c.profile = profile;
                    c.snr_db = snr_db;
                    c.low_if_hz = cfg.nominal_low_if_hz;
                    c.symbol_rate_hz = fsk_pairs(k, 1);
                    c.fsk_separation_hz = fsk_pairs(k, 2);
                    c.digital_pattern = pattern;
                    c.seed = cfg.seed + case_id * 17;
                    cases(end + 1) = c; %#ok<AGROW>
                end
            end

            for pattern = cfg.digital_patterns
                for rate = cfg.digital_rate_candidates_hz
                    for low_if = [cfg.nominal_low_if_hz, 0]
                        case_id = case_id + 1;
                        c = empty_case();
                        c.case_id = case_id;
                        c.modulation = "PSK";
                        c.profile = profile;
                        c.snr_db = snr_db;
                        c.low_if_hz = low_if;
                        c.symbol_rate_hz = rate;
                        c.digital_pattern = pattern;
                        c.seed = cfg.seed + case_id * 17;
                        cases(end + 1) = c; %#ok<AGROW>
                    end
                end
            end
        end
    end
end

function c = empty_case()
    c = struct();
    c.case_id = 0;
    c.modulation = "";
    c.profile = "";
    c.snr_db = NaN;
    c.low_if_hz = NaN;
    c.analog_freq_hz = NaN;
    c.am_depth = NaN;
    c.fm_deviation_hz = NaN;
    c.symbol_rate_hz = NaN;
    c.fsk_separation_hz = NaN;
    c.digital_pattern = "";
    c.seed = 0;
end

function row = empty_result_row()
    row = struct();
    row.case_id = 0;
    row.modulation = "";
    row.profile = "";
    row.snr_db = NaN;
    row.true_low_if_hz = NaN;
    row.actual_low_if_hz = NaN;
    row.true_analog_freq_hz = NaN;
    row.true_am_depth = NaN;
    row.true_fm_deviation_hz = NaN;
    row.true_symbol_rate_hz = NaN;
    row.true_fsk_separation_hz = NaN;
    row.digital_pattern = "";
    row.adc_clip_rate = NaN;
    row.adc_peak_v = NaN;
    row.tx_rx_freq_error_hz = NaN;
    row.drift_span_hz = NaN;
    row.detected_mode = "";
    row.detected_low_if_hz = NaN;
    row.detected_analog_freq_hz = NaN;
    row.detected_symbol_rate_hz = NaN;
    row.detected_fsk_separation_hz = NaN;
    row.detected_fm_deviation_hz = NaN;
    row.id_env_depth = NaN;
    row.id_env_floor_ratio = NaN;
    row.id_env_tone_fraction = NaN;
    row.id_freq_tone_fraction = NaN;
    row.id_freq_harmonic_ratio = NaN;
    row.id_fsk_score = NaN;
    row.id_fsk_balance = NaN;
    row.id_fsk_activity = NaN;
    row.id_psk_jump_metric = NaN;
    row.mode_correct = false;
    row.symbol_rate_error_pct = NaN;
    row.fsk_sep_error_pct = NaN;
    row.oracle_mode = "";
    row.fixed_mode = "";
    row.dynamic_mode = "";
    row.oracle_ber = NaN;
    row.fixed_ber = NaN;
    row.dynamic_ber = NaN;
    row.oracle_corr = NaN;
    row.fixed_corr = NaN;
    row.dynamic_corr = NaN;
    row.oracle_nrmse = NaN;
    row.fixed_nrmse = NaN;
    row.dynamic_nrmse = NaN;
    row.oracle_pass = false;
    row.fixed_pass = false;
    row.dynamic_pass = false;
    row.oracle_reason = "";
    row.fixed_reason = "";
    row.dynamic_reason = "";
end

function item = empty_plot_item()
    item = struct();
    item.case_cfg = empty_case();
    item.truth = struct();
    item.iq_adc_v = complex([]);
    item.oracle_cfg = struct();
    item.fixed_cfg = struct();
    item.dynamic_cfg = struct();
    item.oracle_metrics = struct();
    item.fixed_metrics = struct();
    item.dynamic_metrics = struct();
end

function [iq_adc_v, truth, meta] = synthesize_case_iq(case_cfg, cfg)
    fs = cfg.sample_rate_hz;
    n = round(cfg.duration_s * fs);
    t = (0:n - 1).' / fs;

    old_rng = rng;
    rng(case_cfg.seed);
    symbol_offset_s = 0;
    if isfinite(case_cfg.symbol_rate_hz)
        symbol_offset_s = rand() / case_cfg.symbol_rate_hz;
    end

    [iq_unit, truth] = make_modulated_iq(case_cfg, t, symbol_offset_s, fs);
    iq_source_v = cfg.rf_source_output_peak_v * iq_unit ./ max(max(abs(iq_unit)), eps);
    [iq_rx_v, meta] = apply_receiver_impairments(iq_source_v, t, case_cfg, cfg);
    iq_adc_v = quantize_complex_adc(iq_rx_v, cfg.adc_bits, cfg.adc_full_scale_peak_v);
    meta.adc_clip_rate = mean(abs(real(iq_rx_v)) >= cfg.adc_full_scale_peak_v | ...
        abs(imag(iq_rx_v)) >= cfg.adc_full_scale_peak_v);

    truth.t = t;
    truth.sample_rate_hz = fs;
    truth.symbol_offset_s = symbol_offset_s;
    truth.modulation = case_cfg.modulation;
    truth.low_if_hz = case_cfg.low_if_hz;
    truth.actual_low_if_hz = meta.actual_low_if_hz;
    rng(old_rng);
end

function [iq, truth] = make_modulated_iq(case_cfg, t, symbol_offset_s, fs)
    truth = struct();
    truth.baseband = zeros(size(t));
    truth.sample_bits = false(size(t));
    truth.bit_source = false(0, 1);

    switch char(case_cfg.modulation)
        case "AM"
            msg = cos(2 * pi * case_cfg.analog_freq_hz * t);
            env = 1 + case_cfg.am_depth * msg;
            iq = env .* exp(1j * 2 * pi * case_cfg.low_if_hz * t);
            truth.baseband = msg;

        case "FM"
            msg = cos(2 * pi * case_cfg.analog_freq_hz * t);
            beta = case_cfg.fm_deviation_hz / max(case_cfg.analog_freq_hz, eps);
            phase_mod = beta * sin(2 * pi * case_cfg.analog_freq_hz * t);
            iq = exp(1j * (2 * pi * case_cfg.low_if_hz * t + phase_mod));
            truth.baseband = msg;

        case "ASK"
            [bits, bit_source] = make_bits_for_time(t, case_cfg.symbol_rate_hz, symbol_offset_s, case_cfg.digital_pattern);
            amp = double(bits);
            iq = amp .* exp(1j * 2 * pi * case_cfg.low_if_hz * t);
            truth.sample_bits = bits;
            truth.bit_source = bit_source;

        case "FSK"
            [bits, bit_source] = make_bits_for_time(t, case_cfg.symbol_rate_hz, symbol_offset_s, case_cfg.digital_pattern);
            inst_freq_hz = case_cfg.low_if_hz + case_cfg.fsk_separation_hz * double(bits);
            phase = cumsum(2 * pi * inst_freq_hz / fs);
            iq = exp(1j * phase);
            truth.sample_bits = bits;
            truth.bit_source = bit_source;

        case "PSK"
            [bits, bit_source] = make_bits_for_time(t, case_cfg.symbol_rate_hz, symbol_offset_s, case_cfg.digital_pattern);
            phase_bits = pi * double(bits);
            iq = exp(1j * (2 * pi * case_cfg.low_if_hz * t + phase_bits));
            truth.sample_bits = bits;
            truth.bit_source = bit_source;

        otherwise
            error("Unknown modulation: %s", case_cfg.modulation);
    end
end

function [bits, bit_source] = make_bits_for_time(t, bit_rate_hz, symbol_offset_s, pattern)
    bit_index = floor((t + symbol_offset_s) * bit_rate_hz) + 1;
    bit_count = max(bit_index) + 4;
    bit_source = make_bit_source(bit_count, pattern);
    bits = bit_source(bit_index);
end

function bits = make_bit_source(count, pattern)
    switch string(pattern)
        case "loop01"
            bits = mod((1:count).', 2) ~= 0;
        otherwise
            bits = pn9_bits(count);
    end
end

function bits = pn9_bits(count)
    reg = uint16(hex2dec("1FF"));
    bits = false(count, 1);
    for k = 1:count
        bits(k) = bitand(reg, 1) ~= 0;
        new_bit = bitxor(bitand(reg, 1), bitand(bitshift(reg, -5), 1));
        reg = bitor(bitshift(reg, -1), bitshift(uint16(new_bit), 8));
        reg = bitand(reg, uint16(hex2dec("1FF")));
        if reg == 0
            reg = uint16(hex2dec("1FF"));
        end
    end
end

function [y, meta] = apply_receiver_impairments(x, t, case_cfg, cfg)
    profile = make_receiver_profile(case_cfg.profile);
    y = x;
    relative_t = t - t(1);
    span_s = max(relative_t(end), eps);

    freq_error_hz = uniform_range(profile.tx_rx_freq_error_range_hz);
    drift_span_hz = symmetric_random(profile.tx_rx_freq_drift_span_hz);
    drift_rate_hz_per_s = drift_span_hz / span_s;

    if freq_error_hz ~= 0 || drift_span_hz ~= 0
        phase_shift = 2 * pi * (freq_error_hz * relative_t + ...
            0.5 * drift_rate_hz_per_s * relative_t.^2);
        y = y .* exp(1j * phase_shift);
    end

    if profile.ripple_depth > 0
        ripple = 1 + profile.ripple_depth * cos(2 * pi * profile.ripple_freq_hz * t + 2 * pi * rand());
        y = y .* ripple;
    end

    if isfinite(profile.image_leakage_db)
        image_gain = 10^(profile.image_leakage_db / 20);
        y = y + image_gain * exp(1j * 2 * pi * rand()) .* conj(y);
    end

    sig_rms = sqrt(mean(abs(y).^2));
    if profile.dc_offset_ratio > 0 && sig_rms > 0
        y = y + profile.dc_offset_ratio * sig_rms * exp(1j * 2 * pi * rand());
    end

    y = cfg.source_to_adc_gain * profile.adc_drive_gain * y;
    y = add_awgn(y, case_cfg.snr_db);

    meta = struct();
    meta.actual_low_if_hz = case_cfg.low_if_hz + freq_error_hz + 0.5 * drift_span_hz;
    meta.tx_rx_freq_error_hz = freq_error_hz;
    meta.drift_span_hz = drift_span_hz;
    meta.adc_peak_v = max(abs(y));
    meta.adc_rms_v = sqrt(mean(abs(y).^2));
    meta.adc_clip_rate = 0;
end

function profile = make_receiver_profile(name)
    profile = struct();
    profile.tx_rx_freq_error_range_hz = [0, 0];
    profile.tx_rx_freq_drift_span_hz = 0;
    profile.image_leakage_db = -Inf;
    profile.ripple_depth = 0;
    profile.ripple_freq_hz = 1000;
    profile.dc_offset_ratio = 0;
    profile.adc_drive_gain = 1.0;

    switch string(name)
        case "ideal"
            return;
        case "board_like_normal"
            profile.tx_rx_freq_error_range_hz = [-3.0e3, 3.0e3];
            profile.tx_rx_freq_drift_span_hz = 120;
            profile.image_leakage_db = -12;
            profile.ripple_depth = 0.03;
            profile.ripple_freq_hz = 1.5e3;
            profile.dc_offset_ratio = 0.010;
            profile.adc_drive_gain = 1.00;
        case "board_like_hard"
            profile.tx_rx_freq_error_range_hz = [-10.0e3, 10.0e3];
            profile.tx_rx_freq_drift_span_hz = 300;
            profile.image_leakage_db = -6;
            profile.ripple_depth = 0.08;
            profile.ripple_freq_hz = 1.0e3;
            profile.dc_offset_ratio = 0.030;
            profile.adc_drive_gain = 1.15;
        otherwise
            error("Unknown profile: %s", name);
    end
end

function y = add_awgn(x, snr_db)
    if isinf(snr_db)
        y = x;
        return;
    end
    p = mean(abs(x).^2);
    np = p / (10^(snr_db / 10));
    noise = sqrt(np / 2) * (randn(size(x)) + 1j * randn(size(x)));
    y = x + noise;
end

function yq = quantize_complex_adc(y, bits, full_scale_peak_v)
    yq = quantize_real_adc(real(y), bits, full_scale_peak_v) + ...
        1j * quantize_real_adc(imag(y), bits, full_scale_peak_v);
end

function xq = quantize_real_adc(x, bits, full_scale_peak_v)
    levels = 2^bits;
    x_clip = min(max(x, -full_scale_peak_v), full_scale_peak_v);
    code = round((x_clip + full_scale_peak_v) / (2 * full_scale_peak_v) * (levels - 1));
    xq = (code / (levels - 1)) * (2 * full_scale_peak_v) - full_scale_peak_v;
end

function demod_cfg = make_fixed_demod_config(case_cfg, cfg)
    demod_cfg = struct();
    demod_cfg.scheme = "fixed";
    demod_cfg.mode = case_cfg.modulation;
    demod_cfg.low_if_hz = cfg.nominal_low_if_hz;
    demod_cfg.analog_freq_hz = cfg.nominal_am_freq_hz;
    demod_cfg.fm_deviation_hz = cfg.nominal_fm_deviation_hz;
    demod_cfg.symbol_rate_hz = cfg.nominal_symbol_rate_hz;
    demod_cfg.fsk_separation_hz = cfg.nominal_fsk_separation_hz;
    demod_cfg.timing_offset_samples = NaN;

    if case_cfg.modulation == "FM"
        demod_cfg.analog_freq_hz = cfg.nominal_fm_freq_hz;
    elseif case_cfg.modulation == "PSK"
        demod_cfg.low_if_hz = 0;
    end
end

function demod_cfg = make_oracle_demod_config(case_cfg, rx_meta)
    demod_cfg = struct();
    demod_cfg.scheme = "oracle";
    demod_cfg.mode = case_cfg.modulation;
    demod_cfg.low_if_hz = rx_meta.actual_low_if_hz;
    demod_cfg.analog_freq_hz = case_cfg.analog_freq_hz;
    demod_cfg.fm_deviation_hz = case_cfg.fm_deviation_hz;
    demod_cfg.symbol_rate_hz = case_cfg.symbol_rate_hz;
    demod_cfg.fsk_separation_hz = case_cfg.fsk_separation_hz;
    demod_cfg.timing_offset_samples = NaN;

    if ~isfinite(demod_cfg.low_if_hz)
        demod_cfg.low_if_hz = case_cfg.low_if_hz;
    end
    if ~isfinite(demod_cfg.symbol_rate_hz)
        demod_cfg.symbol_rate_hz = 10e3;
    end
    if ~isfinite(demod_cfg.fsk_separation_hz)
        demod_cfg.fsk_separation_hz = 20e3;
    end
end

function demod_cfg = make_dynamic_demod_config(id, cfg)
    demod_cfg = struct();
    demod_cfg.scheme = "identified";
    demod_cfg.mode = id.mode;
    demod_cfg.low_if_hz = id.low_if_hz;
    demod_cfg.analog_freq_hz = id.analog_freq_hz;
    demod_cfg.fm_deviation_hz = id.fm_deviation_hz;
    demod_cfg.symbol_rate_hz = id.symbol_rate_hz;
    demod_cfg.fsk_separation_hz = id.fsk_separation_hz;
    demod_cfg.timing_offset_samples = id.timing_offset_samples;

    if ~isfinite(demod_cfg.low_if_hz)
        demod_cfg.low_if_hz = cfg.nominal_low_if_hz;
    end
    if ~isfinite(demod_cfg.symbol_rate_hz)
        demod_cfg.symbol_rate_hz = cfg.nominal_symbol_rate_hz;
    end
    if ~isfinite(demod_cfg.fsk_separation_hz)
        demod_cfg.fsk_separation_hz = cfg.nominal_fsk_separation_hz;
    end
end

function id = identify_signal_params(iq, cfg)
    fs = cfg.sample_rate_hz;
    z_raw = iq(:);
    z = z_raw - mean(z_raw);
    env = abs(z);
    env_ac = env - mean(env);
    [env_peak_hz, env_tone_fraction] = dominant_tone_freq(env_ac, fs, 2e3, 80e3);
    env_p95 = percentile_local(env, 95);
    env_p05 = percentile_local(env, 5);
    env_depth = (env_p95 - env_p05) / max(env_p95 + env_p05, eps);
    env_floor_ratio = env_p05 / max(env_p95, eps);

    phase_step = angle(z(2:end) .* conj(z(1:end - 1)));
    phase_step_psk = angle(z_raw(2:end) .* conj(z_raw(1:end - 1)));
    inst_freq = phase_step * fs / (2 * pi);
    inst_freq = clip_vector(inst_freq, -250e3, 250e3);
    inst_smooth = movmean(inst_freq, max(3, round(fs / 200e3)));
    inst_ac = inst_smooth - median_local(inst_smooth);
    [freq_tone_hz, freq_tone_fraction] = dominant_tone_freq(inst_ac, fs, 2e3, 100e3);
    freq_harmonic_ratio = harmonic_power_ratio(inst_ac, fs, freq_tone_hz);
    [fsk_low_hz, fsk_high_hz, fsk_score, fsk_balance] = two_level_split(inst_smooth);
    fsk_sep_hz = abs(fsk_high_hz - fsk_low_hz);
    fsk_activity = mean(abs(inst_smooth - median_local(inst_smooth)) > max(1.5e3, 0.25 * fsk_sep_hz));
    is_fsk_candidate = env_floor_ratio > 0.16 && fsk_sep_hz > 6e3 && fsk_activity > 0.18 && fsk_balance > 0.12 && ...
        (fsk_score > 4.8 || ...
        (fsk_score > 2.7 && freq_harmonic_ratio > 0.03) || ...
        (fsk_score > 3.0 && freq_tone_fraction < 0.10));

    low_if_hz = delay_frequency_estimate(z, fs, 16);
    psk_low_if_hz = 0.5 * delay_frequency_estimate(z_raw.^2, fs, 16);
    phase_without_carrier = wrap_pi(phase_step_psk - 2 * pi * psk_low_if_hz / fs);
    psk_jump_metric = mean(abs(phase_without_carrier) > 1.2);

    id = struct();
    id.mode = "UNKNOWN";
    id.low_if_hz = low_if_hz;
    id.analog_freq_hz = env_peak_hz;
    id.fm_deviation_hz = NaN;
    id.symbol_rate_hz = NaN;
    id.fsk_separation_hz = NaN;
    id.timing_offset_samples = NaN;
    id.env_depth = env_depth;
    id.env_floor_ratio = env_floor_ratio;
    id.env_tone_fraction = env_tone_fraction;
    id.freq_tone_fraction = freq_tone_fraction;
    id.freq_harmonic_ratio = freq_harmonic_ratio;
    id.fsk_score = fsk_score;
    id.fsk_balance = fsk_balance;
    id.fsk_activity = fsk_activity;
    id.psk_jump_metric = psk_jump_metric;

    if psk_jump_metric > 8e-4 && env_floor_ratio > 0.25 && env_depth < 0.85
        id.mode = "PSK";
        id.low_if_hz = psk_low_if_hz;
        zbb = nco_compensate(z_raw, fs, id.low_if_hz);
        proj = psk_projector(zbb);
        binary = proj > 0;
        [id.symbol_rate_hz, id.timing_offset_samples] = estimate_symbol_rate(binary, fs, cfg.digital_rate_candidates_hz);
    elseif is_fsk_candidate
        id.mode = "FSK";
        threshold = 0.5 * (fsk_low_hz + fsk_high_hz);
        binary = inst_smooth > threshold;
        [id.symbol_rate_hz, id.timing_offset_samples] = estimate_symbol_rate(binary, fs, cfg.digital_rate_candidates_hz);
        id.fsk_separation_hz = fsk_sep_hz;
        id.low_if_hz = min(fsk_low_hz, fsk_high_hz);
    elseif env_depth > 0.42 && env_tone_fraction > 0.32
        id.mode = "AM";
        id.analog_freq_hz = env_peak_hz;
    elseif env_depth > 0.55
        id.mode = "ASK";
        binary = env > 0.5 * (env_p95 + env_p05);
        [id.symbol_rate_hz, id.timing_offset_samples] = estimate_symbol_rate(binary, fs, cfg.digital_rate_candidates_hz);
        id.low_if_hz = low_if_hz;
    elseif std(inst_ac) > 2e3 && freq_tone_fraction > 0.08
        id.mode = "FM";
        id.analog_freq_hz = freq_tone_hz;
        id.fm_deviation_hz = 0.5 * (percentile_local(inst_smooth, 95) - percentile_local(inst_smooth, 5));
        id.low_if_hz = low_if_hz;
    elseif env_depth > 0.30
        id.mode = "ASK";
        binary = env > 0.5 * (env_p95 + env_p05);
        [id.symbol_rate_hz, id.timing_offset_samples] = estimate_symbol_rate(binary, fs, cfg.digital_rate_candidates_hz);
    else
        id.mode = "PSK";
        id.low_if_hz = psk_low_if_hz;
        zbb = nco_compensate(z_raw, fs, id.low_if_hz);
        proj = psk_projector(zbb);
        binary = proj > 0;
        [id.symbol_rate_hz, id.timing_offset_samples] = estimate_symbol_rate(binary, fs, cfg.digital_rate_candidates_hz);
    end
end

function metrics = run_demod_and_score(iq, truth, demod_cfg, cfg)
    metrics = struct();
    metrics.ber = NaN;
    metrics.corr = NaN;
    metrics.nrmse = NaN;
    metrics.pass = false;
    metrics.reason = "";
    metrics.waveform = [];

    expected = string(truth.modulation);
    mode = string(demod_cfg.mode);

    if expected == "AM"
        if mode ~= "AM"
            metrics.reason = "mode mismatch";
            return;
        end
        y = demod_am(iq);
        metrics = score_analog(y, truth.baseband, cfg, expected);
        return;
    end

    if expected == "FM"
        if mode ~= "FM"
            metrics.reason = "mode mismatch";
            return;
        end
        y = demod_fm(iq, cfg.sample_rate_hz);
        metrics = score_analog(y, truth.baseband, cfg, expected);
        return;
    end

    if expected == "ASK"
        if ~(mode == "ASK" || mode == "AM")
            metrics.reason = "mode mismatch";
            metrics.ber = 1;
            return;
        end
        [decoded, sample_idx, waveform] = demod_ask_bits(iq, demod_cfg, cfg);
        metrics = score_bits(decoded, truth.sample_bits(sample_idx), cfg, expected);
        metrics.waveform = waveform;
        return;
    end

    if expected == "FSK"
        if mode ~= "FSK"
            metrics.reason = "mode mismatch";
            metrics.ber = 1;
            return;
        end
        [decoded, sample_idx, waveform] = demod_fsk_bits(iq, demod_cfg, cfg);
        metrics = score_bits(decoded, truth.sample_bits(sample_idx), cfg, expected);
        metrics.waveform = waveform;
        return;
    end

    if expected == "PSK"
        if mode ~= "PSK"
            metrics.reason = "mode mismatch";
            metrics.ber = 1;
            return;
        end
        [decoded, sample_idx, waveform] = demod_psk_bits(iq, demod_cfg, cfg);
        metrics = score_bits(decoded, truth.sample_bits(sample_idx), cfg, expected);
        metrics.waveform = waveform;
        return;
    end
end

function y = demod_am(iq)
    z = iq(:) - mean(iq(:));
    env = abs(z);
    y = env - movmean(env, max(8, floor(numel(env) / 64)));
end

function y = demod_fm(iq, fs)
    z = iq(:) - mean(iq(:));
    step = angle(z(2:end) .* conj(z(1:end - 1))) * fs / (2 * pi);
    step = [step(1); step(:)];
    y = step - movmean(step, max(8, floor(fs / 2000)));
end

function [bits, sample_idx, waveform] = demod_ask_bits(iq, demod_cfg, cfg)
    env = abs(iq(:) - mean(iq(:)));
    env = movmean(env, max(1, round(cfg.sample_rate_hz / max(demod_cfg.symbol_rate_hz * 6, 1))));
    th = 0.5 * (percentile_local(env, 15) + percentile_local(env, 85));
    sample_idx = symbol_sample_indices(numel(env), demod_cfg, cfg);
    bits = env(sample_idx) > th;
    waveform = double(env > th);
end

function [bits, sample_idx, waveform] = demod_fsk_bits(iq, demod_cfg, cfg)
    fs = cfg.sample_rate_hz;
    z = iq(:) - mean(iq(:));
    inst = angle(z(2:end) .* conj(z(1:end - 1))) * fs / (2 * pi);
    inst = [inst(1); inst(:)];
    inst = movmean(inst, max(2, round(fs / max(demod_cfg.symbol_rate_hz * 8, 1))));
    if isfield(demod_cfg, "scheme") && string(demod_cfg.scheme) == "identified"
        [lo, hi, score, balance] = two_level_split(inst); %#ok<ASGLU>
        if isfinite(lo) && isfinite(hi) && score > 2.0 && balance > 0.10
            th = 0.5 * (lo + hi);
        else
            th = demod_cfg.low_if_hz + 0.5 * demod_cfg.fsk_separation_hz;
        end
    else
        th = demod_cfg.low_if_hz + 0.5 * demod_cfg.fsk_separation_hz;
    end
    sample_idx = symbol_sample_indices(numel(inst), demod_cfg, cfg);
    bits = inst(sample_idx) > th;
    waveform = double(inst > th);
end

function [bits, sample_idx, waveform] = demod_psk_bits(iq, demod_cfg, cfg)
    fs = cfg.sample_rate_hz;
    z = iq(:);
    zbb = nco_compensate(z, fs, demod_cfg.low_if_hz);
    proj = bpsk_costas_projector(zbb, fs, demod_cfg.symbol_rate_hz);
    proj = movmean(proj, max(1, round(fs / max(demod_cfg.symbol_rate_hz * 10, 1))));
    sample_idx = symbol_sample_indices(numel(proj), demod_cfg, cfg);
    bits = proj(sample_idx) > 0;
    waveform = double(proj > 0);
end

function idx = symbol_sample_indices(n, demod_cfg, cfg)
    rate = max(double(demod_cfg.symbol_rate_hz), 1);
    spb = cfg.sample_rate_hz / rate;
    if isfield(demod_cfg, "timing_offset_samples") && isfinite(demod_cfg.timing_offset_samples)
        offset = demod_cfg.timing_offset_samples + 0.5 * spb;
    else
        offset = 0.5 * spb;
    end
    idx = round(offset:spb:n).';
    idx = idx(idx >= 1 & idx <= n);
    if numel(idx) < 8
        idx = round((0.5 * spb):spb:n).';
        idx = idx(idx >= 1 & idx <= n);
    end
end

function metrics = score_analog(y, ref, cfg, expected)
    y = y(:);
    ref = ref(:);
    n = min(numel(y), numel(ref));
    y = y(1:n);
    ref = ref(1:n);
    y = y - mean(y);
    ref = ref - mean(ref);
    max_lag = round(cfg.sample_rate_hz / 2000);
    [corr_val, aligned_y, aligned_ref] = best_abs_corr(y, ref, max_lag);
    gain = (aligned_y' * aligned_ref) / max(aligned_y' * aligned_y, eps);
    err = gain * aligned_y - aligned_ref;
    nrmse = sqrt(mean(err.^2)) / max(rms_local(aligned_ref), eps);

    metrics = struct();
    metrics.ber = NaN;
    metrics.corr = corr_val;
    metrics.nrmse = nrmse;
    metrics.waveform = y;

    if expected == "AM"
        pass = corr_val >= 0.75 && nrmse <= 0.80;
    else
        pass = corr_val >= 0.80 && nrmse <= 0.75;
    end

    metrics.pass = pass;
    if pass
        metrics.reason = "pass";
    else
        metrics.reason = sprintf("corr %.3f nrmse %.3f", corr_val, nrmse);
    end
end

function metrics = score_bits(decoded, truth_bits, cfg, expected)
    decoded = logical(decoded(:));
    truth_bits = logical(truth_bits(:));
    n = min(numel(decoded), numel(truth_bits));
    decoded = decoded(1:n);
    truth_bits = truth_bits(1:n);
    best = best_ber_with_shift(truth_bits, decoded, -5:5);

    metrics = struct();
    metrics.ber = best.ber;
    metrics.corr = NaN;
    metrics.nrmse = NaN;
    metrics.waveform = [];

    if expected == "PSK"
        threshold = 0.08;
    elseif expected == "FSK"
        threshold = 0.05;
    else
        threshold = 0.03;
    end
    if n < 20
        threshold = 0.20;
    end
    metrics.pass = best.ber <= threshold;
    if metrics.pass
        metrics.reason = "pass";
    else
        metrics.reason = sprintf("ber %.4f > %.4f", best.ber, threshold);
    end
end

function row = make_result_row(case_cfg, rx_meta, id, oracle_cfg, fixed_cfg, dynamic_cfg, oracle_metrics, fixed_metrics, dynamic_metrics)
    row = empty_result_row();
    row.case_id = case_cfg.case_id;
    row.modulation = case_cfg.modulation;
    row.profile = case_cfg.profile;
    row.snr_db = case_cfg.snr_db;
    row.true_low_if_hz = case_cfg.low_if_hz;
    row.actual_low_if_hz = rx_meta.actual_low_if_hz;
    row.true_analog_freq_hz = case_cfg.analog_freq_hz;
    row.true_am_depth = case_cfg.am_depth;
    row.true_fm_deviation_hz = case_cfg.fm_deviation_hz;
    row.true_symbol_rate_hz = case_cfg.symbol_rate_hz;
    row.true_fsk_separation_hz = case_cfg.fsk_separation_hz;
    row.digital_pattern = case_cfg.digital_pattern;
    row.adc_clip_rate = rx_meta.adc_clip_rate;
    row.adc_peak_v = rx_meta.adc_peak_v;
    row.tx_rx_freq_error_hz = rx_meta.tx_rx_freq_error_hz;
    row.drift_span_hz = rx_meta.drift_span_hz;
    row.detected_mode = id.mode;
    row.detected_low_if_hz = id.low_if_hz;
    row.detected_analog_freq_hz = id.analog_freq_hz;
    row.detected_symbol_rate_hz = id.symbol_rate_hz;
    row.detected_fsk_separation_hz = id.fsk_separation_hz;
    row.detected_fm_deviation_hz = id.fm_deviation_hz;
    row.id_env_depth = id.env_depth;
    row.id_env_floor_ratio = id.env_floor_ratio;
    row.id_env_tone_fraction = id.env_tone_fraction;
    row.id_freq_tone_fraction = id.freq_tone_fraction;
    row.id_freq_harmonic_ratio = id.freq_harmonic_ratio;
    row.id_fsk_score = id.fsk_score;
    row.id_fsk_balance = id.fsk_balance;
    row.id_fsk_activity = id.fsk_activity;
    row.id_psk_jump_metric = id.psk_jump_metric;
    row.mode_correct = mode_matches(case_cfg.modulation, id.mode);
    row.symbol_rate_error_pct = percent_error(id.symbol_rate_hz, case_cfg.symbol_rate_hz);
    row.fsk_sep_error_pct = percent_error(id.fsk_separation_hz, case_cfg.fsk_separation_hz);
    row.oracle_mode = oracle_cfg.mode;
    row.fixed_mode = fixed_cfg.mode;
    row.dynamic_mode = dynamic_cfg.mode;
    row.oracle_ber = oracle_metrics.ber;
    row.fixed_ber = fixed_metrics.ber;
    row.dynamic_ber = dynamic_metrics.ber;
    row.oracle_corr = oracle_metrics.corr;
    row.fixed_corr = fixed_metrics.corr;
    row.dynamic_corr = dynamic_metrics.corr;
    row.oracle_nrmse = oracle_metrics.nrmse;
    row.fixed_nrmse = fixed_metrics.nrmse;
    row.dynamic_nrmse = dynamic_metrics.nrmse;
    row.oracle_pass = oracle_metrics.pass;
    row.fixed_pass = fixed_metrics.pass;
    row.dynamic_pass = dynamic_metrics.pass;
    row.oracle_reason = oracle_metrics.reason;
    row.fixed_reason = fixed_metrics.reason;
    row.dynamic_reason = dynamic_metrics.reason;
end

function ok = mode_matches(expected, detected)
    expected = string(expected);
    detected = string(detected);
    ok = expected == detected || (expected == "ASK" && detected == "AM");
end

function e = percent_error(est, truth)
    if ~isfinite(est) || ~isfinite(truth) || truth == 0
        e = NaN;
    else
        e = 100 * abs(est - truth) / abs(truth);
    end
end

function summary = make_summary_table(results)
    schemes = ["oracle", "fixed", "identified"];
    mods = unique(string(results.modulation), "stable");
    rows = repmat(empty_summary_row(), 0, 1);

    for scheme = schemes
        for mod = mods.'
            mask = string(results.modulation) == mod;
            rows(end + 1) = summarize_subset(results, scheme, mod, "ALL", "ALL", mask); %#ok<AGROW>
        end
    end

    profiles = unique(string(results.profile), "stable");
    for scheme = schemes
        for mod = mods.'
            for profile = profiles.'
                mask = string(results.modulation) == mod & string(results.profile) == profile;
                rows(end + 1) = summarize_subset(results, scheme, mod, profile, "ALL", mask); %#ok<AGROW>
            end
        end
    end

    snrs = unique(results.snr_db, "stable");
    for scheme = schemes
        for mod = mods.'
            for snr = snrs.'
                if isinf(snr)
                    snr_label = "Inf";
                    mask_snr = isinf(results.snr_db);
                else
                    snr_label = string(snr);
                    mask_snr = results.snr_db == snr;
                end
                mask = string(results.modulation) == mod & mask_snr;
                rows(end + 1) = summarize_subset(results, scheme, mod, "ALL", snr_label, mask); %#ok<AGROW>
            end
        end
    end

    summary = struct2table(rows);
end

function row = empty_summary_row()
    row = struct();
    row.scheme = "";
    row.modulation = "";
    row.profile = "";
    row.snr_db = "";
    row.total_count = 0;
    row.pass_count = 0;
    row.pass_rate = NaN;
    row.mean_ber = NaN;
    row.mean_corr = NaN;
    row.mean_nrmse = NaN;
    row.mode_accuracy = NaN;
end

function row = summarize_subset(results, scheme, mod, profile, snr_label, mask)
    row = empty_summary_row();
    row.scheme = scheme;
    row.modulation = mod;
    row.profile = profile;
    row.snr_db = snr_label;
    row.total_count = sum(mask);
    if row.total_count == 0
        return;
    end
    if scheme == "oracle"
        pass_vec = results.oracle_pass(mask);
        ber_vec = results.oracle_ber(mask);
        corr_vec = results.oracle_corr(mask);
        nrmse_vec = results.oracle_nrmse(mask);
    elseif scheme == "fixed"
        pass_vec = results.fixed_pass(mask);
        ber_vec = results.fixed_ber(mask);
        corr_vec = results.fixed_corr(mask);
        nrmse_vec = results.fixed_nrmse(mask);
    else
        pass_vec = results.dynamic_pass(mask);
        ber_vec = results.dynamic_ber(mask);
        corr_vec = results.dynamic_corr(mask);
        nrmse_vec = results.dynamic_nrmse(mask);
    end
    row.pass_count = sum(pass_vec);
    row.pass_rate = row.pass_count / row.total_count;
    row.mean_ber = mean(ber_vec, "omitnan");
    row.mean_corr = mean(corr_vec, "omitnan");
    row.mean_nrmse = mean(nrmse_vec, "omitnan");
    row.mode_accuracy = mean(results.mode_correct(mask));
end

function write_report(path, results, summary, cfg)
    fid = fopen(path, "w");
    if fid < 0
        error("Cannot write report: %s", path);
    end
    cleaner = onCleanup(@() fclose(fid)); %#ok<NASGU>

    fprintf(fid, "# Closed-loop demod validation report\n\n");
    fprintf(fid, "- Generated: 2026-05-26\n");
    fprintf(fid, "- Fs: %.3f MS/s\n", cfg.sample_rate_hz / 1e6);
    fprintf(fid, "- RF source output peak: %.1f mV, ADC target peak: %.2f V\n", ...
        cfg.rf_source_output_peak_v * 1e3, cfg.adc_target_peak_v);
    fprintf(fid, "- Profiles: %s\n", strjoin(cfg.profile_names, ", "));
    fprintf(fid, "- SNR: Inf / 20 / 10 dB\n\n");

    fprintf(fid, "## Scope\n\n");
    fprintf(fid, "This run compares three demod paths on the same simulated received IQ data.\n");
    fprintf(fid, "- oracle: true channel and modulation parameters are supplied to isolate the demodulator itself.\n");
    fprintf(fid, "- fixed: expected mode is selected, but demod parameters stay at nominal defaults.\n");
    fprintf(fid, "- identified: mode and parameters are estimated from the impaired IQ record before demod.\n\n");
    fprintf(fid, "The attached DPSK reference project was used as algorithm guidance, not as copied code.\n");
    fprintf(fid, "Relevant ideas retained here are: matched-filter style smoothing, eye-center timing from transition phase, ");
    fprintf(fid, "square-law BPSK carrier estimation, lightweight decision-directed Costas tracking, ");
    fprintf(fid, "and differential decoding as a future option when the transmitter is DPSK rather than ordinary 2PSK.\n\n");
    fprintf(fid, "The identified path also records diagnostic discriminants: envelope depth/floor, envelope tone fraction, ");
    fprintf(fid, "FM/FSK discriminator tone fraction, FSK harmonic ratio, FSK two-cluster score, and PSK phase-jump metric.\n\n");

    fprintf(fid, "## Overall summary\n\n");
    fprintf(fid, "| scheme | modulation | cases | pass | pass_rate | mean_ber | mean_corr | mean_nrmse | mode_accuracy |\n");
    fprintf(fid, "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    all_rows = summary(string(summary.profile) == "ALL" & string(summary.snr_db) == "ALL", :);
    for i = 1:height(all_rows)
        fprintf(fid, "| %s | %s | %d | %d | %.2f%% | %.4g | %.4g | %.4g | %.2f%% |\n", ...
            all_rows.scheme(i), all_rows.modulation(i), all_rows.total_count(i), ...
            all_rows.pass_count(i), 100 * all_rows.pass_rate(i), ...
            all_rows.mean_ber(i), all_rows.mean_corr(i), all_rows.mean_nrmse(i), ...
            100 * all_rows.mode_accuracy(i));
    end

    reasonable_mask = string(results.profile) ~= "board_like_hard" & ...
        (isinf(results.snr_db) | results.snr_db >= 20);
    fprintf(fid, "\n## Practical operating subset\n\n");
    fprintf(fid, "This subset excludes the intentionally hard front-end profile and 10 dB SNR. ");
    fprintf(fid, "It is closer to the target bench condition where the signal source is adjusted to a clean 100 mV-level receive chain.\n\n");
    write_scheme_mode_table(fid, results, reasonable_mask);

    fprintf(fid, "\n## Failure split\n\n");
    fprintf(fid, "If oracle passes but identified fails, the simulated signal is theoretically decodable and the main risk is mode/parameter/timing identification. ");
    fprintf(fid, "If oracle also fails, the demod path or the front-end condition itself is the limiting factor.\n\n");
    fprintf(fid, "| modulation | cases | oracle_pass_identified_fail | oracle_fail |\n");
    fprintf(fid, "| --- | ---: | ---: | ---: |\n");
    for mod = unique(string(results.modulation), "stable").'
        sub = reasonable_mask & string(results.modulation) == mod;
        fprintf(fid, "| %s | %d | %d | %d |\n", mod, sum(sub), ...
            sum(results.oracle_pass(sub) & ~results.dynamic_pass(sub)), ...
            sum(~results.oracle_pass(sub)));
    end

    fprintf(fid, "\n## Parameter-identification error highlights\n\n");
    for mod = unique(string(results.modulation), "stable").'
        mask = string(results.modulation) == mod;
        mode_acc = mean(results.mode_correct(mask));
        sym_err = mean(results.symbol_rate_error_pct(mask), "omitnan");
        fsk_err = mean(results.fsk_sep_error_pct(mask), "omitnan");
        fprintf(fid, "- %s: mode_accuracy %.2f%%, mean_symbol_rate_error %.2f%%, mean_fsk_sep_error %.2f%%\n", ...
            mod, 100 * mode_acc, sym_err, fsk_err);
    end

    fprintf(fid, "\n## Notes\n\n");
    fprintf(fid, "- The case set is deterministic but not tuned from one log file.\n");
    fprintf(fid, "- Digital BER scoring allows polarity inversion because there is no preamble.\n");
    fprintf(fid, "- FSK classification rejects ASK-like near-zero envelope cases before using phase/frequency evidence.\n");
    fprintf(fid, "- FSK versus FM separation uses both two-cluster discriminator evidence and square-wave harmonic evidence.\n");
    fprintf(fid, "- PSK demod includes a lightweight Costas-style residual phase tracker; static low-IF removal alone is not sufficient in board-like cases.\n");
    fprintf(fid, "- A failure in the identified path can be caused by mode identification, parameter estimation, or demod logic.\n");
    fprintf(fid, "- A failure in the fixed path mainly indicates that fixed 10 kbps / 20 kHz / low-IF assumptions are not portable.\n");
end

function write_scheme_mode_table(fid, results, mask)
    schemes = ["oracle", "fixed", "identified"];
    mods = unique(string(results.modulation), "stable");
    fprintf(fid, "| scheme | modulation | cases | pass | pass_rate | mean_ber | mean_corr | mean_nrmse |\n");
    fprintf(fid, "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |\n");
    for scheme = schemes
        for mod = mods.'
            sub = mask & string(results.modulation) == mod;
            total_count = sum(sub);
            if total_count == 0
                continue;
            end
            if scheme == "oracle"
                pass_vec = results.oracle_pass(sub);
                ber_vec = results.oracle_ber(sub);
                corr_vec = results.oracle_corr(sub);
                nrmse_vec = results.oracle_nrmse(sub);
            elseif scheme == "fixed"
                pass_vec = results.fixed_pass(sub);
                ber_vec = results.fixed_ber(sub);
                corr_vec = results.fixed_corr(sub);
                nrmse_vec = results.fixed_nrmse(sub);
            else
                pass_vec = results.dynamic_pass(sub);
                ber_vec = results.dynamic_ber(sub);
                corr_vec = results.dynamic_corr(sub);
                nrmse_vec = results.dynamic_nrmse(sub);
            end
            pass_count = sum(pass_vec);
            fprintf(fid, "| %s | %s | %d | %d | %.2f%% | %.4g | %.4g | %.4g |\n", ...
                scheme, mod, total_count, pass_count, 100 * pass_count / total_count, ...
                mean(ber_vec, "omitnan"), mean(corr_vec, "omitnan"), mean(nrmse_vec, "omitnan"));
        end
    end
end

function plot_summary(summary, output_dir)
    rows = summary(string(summary.profile) == "ALL" & string(summary.snr_db) == "ALL", :);
    mods = unique(string(rows.modulation), "stable");
    oracle = zeros(numel(mods), 1);
    fixed = zeros(numel(mods), 1);
    dyn = zeros(numel(mods), 1);
    for i = 1:numel(mods)
        oracle(i) = rows.pass_rate(string(rows.scheme) == "oracle" & string(rows.modulation) == mods(i));
        fixed(i) = rows.pass_rate(string(rows.scheme) == "fixed" & string(rows.modulation) == mods(i));
        dyn(i) = rows.pass_rate(string(rows.scheme) == "identified" & string(rows.modulation) == mods(i));
    end

    fig = figure("Visible", "off", "Color", "w");
    bar([oracle, fixed, dyn] * 100);
    grid on;
    xticklabels(mods);
    ylabel("Pass rate (%)");
    legend(["oracle", "fixed", "identified"], "Location", "southoutside", "Orientation", "horizontal");
    title("Closed-loop demod pass rate");
    exportgraphics(fig, fullfile(output_dir, "closed_loop_pass_rate_by_mode.png"), "Resolution", 150);
    close(fig);
end

function plot_examples(plot_bank, cfg)
    for i = 1:numel(plot_bank)
        item = plot_bank(i);
        t_ms = item.truth.t * 1e3;
        n = min(numel(t_ms), round(2.5e-3 * cfg.sample_rate_hz));
        if n < 32
            continue;
        end
        fig = figure("Visible", "off", "Color", "w");
        tiledlayout(2, 1, "TileSpacing", "compact");
        nexttile;
        if item.case_cfg.modulation == "AM" || item.case_cfg.modulation == "FM"
            h = plot(t_ms(1:n), item.truth.baseband(1:n), "k", "LineWidth", 1.0); hold on;
            names = "truth";
            if ~isempty(item.dynamic_metrics.waveform)
                y = normalize_plot_wave(item.dynamic_metrics.waveform(1:n));
                h(end + 1) = plot(t_ms(1:n), y, "r"); %#ok<AGROW>
                names(end + 1) = "identified"; %#ok<AGROW>
            end
            ylabel("analog");
            legend(h, names, "Location", "best");
        else
            h = stairs(t_ms(1:n), double(item.truth.sample_bits(1:n)), "k"); hold on;
            names = "truth";
            if ~isempty(item.dynamic_metrics.waveform)
                h(end + 1) = stairs(t_ms(1:n), item.dynamic_metrics.waveform(1:n), "r"); %#ok<AGROW>
                names(end + 1) = "identified"; %#ok<AGROW>
            end
            ylim([-0.2, 1.2]);
            ylabel("bits");
            legend(h, names, "Location", "best");
        end
        title(sprintf("%s case %d, profile %s, SNR %s", ...
            item.case_cfg.modulation, item.case_cfg.case_id, item.case_cfg.profile, snr_text(item.case_cfg.snr_db)));

        nexttile;
        plot(t_ms(1:n), real(item.iq_adc_v(1:n))); hold on;
        plot(t_ms(1:n), imag(item.iq_adc_v(1:n)));
        grid on;
        xlabel("time (ms)");
        ylabel("ADC V");
        legend(["I", "Q"], "Location", "best");
        file_name = sprintf("example_%02d_%s_case_%03d.png", i, item.case_cfg.modulation, item.case_cfg.case_id);
        exportgraphics(fig, fullfile(cfg.output_dir, file_name), "Resolution", 150);
        close(fig);
    end
end

function keep = should_keep_plot_case(case_cfg)
    keep = (case_cfg.snr_db == 20 && case_cfg.profile == "board_like_normal");
end

function y = normalize_plot_wave(x)
    x = x(:) - mean(x(:));
    y = x / max(max(abs(x)), eps);
end

function [freq_hz, tone_fraction] = dominant_tone_freq(x, fs, f_min, f_max)
    x = x(:) - mean(x(:));
    n = numel(x);
    win = hann_local(n);
    spec = abs(fft(x .* win)).^2;
    half = floor(n / 2) + 1;
    f = (0:half - 1).' * fs / n;
    spec = spec(1:half);
    mask = f >= f_min & f <= f_max;
    if ~any(mask) || sum(spec(mask)) <= eps
        freq_hz = NaN;
        tone_fraction = 0;
        return;
    end
    band_spec = spec(mask);
    band_f = f(mask);
    [pk, idx] = max(band_spec);
    freq_hz = band_f(idx);
    tone_fraction = pk / max(sum(band_spec), eps);
end

function ratio = harmonic_power_ratio(x, fs, base_freq_hz)
    if ~isfinite(base_freq_hz) || base_freq_hz <= 0
        ratio = 0;
        return;
    end
    x = x(:) - mean(x(:));
    n = numel(x);
    win = hann_local(n);
    spec = abs(fft(x .* win)).^2;
    half = floor(n / 2) + 1;
    f = (0:half - 1).' * fs / n;
    spec = spec(1:half);
    band_half_width_hz = max(500, 0.08 * base_freq_hz);
    primary = band_power_around(f, spec, base_freq_hz, band_half_width_hz);
    harmonic = 0;
    for mult = [3, 5]
        hf = mult * base_freq_hz;
        if hf < 0.45 * fs
            harmonic = harmonic + band_power_around(f, spec, hf, band_half_width_hz);
        end
    end
    ratio = harmonic / max(primary, eps);
end

function p = band_power_around(f, spec, center_hz, half_width_hz)
    mask = f >= center_hz - half_width_hz & f <= center_hz + half_width_hz;
    if any(mask)
        p = sum(spec(mask));
    else
        p = 0;
    end
end

function [rate_hz, timing_offset_samples] = estimate_symbol_rate(binary, fs, candidates)
    b = logical(binary(:));
    edge_idx = find(diff(b) ~= 0) + 1;
    if numel(edge_idx) < 4
        rate_hz = 10e3;
        timing_offset_samples = NaN;
        return;
    end
    best_score = -Inf;
    best_rate = candidates(1);
    best_offset = NaN;
    raw_run_lengths = diff([1; edge_idx; numel(b) + 1]);
    run_lengths = raw_run_lengths;
    phase_edges = edge_idx;
    if numel(raw_run_lengths) > 2
        run_lengths = raw_run_lengths(2:end - 1);
        min_valid_run = 0.35 * fs / max(candidates);
        filtered_runs = run_lengths(run_lengths >= min_valid_run);
        if numel(filtered_runs) >= 3
            run_lengths = filtered_runs;
        end
        valid_edge = raw_run_lengths(1:end - 1) >= min_valid_run & ...
            raw_run_lengths(2:end) >= min_valid_run;
        if any(valid_edge)
            phase_edges = edge_idx(valid_edge);
        end
    end
    for rate = candidates
        spb = fs / rate;
        run_ratio = run_lengths / spb;
        nearest = max(1, round(run_ratio));
        run_error = abs(run_ratio - nearest);
        align_score = -median(run_error, "omitnan");

        phase = mod(phase_edges, spb);
        phasor = exp(1j * 2 * pi * phase / spb);
        concentration = abs(mean(phasor));
        score = align_score + 0.02 * concentration;

        if score > best_score + 0.12 || (abs(score - best_score) <= 0.12 && rate < best_rate)
            best_score = score;
            best_rate = rate;
            best_offset = circular_mean_phase_samples(phase, spb);
        end
    end
    rate_hz = best_rate;
    timing_offset_samples = best_offset;
end

function offset = circular_mean_phase_samples(phase, spb)
    v = mean(exp(1j * 2 * pi * phase / spb));
    a = angle(v);
    if a < 0
        a = a + 2 * pi;
    end
    offset = a * spb / (2 * pi);
end

function [lo, hi, score, balance] = two_level_split(x)
    x = x(:);
    x = x(isfinite(x));
    if numel(x) > 6000
        step = ceil(numel(x) / 6000);
        x = x(1:step:end);
    end
    if numel(x) < 16
        lo = NaN;
        hi = NaN;
        score = 0;
        balance = 0;
        return;
    end
    lo = percentile_local(x, 25);
    hi = percentile_local(x, 75);
    for k = 1:12
        th = 0.5 * (lo + hi);
        a = x(x <= th);
        b = x(x > th);
        if isempty(a) || isempty(b)
            break;
        end
        lo = mean(a);
        hi = mean(b);
    end
    if lo > hi
        tmp = lo;
        lo = hi;
        hi = tmp;
    end
    th = 0.5 * (lo + hi);
    a = x(x <= th);
    b = x(x > th);
    within = 0.5 * (var(a) + var(b));
    score = abs(hi - lo) / max(sqrt(within), eps);
    balance = min(numel(a), numel(b)) / max(numel(a) + numel(b), 1);
end

function f = delay_frequency_estimate(z, fs, delay_n)
    z = z(:);
    if numel(z) <= delay_n
        f = NaN;
        return;
    end
    prod_sum = sum(z(1 + delay_n:end) .* conj(z(1:end - delay_n)));
    f = angle(prod_sum) * fs / (2 * pi * delay_n);
end

function fbest = refine_bpsk_low_if(z, fs, initial_hz)
    z = z(:);
    if ~isfinite(initial_hz)
        initial_hz = 0;
    end
    span_hz = 5e3;
    step_hz = 250;
    center_candidates = unique([initial_hz, 0, -5e3, 5e3]);
    candidates = [];
    for c = center_candidates
        candidates = [candidates, c + (-span_hz:step_hz:span_hz)]; %#ok<AGROW>
    end
    candidates = unique(candidates(abs(candidates) <= 20e3));
    n = numel(z);
    t = (0:n - 1).' / fs;
    best_score = -Inf;
    fbest = initial_hz;
    for f = candidates
        rotated = z .* exp(-1j * 2 * pi * f * t);
        score = abs(mean(rotated.^2)) / max(mean(abs(rotated).^2), eps);
        if score > best_score
            best_score = score;
            fbest = f;
        end
    end
end

function zbb = nco_compensate(z, fs, low_if_hz)
    n = numel(z);
    t = (0:n - 1).' / fs;
    zbb = z(:) .* exp(-1j * 2 * pi * low_if_hz * t);
end

function proj = psk_projector(zbb)
    m = mean(zbb(:).^2);
    phi = 0.5 * angle(m);
    proj = real(zbb(:) * exp(-1j * phi));
end

function proj = bpsk_costas_projector(zbb, fs, symbol_rate_hz)
    z = zbb(:);
    m = mean(z.^2);
    if isfinite(m) && abs(m) > eps
        z = z * exp(-1j * 0.5 * angle(m));
    end
    n = numel(z);
    proj = zeros(n, 1);
    if n == 0
        return;
    end

    rate_scale = max(symbol_rate_hz, 1) / 10e3;
    alpha = min(max(0.020 * rate_scale, 0.010), 0.050);
    beta = alpha * alpha / 8;
    max_freq_rad = 2 * pi * 25e3 / fs;
    phase = 0;
    freq = 0;

    for k = 1:n
        v = z(k) * exp(-1j * phase);
        re = real(v);
        im = imag(v);
        if re >= 0
            decision = 1;
        else
            decision = -1;
        end
        err = decision * im / max(abs(v), eps);
        err = min(max(err, -1), 1);
        freq = min(max(freq + beta * err, -max_freq_rad), max_freq_rad);
        phase = wrap_pi(phase + freq + alpha * err);
        proj(k) = re;
    end
end

function [best_corr, best_y, best_ref] = best_abs_corr(y, ref, max_lag)
    best_corr = -Inf;
    best_y = y;
    best_ref = ref;
    for lag = -max_lag:max_lag
        if lag < 0
            yy = y(1:end + lag);
            rr = ref(1 - lag:end);
        elseif lag > 0
            yy = y(1 + lag:end);
            rr = ref(1:end - lag);
        else
            yy = y;
            rr = ref;
        end
        if numel(yy) < 16
            continue;
        end
        yy = yy - mean(yy);
        rr = rr - mean(rr);
        c = abs((yy' * rr) / max(sqrt((yy' * yy) * (rr' * rr)), eps));
        if c > best_corr
            best_corr = c;
            best_y = yy;
            best_ref = rr;
        end
    end
end

function best = best_ber_with_shift(truth_bits, decoded_bits, shifts)
    best = struct("ber", Inf, "inverted", false, "shift", 0);
    for shift = shifts
        if shift < 0
            d = decoded_bits(1:end + shift);
            t = truth_bits(1 - shift:end);
        elseif shift > 0
            d = decoded_bits(1 + shift:end);
            t = truth_bits(1:end - shift);
        else
            d = decoded_bits;
            t = truth_bits;
        end
        if numel(d) < 8
            continue;
        end
        ber_normal = mean(d ~= t);
        ber_inv = mean(~d ~= t);
        if ber_inv < ber_normal
            ber = ber_inv;
            inv = true;
        else
            ber = ber_normal;
            inv = false;
        end
        if ber < best.ber
            best.ber = ber;
            best.inverted = inv;
            best.shift = shift;
        end
    end
end

function x = clip_vector(x, lo, hi)
    x = min(max(x, lo), hi);
end

function y = wrap_pi(x)
    y = mod(x + pi, 2 * pi) - pi;
end

function p = percentile_local(x, pct)
    x = sort(x(isfinite(x(:))));
    if isempty(x)
        p = NaN;
        return;
    end
    pos = 1 + (numel(x) - 1) * pct / 100;
    lo = floor(pos);
    hi = ceil(pos);
    if lo == hi
        p = x(lo);
    else
        p = x(lo) + (x(hi) - x(lo)) * (pos - lo);
    end
end

function m = median_local(x)
    m = percentile_local(x, 50);
end

function r = rms_local(x)
    r = sqrt(mean(abs(x).^2));
end

function w = hann_local(n)
    if n <= 1
        w = ones(n, 1);
    else
        w = 0.5 - 0.5 * cos(2 * pi * (0:n - 1).' / (n - 1));
    end
end

function v = uniform_range(range)
    v = range(1) + (range(2) - range(1)) * rand();
end

function v = symmetric_random(span)
    v = (2 * rand() - 1) * span;
end

function txt = snr_text(snr_db)
    if isinf(snr_db)
        txt = "Inf";
    else
        txt = sprintf("%.0fdB", snr_db);
    end
end
