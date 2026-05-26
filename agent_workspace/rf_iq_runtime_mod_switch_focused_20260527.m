% rf_iq_runtime_mod_switch_focused_20260527.m
% Focused runtime-monitor simulation:
% current demod is running, then RF signal suddenly switches to another
% modulation, optionally at a different frequency. This mirrors the current
% low-compute monitor in Core/App/Tasks/DemodTask.c.

clear; clc; close all;

cfg = struct();
cfg.sample_rate_hz = 2.048e6;
cfg.block_n = 4096;
cfg.decim = 32;
cfg.baseline_blocks = 4;
cfg.trigger_blocks = 2;
cfg.low_if_delta_hz = 1700;
cfg.power_delta_pm = 350;
cfg.env_cv_delta_pm = 300;
cfg.phase_delta_pm = 260;
cfg.duration_s = 0.052;
cfg.change_time_s = 0.024;
cfg.valid_latency_s = 0.020;
cfg.target_low_if_hz = -5e3;
cfg.adc_bits = 14;
cfg.adc_target_peak_v = 0.80;
cfg.snr_db_list = [20, 10];
cfg.freq_step_hz_list = [0, 10e3, -10e3, 20e3, 50e3];
cfg.rng_seed = 20260527;
cfg.output_dir = fullfile(fileparts(mfilename("fullpath")), ...
    "runtime_mod_switch_focused_output_20260527");

if ~exist(cfg.output_dir, "dir")
    mkdir(cfg.output_dir);
end

rng(cfg.rng_seed);
profiles = build_profiles();
mods = ["AM", "FM", "ASK", "FSK", "PSK"];

cases = build_cases(mods, profiles, cfg);
rows = repmat(empty_result_row(), 0, 1);

fprintf("Focused runtime modulation/frequency switch simulation\n");
fprintf("Cases: %d, Fs %.3f MS/s, block %.3f ms\n", ...
    numel(cases), cfg.sample_rate_hz / 1e6, cfg.block_n / cfg.sample_rate_hz * 1e3);
fprintf("Output: %s\n", cfg.output_dir);

for k = 1:numel(cases)
    case_cfg = cases(k);
    [i_adc, q_adc, meta] = synth_case(case_cfg, cfg);
    monitor = run_monitor(i_adc, q_adc, case_cfg.mod_a, cfg);
    rows(end + 1) = make_result_row(case_cfg, meta, monitor, cfg); %#ok<SAGROW>
end

results = struct2table(rows);
results_csv = fullfile(cfg.output_dir, "runtime_mod_switch_focused_results.csv");
writetable(results, results_csv);

summary = make_summary(results);
summary_csv = fullfile(cfg.output_dir, "runtime_mod_switch_focused_summary.csv");
writetable(summary, summary_csv);

pair_summary = make_pair_summary(results);
pair_csv = fullfile(cfg.output_dir, "runtime_mod_switch_focused_pair_summary.csv");
writetable(pair_summary, pair_csv);

report_path = fullfile(cfg.output_dir, "runtime_mod_switch_focused_report.md");
write_report(report_path, results, summary, pair_summary, cfg);

fprintf("\nSummary:\n");
disp(summary);
fprintf("Results CSV: %s\n", results_csv);
fprintf("Summary CSV: %s\n", summary_csv);
fprintf("Pair CSV: %s\n", pair_csv);
fprintf("Report: %s\n", report_path);

function profiles = build_profiles()
    profiles = struct([]);

    profiles(end + 1).name = "ideal";
    profiles(end).freq_error_hz = 0;
    profiles(end).drift_hz = 0;
    profiles(end).iq_gain_err = 0.000;
    profiles(end).iq_phase_err_deg = 0.0;
    profiles(end).image_leakage_db = -Inf;
    profiles(end).dc_offset = 0.000;
    profiles(end).ripple_depth = 0.000;

    profiles(end + 1).name = "board_like_normal";
    profiles(end).freq_error_hz = 1100;
    profiles(end).drift_hz = 60;
    profiles(end).iq_gain_err = 0.025;
    profiles(end).iq_phase_err_deg = 2.0;
    profiles(end).image_leakage_db = -24;
    profiles(end).dc_offset = 0.010;
    profiles(end).ripple_depth = 0.012;

    profiles(end + 1).name = "board_like_hard";
    profiles(end).freq_error_hz = 4200;
    profiles(end).drift_hz = 180;
    profiles(end).iq_gain_err = 0.060;
    profiles(end).iq_phase_err_deg = 5.0;
    profiles(end).image_leakage_db = -15;
    profiles(end).dc_offset = 0.025;
    profiles(end).ripple_depth = 0.025;
