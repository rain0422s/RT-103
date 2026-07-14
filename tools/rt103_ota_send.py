#!/usr/bin/env python3
"""Send an RT-103 OTA image over USART1.

The session control commands stay ASCII for debuggability. Image chunks use the
OTAB binary packet format implemented by App/ota_update.c.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import time
from pathlib import Path


OTAB_MAGIC = 0x4241544F
OTAB_HEADER_SIZE = 20
OTAB_PAYLOAD_MAX = 256


def crc32(data: bytes) -> int:
    return binascii.crc32(data) & 0xFFFFFFFF


def read_reply(ser, timeout: float = 5.0) -> str:
    deadline = time.time() + timeout
    line = b""
    while time.time() < deadline:
        part = ser.readline()
        if part:
            line = part.strip()
            break
    if not line:
        raise TimeoutError("timeout waiting for device reply")
    return line.decode("ascii", errors="replace")


def send_line(ser, line: str, expect_prefix: str = "OK") -> str:
    ser.write((line + "\r\n").encode("ascii"))
    ser.flush()
    reply = read_reply(ser)
    if not reply.startswith(expect_prefix):
        raise RuntimeError(f"{line!r} failed: {reply}")
    return reply


def send_image(args: argparse.Namespace) -> None:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: pip install pyserial") from exc

    image = Path(args.image).read_bytes()
    image_crc = crc32(image)
    slot_part = f" slot={args.slot}" if args.slot else ""
    min_part = f" min={args.min_version}" if args.min_version is not None else ""
    begin = (
        f"OTA BEGIN size={len(image)} crc={image_crc:08X} "
        f"version={args.version}{min_part}{slot_part}"
    )

    with serial.Serial(args.port, args.baud, timeout=args.timeout) as ser:
        time.sleep(args.settle)
        if args.abort_first:
            send_line(ser, "OTA ABORT")
        print(send_line(ser, begin))

        sequence = 0
        for offset in range(0, len(image), OTAB_PAYLOAD_MAX):
            payload = image[offset:offset + OTAB_PAYLOAD_MAX]
            header = struct.pack(
                "<IHHIII",
                OTAB_MAGIC,
                OTAB_HEADER_SIZE,
                len(payload),
                sequence,
                offset,
                crc32(payload),
            )
            ser.write(header + payload)
            ser.flush()
            reply = read_reply(ser, timeout=args.timeout)
            if not reply.startswith("OK OTAB DATA"):
                raise RuntimeError(
                    f"chunk sequence={sequence} offset={offset} failed: {reply}"
                )
            if args.verbose:
                print(f"{reply} off={offset} len={len(payload)}")
            sequence += 1

        print(send_line(ser, "OTA END"))
        if args.apply:
            print(send_line(ser, "OTA APPLY"))
        else:
            print(send_line(ser, "OTA STATUS?"))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM5", help="serial port, default COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--image", required=True, help="app_a/app_b .bin path")
    parser.add_argument("--version", type=int, required=True)
    parser.add_argument("--min-version", type=int)
    parser.add_argument("--slot", choices=("A", "B", "0", "1"))
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--settle", type=float, default=0.2)
    parser.add_argument("--abort-first", action="store_true", default=True)
    parser.add_argument("--no-abort-first", dest="abort_first", action="store_false")
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    send_image(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
