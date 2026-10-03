from __future__ import annotations

import argparse
import gzip
import json
import struct
from collections import Counter
from pathlib import Path
from typing import BinaryIO


def text(value: bytes) -> str:
    return value.decode("ascii", errors="replace").strip()


def integer(message: bytes, start: int, size: int) -> int:
    return int.from_bytes(message[start:start + size], "big")


def common_fields(message: bytes) -> dict[str, object]:
    if len(message) < 11:
        return {}
    return {
        "stock_locate": integer(message, 1, 2),
        "tracking_number": integer(message, 3, 2),
        "timestamp_nanoseconds": integer(message, 5, 6),
    }


def decode_message(message: bytes) -> dict[str, object]:
    message_type = chr(message[0]) if message else "?"
    record: dict[str, object] = {"type": message_type, **common_fields(message)}

    if message_type == "S" and len(message) >= 12:
        record["event_code"] = chr(message[11])
    elif message_type == "R" and len(message) >= 19:
        record["stock"] = text(message[11:19])
    elif message_type in {"A", "F"} and len(message) >= 36:
        record.update(
            order_reference=integer(message, 11, 8),
            side=chr(message[19]),
            shares=integer(message, 20, 4),
            stock=text(message[24:32]),
            price_ticks=integer(message, 32, 4),
        )
        if message_type == "F" and len(message) >= 40:
            record["attribution"] = text(message[36:40])
    elif message_type == "D" and len(message) >= 19:
        record["order_reference"] = integer(message, 11, 8)
    elif message_type == "E" and len(message) >= 31:
        record.update(
            order_reference=integer(message, 11, 8),
            executed_shares=integer(message, 19, 4),
            match_number=integer(message, 23, 8),
        )
    elif message_type == "C" and len(message) >= 36:
        record.update(
            order_reference=integer(message, 11, 8),
            executed_shares=integer(message, 19, 4),
            match_number=integer(message, 23, 8),
            printable=chr(message[31]),
            execution_price_ticks=integer(message, 32, 4),
        )
    elif message_type == "U" and len(message) >= 35:
        record.update(
            original_order_reference=integer(message, 11, 8),
            new_order_reference=integer(message, 19, 8),
            shares=integer(message, 27, 4),
            price_ticks=integer(message, 31, 4),
        )
    elif message_type == "P" and len(message) >= 44:
        record.update(
            order_reference=integer(message, 11, 8),
            side=chr(message[19]),
            shares=integer(message, 20, 4),
            stock=text(message[24:32]),
            price_ticks=integer(message, 32, 4),
            match_number=integer(message, 36, 8),
        )
    return record


def messages(stream: BinaryIO):
    while True:
        header = stream.read(2)
        if not header:
            return
        if len(header) != 2:
            raise ValueError("truncated ITCH message length")
        length = struct.unpack(">H", header)[0]
        if length == 0:
            raise ValueError("invalid zero-length ITCH message")
        message = stream.read(length)
        if len(message) != length:
            raise ValueError("truncated ITCH message payload")
        yield message


def parse(
    path: Path,
    sample_limit: int,
    strict: bool,
    scan_all: bool,
) -> tuple[Counter[str], list[dict[str, object]], int, int, bool]:
    counts: Counter[str] = Counter()
    samples: list[dict[str, object]] = []
    message_count = 0
    payload_bytes = 0
    truncated = False
    with gzip.open(path, "rb") as stream:
        try:
            for message in messages(stream):
                message_count += 1
                payload_bytes += len(message) + 2
                message_type = chr(message[0]) if message else "?"
                counts[message_type] += 1
                if len(samples) < sample_limit:
                    samples.append(decode_message(message))
                if not scan_all and len(samples) == sample_limit:
                    break
        except EOFError:
            if strict:
                raise
            truncated = True
    return counts, samples, message_count, payload_bytes, truncated


def main() -> None:
    parser = argparse.ArgumentParser(description="Inspect a NASDAQ ITCH 5.0 gzip stream")
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=Path("data/07302019.NASDAQ_ITCH50.gz"),
    )
    parser.add_argument(
        "-n",
        "--sample",
        "--records",
        dest="sample",
        type=int,
        default=20,
        help="number of initial decoded messages to print",
    )
    parser.add_argument("--summary-only", action="store_true")
    parser.add_argument("--strict", action="store_true", help="fail if the gzip stream is truncated")
    args = parser.parse_args()
    if args.sample < 0:
        parser.error("--sample must be non-negative")

    counts, samples, message_count, payload_bytes, truncated = parse(
        args.path, args.sample, args.strict, args.summary_only
    )
    print(f"file={args.path}")
    print(f"messages={message_count}")
    print(f"framed_bytes={payload_bytes}")
    if truncated:
        print("warning=gzip stream is truncated; showing complete records before the cutoff")
    if not args.summary_only:
        print("samples:")
        for sample in samples:
            print(json.dumps(sample, sort_keys=True))
    print("message_types:")
    for message_type, count in counts.most_common():
        print(f"  {message_type}: {count}")


if __name__ == "__main__":
    main()