end

function cases = build_cases(mods, profiles, cfg)
    cases = struct([]);
    case_id = 0;

    for profile_index = 1:numel(profiles)
        for snr_db = cfg.snr_db_list
            for m = 1:numel(mods)
                case_id = case_id + 1;
                cases(end + 1) = make_case(case_id, "no_change", profiles(profile_index), ...
                    snr_db, mods(m), mods(m), 0, cfg); %#ok<AGROW>
            end

            for a = 1:numel(mods)
                for b = 1:numel(mods)
                    if a == b
                        continue;
                    end
                    for freq_step_hz = cfg.freq_step_hz_list
                        scenario = "mod_switch_same_freq";
                        if freq_step_hz ~= 0
                            scenario = "freq_mod_switch";
                        end
                        case_id = case_id + 1;
                        cases(end + 1) = make_case(case_id, scenario, profiles(profile_index), ...
                            snr_db, mods(a), mods(b), freq_step_hz, cfg); %#ok<AGROW>
                    end
                end
            end
        end
    end
end

function c = make_case(case_id, scenario, profile, snr_db, mod_a, mod_b, freq_step_hz, cfg)
    c = struct();
    c.case_id = case_id;
    c.scenario = string(scenario);
    c.has_change = scenario ~= "no_change";
    c.profile = profile;
    c.profile_name = string(profile.name);
    c.snr_db = snr_db;
    c.mod_a = string(mod_a);
    c.mod_b = string(mod_b);
    c.freq_step_hz = freq_step_hz;
    c.low_if_a_hz = cfg.target_low_if_hz + profile.freq_error_hz;
    c.low_if_b_hz = c.low_if_a_hz + freq_step_hz;
    c.seed = 910000 + case_id * 31;
end

function [i_adc, q_adc, meta] = synth_case(case_cfg, cfg)
    rng(case_cfg.seed);
    n = round(cfg.duration_s * cfg.sample_rate_hz);
    t = (0:n-1).' / cfg.sample_rate_hz;
    change_index = max(1, min(n, round(cfg.change_time_s * cfg.sample_rate_hz)));

    iq_a = synth_mod(case_cfg.mod_a, case_cfg.low_if_a_hz, t, cfg);
    if case_cfg.has_change
        iq_b = synth_mod(case_cfg.mod_b, case_cfg.low_if_b_hz, t, cfg);
        iq = iq_a;
        iq(change_index:end) = iq_b(change_index:end);
    else
        iq = iq_a;
    end

    drift_phase = 2 * pi * (case_cfg.profile.drift_hz / cfg.duration_s) .* (t.^2) / 2;
    iq = iq .* exp(1j * drift_phase);
    iq = apply_frontend(iq, t, case_cfg.profile);
    iq = add_awgn(iq, case_cfg.snr_db);
    [i_adc, q_adc] = quantize_adc(iq, cfg);

    meta = struct();
    meta.n = n;
    meta.change_index = change_index;
    meta.change_time_s = cfg.change_time_s;
end

function iq = synth_mod(modulation, low_if_hz, t, cfg)
    switch modulation
        case "AM"
            depth = 0.50;
            msg = sin(2*pi*10e3*t);
            env = 1.0 + depth * msg;
            phase = 2*pi*low_if_hz*t;
            iq = env .* exp(1j*phase);
        case "FM"
            msg = sin(2*pi*10e3*t);
            inst_freq = low_if_hz + 75e3 * msg;
            phase = 2*pi*cumsum(inst_freq) / cfg.sample_rate_hz;
            iq = exp(1j*phase);
        case "ASK"
            bits = make_bits(numel(t), cfg.sample_rate_hz, 10e3, "pn9");
            env = 0.05 + 0.95 * bits;
            phase = 2*pi*low_if_hz*t;
            iq = env .* exp(1j*phase);
        case "FSK"
            bits = make_bits(numel(t), cfg.sample_rate_hz, 10e3, "pn9");
            f_dev = 10e3;
            inst_freq = low_if_hz + (2*bits - 1) * f_dev;
            phase = 2*pi*cumsum(inst_freq) / cfg.sample_rate_hz;
            iq = exp(1j*phase);
        case "PSK"
            bits = make_bits(numel(t), cfg.sample_rate_hz, 10e3, "pn9");
            sym = 2*bits - 1;
            phase = 2*pi*low_if_hz*t;
            iq = sym .* exp(1j*phase);
        otherwise
            error("Unsupported modulation: %s", modulation);
    end
