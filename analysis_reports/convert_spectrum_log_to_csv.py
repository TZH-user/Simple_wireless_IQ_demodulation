from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path
from tkinter import Tk, filedialog


SPEC_ID_TO_NAME = {
    1: "IQ",
    2: "包络",
    3: "相位",
}


def parse_log(log_path: Path, test_id: str) -> list[dict[str, object]]:
    """解析串口导出的频谱日志，返回适合写入 CSV 的长表记录。"""
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    center_match = re.search(r"sweep done center=(\d+)Hz", text)
    done_match = re.search(r"calculation complete center=(\d+)Hz mod=(\d+)Hz depth=(\d+)pm", text)
    depths = [int(x) for x in re.findall(r"analyze: depth=(\d+)pm", text)]

    center_hz = ""
    mod_hz = ""
    depth_pm = ""
    if done_match:
        center_hz = done_match.group(1)
        mod_hz = done_match.group(2)
        depth_pm = done_match.group(3)
    elif center_match:
        center_hz = center_match.group(1)
        if depths:
            depth_pm = str(round(sum(depths) / len(depths)))

    rows: list[dict[str, object]] = []
    for freq_hz, mag, phase_rad, bin_no, spec_id in re.findall(
        r"spec:([0-9.]+),([0-9.]+),([-0-9.]+),(\d+),(\d+)", text
    ):
        sid = int(spec_id)
        spec_name = SPEC_ID_TO_NAME.get(sid)
        if spec_name is None:
            continue
        rows.append(
            {
                "测试编号": test_id,
                "数据源文件": log_path.name,
                "中心频率_Hz": center_hz,
                "识别调制频率_Hz": mod_hz,
                "包络深度_pm": depth_pm,
                "频谱类型": spec_name,
                "频率_Hz": freq_hz,
                "幅度": mag,
                "相位_rad": phase_rad,
                "Bin": bin_no,
            }
        )
    return rows


def split_log_paths(raw_paths: str) -> list[Path]:
    """允许用户用分号分隔多个日志路径。"""
    return [Path(item.strip().strip('"')) for item in raw_paths.split(";") if item.strip()]


def choose_files_with_dialog(args: argparse.Namespace) -> argparse.Namespace:
    """没有传命令行参数时，用 Windows 文件窗口选择日志和输出 CSV。"""
    root = Tk()
    root.withdraw()
    root.attributes("-topmost", True)

    if not args.logs:
        log_paths = filedialog.askopenfilenames(
            title="选择串口导出日志文件，可多选",
            filetypes=[("串口日志", "*.txt *.log"), ("所有文件", "*.*")],
        )
        if log_paths:
            args.logs = ";".join(log_paths)

    if not args.out:
        out_path = filedialog.asksaveasfilename(
            title="选择输出 CSV 文件名",
            defaultextension=".csv",
            filetypes=[("CSV 文件", "*.csv"), ("所有文件", "*.*")],
        )
        if out_path:
            args.out = out_path

    root.destroy()
    return args


def ask_if_missing(args: argparse.Namespace) -> argparse.Namespace:
    args = choose_files_with_dialog(args)
    if not args.logs:
        args.logs = input("请输入串口日志路径，多个文件用英文分号分隔：").strip()
    if not args.out:
        args.out = input("请输入输出 CSV 文件路径或文件名：").strip()
    return args


def main() -> None:
    parser = argparse.ArgumentParser(description="将串口频谱日志转换为 CSV 长表。")
    parser.add_argument("--logs", help="日志路径；多个日志用英文分号分隔。")
    parser.add_argument("--out", help="输出 CSV 文件路径。")
    parser.add_argument("--test-prefix", default="T", help="测试编号前缀，默认 T。")
    args = ask_if_missing(parser.parse_args())

    log_paths = split_log_paths(args.logs)
    if not log_paths:
        raise SystemExit("未提供有效日志路径。")

    out_path = Path(args.out.strip().strip('"'))
    if out_path.suffix.lower() != ".csv":
        out_path = out_path.with_suffix(".csv")

    rows: list[dict[str, object]] = []
    for index, log_path in enumerate(log_paths, start=1):
        if not log_path.exists():
            raise SystemExit(f"日志文件不存在：{log_path}")
        test_id = f"{args.test_prefix}{index:03d}"
        rows.extend(parse_log(log_path, test_id))

    headers = [
        "测试编号",
        "数据源文件",
        "中心频率_Hz",
        "识别调制频率_Hz",
        "包络深度_pm",
        "频谱类型",
        "频率_Hz",
        "幅度",
        "相位_rad",
        "Bin",
    ]

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", newline="", encoding="utf-8-sig") as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        writer.writeheader()
        writer.writerows(rows)

    print(f"CSV 已生成：{out_path}")
    print(f"写入频谱点数：{len(rows)}")


if __name__ == "__main__":
    main()
