from __future__ import annotations

import argparse
import time

import serial


def main() -> None:
    parser = argparse.ArgumentParser(description="Send one temporary ASCII command to the STM32 log UART.")
    parser.add_argument("command", help="Command text, for example @PING, @TASK, @CAL, @IDLE, @STAT.")
    parser.add_argument("--port", default="COM14", help="Serial port, default COM14.")
    parser.add_argument("--baud", type=int, default=1500000, help="Baud rate, default 1500000.")
    parser.add_argument("--read-ms", type=int, default=1200, help="Readback time in ms.")
    args = parser.parse_args()

    command = args.command.strip()
    if not command.startswith("@"):
        command = "@" + command

    with serial.Serial(args.port, args.baud, timeout=0.05, dsrdtr=False, rtscts=False) as ser:
        ser.reset_input_buffer()
        ser.write((command + "\r\n").encode("ascii"))
        ser.flush()

        deadline = time.monotonic() + (args.read_ms / 1000.0)
        chunks: list[bytes] = []
        while time.monotonic() < deadline:
            data = ser.read(4096)
            if data:
                chunks.append(data)

    text = b"".join(chunks).decode("utf-8", errors="ignore")
    if text:
        print(text, end="" if text.endswith("\n") else "\n")


if __name__ == "__main__":
    main()