end

function bits = make_bits(n, fs, symbol_rate_hz, pattern)
    samples_per_symbol = max(1, round(fs / symbol_rate_hz));
    symbol_count = ceil(n / samples_per_symbol) + 2;
    switch pattern
        case "pn9"
            reg = ones(1, 9);
            b = zeros(symbol_count, 1);
            for k = 1:symbol_count
                b(k) = reg(end);
                new_bit = xor(reg(5), reg(9));
                reg = [new_bit, reg(1:end-1)]; %#ok<AGROW>
            end
        otherwise
            b = repmat([0;1], ceil(symbol_count/2), 1);
            b = b(1:symbol_count);
    end
    bits = repelem(b, samples_per_symbol);
    bits = bits(1:n);
end

function iq = apply_frontend(iq, t, profile)
    if isfinite(profile.image_leakage_db)
        leak = 10^(profile.image_leakage_db / 20);
        iq = iq + leak * conj(iq);
    end

    i = real(iq) * (1 + profile.iq_gain_err);
    q = imag(iq) * (1 - profile.iq_gain_err);
    ph = deg2rad(profile.iq_phase_err_deg);
    iq = i + 1j * (q*cos(ph) + i*sin(ph));

    if profile.ripple_depth ~= 0
        iq = iq .* (1 + profile.ripple_depth * sin(2*pi*1200*t));
    end

    iq = iq + profile.dc_offset * (1 + 0.7j);
end

function y = add_awgn(x, snr_db)
    if isinf(snr_db)
        y = x;
        return;
    end
    sig_power = mean(abs(x).^2);
    noise_power = sig_power / (10^(snr_db / 10));
    noise = sqrt(noise_power/2) * (randn(size(x)) + 1j*randn(size(x)));
    y = x + noise;
end

function [i_adc, q_adc] = quantize_adc(iq, cfg)
    max_code = 2^cfg.adc_bits - 1;
    center = 2^(cfg.adc_bits - 1);
    peak = max(abs([real(iq); imag(iq)]));
    if peak <= 0
        peak = 1;
    end
    scale = cfg.adc_target_peak_v * center / peak;
    i_adc = round(center + real(iq) * scale);
    q_adc = round(center + imag(iq) * scale);
    i_adc = uint16(min(max(i_adc, 0), max_code));
    q_adc = uint16(min(max(q_adc, 0), max_code));
end

function mon = run_monitor(i_adc, q_adc, initial_mode, cfg)
    state = struct();
    state.baseline = empty_feature();
    state.baseline_valid = false;
    state.baseline_blocks = 0;
    state.suspect_blocks = 0;
    state.triggered = false;
    state.trigger_time_s = NaN;
    state.trigger_reason = 0;
    state.trigger_score = 0;

    n = numel(i_adc);
    block_count = floor(n / cfg.block_n);
    for b = 1:block_count
        idx0 = (b-1)*cfg.block_n + 1;
        idx1 = b*cfg.block_n;
        now = extract_features(i_adc(idx0:idx1), q_adc(idx0:idx1), cfg);

        if ~state.baseline_valid
            if state.baseline_blocks == 0
                state.baseline = now;
            else
                state.baseline = average_feature_like_c(state.baseline, now);
            end
            state.baseline_blocks = state.baseline_blocks + 1;
            if state.baseline_blocks >= cfg.baseline_blocks
                state.baseline_valid = true;
            end
            continue;
        end

        [score, reason] = score_features(now, state.baseline, initial_mode, cfg);
        if score >= 2
            state.suspect_blocks = state.suspect_blocks + 1;
        else
            state.suspect_blocks = 0;
        end

        if state.suspect_blocks >= cfg.trigger_blocks
            state.triggered = true;
            state.trigger_time_s = idx1 / cfg.sample_rate_hz;
            state.trigger_reason = reason;
            state.trigger_score = score;
            break;
        end
    end

    mon = state;
end

function f = empty_feature()
    f = struct("mean_power", 0, "env_cv_pm", 0, ...
        "short_phase_disp_pm", 0, "long_phase_disp_pm", 0, "low_if_hz", 0);
end

function f = average_feature_like_c(a, b)
    f = empty_feature();
    f.mean_power = floor((a.mean_power + b.mean_power) / 2);
    f.env_cv_pm = floor((a.env_cv_pm + b.env_cv_pm) / 2);
    f.short_phase_disp_pm = floor((a.short_phase_disp_pm + b.short_phase_disp_pm) / 2);
    f.long_phase_disp_pm = floor((a.long_phase_disp_pm + b.long_phase_disp_pm) / 2);
    f.low_if_hz = round((a.low_if_hz + b.low_if_hz) / 2);
end

function f = extract_features(i_adc, q_adc, cfg)
    center = 2^(cfg.adc_bits - 1);
    short_lag = cfg.decim;
    long_lag = cfg.decim * 4;
    power_sum = 0;
    power_sq_sum = 0;
    short_re = 0; short_im = 0;
    long_re = 0; long_im = 0;
    if_re = 0; if_im = 0;
    used = 0;

    for idx = (long_lag+1):cfg.decim:numel(i_adc)
        i0 = double(i_adc(idx)) - center;
        q0 = double(q_adc(idx)) - center;
        is = double(i_adc(idx - short_lag)) - center;
        qs = double(q_adc(idx - short_lag)) - center;
        il = double(i_adc(idx - long_lag)) - center;
        ql = double(q_adc(idx - long_lag)) - center;
        power = i0*i0 + q0*q0;

        power_sum = power_sum + power;
        power_sq_sum = power_sq_sum + power * power;
        if_re = if_re + i0*is + q0*qs;
        if_im = if_im + q0*is - i0*qs;
        short_re = short_re + i0*is + q0*qs;
        short_im = short_im + q0*is - i0*qs;
        long_re = long_re + i0*il + q0*ql;
        long_im = long_im + q0*il - i0*ql;
        used = used + 1;
    end

    f = empty_feature();
    if used == 0 || power_sum <= 0
        return;
    end

    f.mean_power = floor(power_sum / used);
    mean_power = power_sum / used;
    var_power = max(0, power_sq_sum / used - mean_power * mean_power);
    if f.mean_power > 0
        f.env_cv_pm = floor(var_power * 1000 / (f.mean_power * f.mean_power));
    end

    if if_re ~= 0 || if_im ~= 0
        phase = atan2(if_im, if_re);
        f.low_if_hz = round(phase * cfg.sample_rate_hz / (2*pi*short_lag));
    end

    total = power_sum;
    short_coh = abs(short_re) + abs(short_im);
    long_coh = abs(long_re) + abs(long_im);
    f.short_phase_disp_pm = max(0, floor((total - min(short_coh, total)) * 1000 / total));
    f.long_phase_disp_pm = max(0, floor((total - min(long_coh, total)) * 1000 / total));
end

function [score, reason] = score_features(now, baseline, initial_mode, cfg)
    score = 0;
    reason = 0;

    if baseline.mean_power ~= 0
        power_delta_pm = abs(now.mean_power - baseline.mean_power) * 1000 / baseline.mean_power;
    else
        power_delta_pm = 0;
    end
    env_delta_pm = abs(now.env_cv_pm - baseline.env_cv_pm);
    short_delta_pm = abs(now.short_phase_disp_pm - baseline.short_phase_disp_pm);
    long_delta_pm = abs(now.long_phase_disp_pm - baseline.long_phase_disp_pm);
    low_if_delta_hz = abs(now.low_if_hz - baseline.low_if_hz);

    if any(initial_mode == ["AM", "ASK", "PSK"])
        if low_if_delta_hz >= cfg.low_if_delta_hz
            score = score + 2;
            reason = bitor(reason, hex2dec("01"));
        end
    end
    if power_delta_pm >= cfg.power_delta_pm
        score = score + 1;
        reason = bitor(reason, hex2dec("02"));
    end
    if env_delta_pm >= cfg.env_cv_delta_pm
        score = score + 1;
        reason = bitor(reason, hex2dec("04"));
    end
    if short_delta_pm >= cfg.phase_delta_pm
        score = score + 1;
        reason = bitor(reason, hex2dec("08"));
    end
    if long_delta_pm >= cfg.phase_delta_pm
        score = score + 1;
        reason = bitor(reason, hex2dec("10"));
    end
end

function r = empty_result_row()
    r = struct("case_id", 0, "scenario", "", "profile", "", "snr_db", NaN, ...
        "mod_a", "", "mod_b", "", "freq_step_hz", NaN, "has_change", false, ...
        "triggered", false, "valid_detection", false, "false_alarm", false, ...
        "miss", false, "late_detection", false, "latency_ms", NaN, ...
        "trigger_score", NaN, "trigger_reason", NaN);
end

function r = make_result_row(case_cfg, meta, mon, cfg)
    latency_s = mon.trigger_time_s - meta.change_time_s;
    valid_detection = case_cfg.has_change && mon.triggered && latency_s >= 0 && latency_s <= cfg.valid_latency_s;
    false_alarm = (~case_cfg.has_change && mon.triggered) || ...
        (case_cfg.has_change && mon.triggered && latency_s < 0);
    late_detection = case_cfg.has_change && mon.triggered && latency_s > cfg.valid_latency_s;
    miss = case_cfg.has_change && ~valid_detection && ~false_alarm;

    r = empty_result_row();
    r.case_id = case_cfg.case_id;
    r.scenario = case_cfg.scenario;
    r.profile = case_cfg.profile_name;
    r.snr_db = case_cfg.snr_db;
    r.mod_a = case_cfg.mod_a;
    r.mod_b = case_cfg.mod_b;
    r.freq_step_hz = case_cfg.freq_step_hz;
    r.has_change = case_cfg.has_change;
    r.triggered = mon.triggered;
    r.valid_detection = valid_detection;
    r.false_alarm = false_alarm;
    r.miss = miss;
    r.late_detection = late_detection;
    r.latency_ms = latency_s * 1e3;
    r.trigger_score = mon.trigger_score;
    r.trigger_reason = mon.trigger_reason;
end

function summary = make_summary(results)
    groups = [
        "ALL", "ALL", "ALL"
        "scenario", "scenario", "ALL"
        "profile", "profile", "ALL"
        "snr", "snr_db", "ALL"
        "freq_step", "freq_step_hz", "ALL"
    ];
    rows = repmat(empty_summary_row(), 0, 1);
    for g = 1:size(groups, 1)
        label = groups(g, 1);
        field = groups(g, 2);
        if label == "ALL"
            rows(end + 1) = summarize_subset(results, label, "ALL", results); %#ok<AGROW>
        else
            vals = unique(results.(field));
            for i = 1:numel(vals)
                if isstring(vals) || iscellstr(vals)
                    subset = results(results.(field) == vals(i), :);
                    value = string(vals(i));
                else
                    subset = results(results.(field) == vals(i), :);
                    value = string(num2str(vals(i)));
                end
                rows(end + 1) = summarize_subset(subset, label, value, results); %#ok<AGROW>
            end
        end
    end
    summary = struct2table(rows);
end

function s = empty_summary_row()
    s = struct("group", "", "value", "", "cases", 0, "changed_cases", 0, ...
        "valid_detection_rate", NaN, "false_alarm_rate", NaN, "miss_rate", NaN, ...
        "late_rate", NaN, "median_latency_ms", NaN, "p95_latency_ms", NaN);
end

function s = summarize_subset(subset, group, value, all_results)
    if nargin < 4
        all_results = subset;
    end
    changed = subset(subset.has_change, :);
    unchanged = subset(~subset.has_change, :);
    lat = changed.latency_ms(changed.valid_detection);

    s = empty_summary_row();
    s.group = string(group);
    s.value = string(value);
    s.cases = height(subset);
    s.changed_cases = height(changed);
    if height(changed) > 0
        s.valid_detection_rate = mean(changed.valid_detection);
        s.miss_rate = mean(changed.miss);
        s.late_rate = mean(changed.late_detection);
    end
    if height(unchanged) > 0
        s.false_alarm_rate = mean(unchanged.false_alarm);
    else
        s.false_alarm_rate = mean(subset.false_alarm);
    end
    if isempty(lat)
        s.median_latency_ms = NaN;
        s.p95_latency_ms = NaN;
    else
        s.median_latency_ms = median(lat);
        s.p95_latency_ms = prctile(lat, 95);
    end
end

function pair_summary = make_pair_summary(results)
    changed = results(results.has_change, :);
    keys = unique(changed(:, ["mod_a", "mod_b"]));
    rows = repmat(empty_summary_row(), 0, 1);
    for i = 1:height(keys)
        subset = changed(changed.mod_a == keys.mod_a(i) & changed.mod_b == keys.mod_b(i), :);
        rows(end + 1) = summarize_subset(subset, "pair", keys.mod_a(i) + "->" + keys.mod_b(i), changed); %#ok<AGROW>
    end
    pair_summary = struct2table(rows);
end

function write_report(path, results, summary, pair_summary, cfg)
    fid = fopen(path, "w", "n", "UTF-8");
    if fid < 0
        error("Cannot write report: %s", path);
    end
    cleanup = onCleanup(@() fclose(fid)); %#ok<NASGU>

    overall = summary(summary.group == "ALL", :);
    same_freq = results(results.scenario == "mod_switch_same_freq" & results.has_change, :);
    diff_freq = results(results.scenario == "freq_mod_switch" & results.has_change, :);
    no_change = results(results.scenario == "no_change", :);

    fprintf(fid, "# Focused Runtime Modulation Switch Monitor Report\n\n");
    fprintf(fid, "Date: 2026-05-27\n\n");
    fprintf(fid, "This simulation mirrors the current low-compute monitor in `DemodTask.c`: block_n=%d, decim=%d, baseline_blocks=%d, trigger_blocks=%d.\n\n", ...
        cfg.block_n, cfg.decim, cfg.baseline_blocks, cfg.trigger_blocks);
    fprintf(fid, "Normal modulation parameters: AM 10 kHz / 50%%, FM 10 kHz / 75 kHz, ASK/FSK/PSK 10 kbps, FSK separation 20 kHz.\n\n");

    fprintf(fid, "## Main Result\n\n");
    fprintf(fid, "- All changed cases valid detection rate: %.2f%%\n", 100 * overall.valid_detection_rate(1));
    fprintf(fid, "- Same-frequency modulation switch detection rate: %.2f%%\n", 100 * mean(same_freq.valid_detection));
    fprintf(fid, "- Different-frequency modulation switch detection rate: %.2f%%\n", 100 * mean(diff_freq.valid_detection));
    fprintf(fid, "- No-change false alarm rate: %.2f%%\n", 100 * mean(no_change.false_alarm));
    fprintf(fid, "- Median valid latency: %.2f ms\n\n", overall.median_latency_ms(1));

    fprintf(fid, "## Interpretation\n\n");
    fprintf(fid, "- Different-frequency switching is much easier because low-IF delta contributes a score of 2 for AM/ASK/PSK initial modes.\n");
    fprintf(fid, "- FM/FSK initial modes do not use low-IF delta as a strong trigger in the current firmware, so constant-envelope transitions can be weaker.\n");
    fprintf(fid, "- Same-frequency modulation-only switching is the harder case and should not be assumed fully covered by the current low-cost monitor.\n\n");

    fprintf(fid, "## Summary Table\n\n");
    fprintf(fid, "| Group | Value | Cases | Valid detection | False alarm | Miss | Late | Median latency ms |\n");
    fprintf(fid, "|---|---|---:|---:|---:|---:|---:|---:|\n");
    for i = 1:height(summary)
        fprintf(fid, "| %s | %s | %d | %.2f%% | %.2f%% | %.2f%% | %.2f%% | %.2f |\n", ...
            char(summary.group(i)), char(summary.value(i)), summary.cases(i), ...
            100 * summary.valid_detection_rate(i), 100 * summary.false_alarm_rate(i), ...
            100 * summary.miss_rate(i), 100 * summary.late_rate(i), summary.median_latency_ms(i));
    end

    fprintf(fid, "\n## Pair Weaknesses\n\n");
    weak = pair_summary(pair_summary.valid_detection_rate < 0.85, :);
    if isempty(weak)
        fprintf(fid, "No modulation pair is below 85%% in this focused sweep.\n");
    else
        fprintf(fid, "| Pair | Cases | Valid detection | Miss | Late | Median latency ms |\n");
        fprintf(fid, "|---|---:|---:|---:|---:|---:|\n");
        for i = 1:height(weak)
            fprintf(fid, "| %s | %d | %.2f%% | %.2f%% | %.2f%% | %.2f |\n", ...
                char(weak.value(i)), weak.cases(i), 100 * weak.valid_detection_rate(i), ...
                100 * weak.miss_rate(i), 100 * weak.late_rate(i), weak.median_latency_ms(i));
        end
    end
end
