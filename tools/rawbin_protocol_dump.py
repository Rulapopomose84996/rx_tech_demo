#!/usr/bin/env python3
"""Extract protocol frames from RXRAW01 rawbin files and inspect channel transitions."""

from __future__ import annotations

import argparse
import csv
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional


FILE_MAGIC = b"RXRAW01\0"
FILE_HEADER_STRUCT = struct.Struct("<8sIIQII")
RECORD_HEADER_STRUCT = struct.Struct("<QII")
ETHERTYPE_IPV4 = 0x0800
IPPROTO_UDP = 17
CONTROL_MAGIC = 0x55AAFF00
DATA_MAGIC = 0x55AAFF03
TAIL_MAGIC = 0x55AAFF30


@dataclass
class RawFrameRecord:
    index: int
    timestamp_ns: int
    queue_id: int
    frame: bytes


@dataclass
class ProtocolFrame:
    record_index: int
    timestamp_ns: int
    queue_id: int
    kind: str
    cpi: int
    channel: int
    prt: int
    packet_index: int
    tail: int
    src_ip: str
    dst_ip: str
    src_port: int
    dst_port: int
    frame_length: int


def parse_header(data: bytes) -> int:
    if len(data) < FILE_HEADER_STRUCT.size:
        raise ValueError("rawbin 文件过小，无法读取文件头")
    magic, version, header_size, created_unix_ns, max_frame_bytes, reserved = FILE_HEADER_STRUCT.unpack_from(data, 0)
    if magic != FILE_MAGIC:
        raise ValueError(f"rawbin magic 不匹配: {magic!r}")
    if header_size < FILE_HEADER_STRUCT.size:
        raise ValueError(f"header_size 非法: {header_size}")
    return header_size


def iter_records(data: bytes, header_size: int) -> Iterator[RawFrameRecord]:
    offset = header_size
    index = 0
    while offset + RECORD_HEADER_STRUCT.size <= len(data):
        timestamp_ns, frame_length, queue_id = RECORD_HEADER_STRUCT.unpack_from(data, offset)
        offset += RECORD_HEADER_STRUCT.size
        end = offset + frame_length
        if end > len(data):
            break
        yield RawFrameRecord(index=index, timestamp_ns=timestamp_ns, queue_id=queue_id, frame=data[offset:end])
        offset = end
        index += 1


def parse_protocol_frame(record: RawFrameRecord) -> Optional[ProtocolFrame]:
    frame = record.frame
    if len(frame) < 14 + 20 + 8 + 16:
        return None

    ethertype = struct.unpack_from(">H", frame, 12)[0]
    if ethertype != ETHERTYPE_IPV4:
        return None

    version_ihl = frame[14]
    version = version_ihl >> 4
    ihl = (version_ihl & 0x0F) * 4
    if version != 4 or ihl < 20 or len(frame) < 14 + ihl + 8 + 16:
        return None

    if frame[23] != IPPROTO_UDP:
        return None

    total_length = struct.unpack_from(">H", frame, 16)[0]
    if len(frame) < 14 + total_length:
        return None

    udp_offset = 14 + ihl
    src_port, dst_port, udp_length = struct.unpack_from(">HHH", frame, udp_offset)
    if udp_length < 8 or len(frame) < udp_offset + 8:
        return None

    payload = frame[udp_offset + 8 :]
    if len(payload) < 16:
        return None

    magic = struct.unpack_from("<I", payload, 0)[0]
    if magic not in (CONTROL_MAGIC, DATA_MAGIC, TAIL_MAGIC):
        return None

    cpi = struct.unpack_from("<H", payload, 4)[0]
    channel = struct.unpack_from("<H", payload, 6)[0]
    prt = struct.unpack_from("<H", payload, 8)[0]
    packet_index = struct.unpack_from("<H", payload, 10)[0]
    tail = struct.unpack_from("<I", payload, 12)[0]
    src_ip = ".".join(str(b) for b in frame[26:30])
    dst_ip = ".".join(str(b) for b in frame[30:34])
    kind = {
        CONTROL_MAGIC: "control",
        DATA_MAGIC: "data",
        TAIL_MAGIC: "tail",
    }[magic]

    return ProtocolFrame(
        record_index=record.index,
        timestamp_ns=record.timestamp_ns,
        queue_id=record.queue_id,
        kind=kind,
        cpi=cpi,
        channel=channel,
        prt=prt,
        packet_index=packet_index,
        tail=tail,
        src_ip=src_ip,
        dst_ip=dst_ip,
        src_port=src_port,
        dst_port=dst_port,
        frame_length=len(frame),
    )


def load_protocol_frames(rawbin: Path) -> list[ProtocolFrame]:
    data = rawbin.read_bytes()
    header_size = parse_header(data)
    frames: list[ProtocolFrame] = []
    for record in iter_records(data, header_size):
        protocol = parse_protocol_frame(record)
        if protocol is not None:
            frames.append(protocol)
    return frames


def print_summary(frames: list[ProtocolFrame], limit: int) -> None:
    checked = frames[:limit]
    print(f"protocol_frames_total={len(frames)}")
    print(f"protocol_frames_checked={len(checked)}")

    jumps = []
    previous: Optional[ProtocolFrame] = None
    for index, frame in enumerate(checked):
        if previous is not None and frame.channel != previous.channel:
            jumps.append(
                (
                    index - 1,
                    previous.record_index,
                    previous.kind,
                    previous.channel,
                    index,
                    frame.record_index,
                    frame.kind,
                    frame.channel,
                )
            )
        previous = frame

    print(f"channel_jumps_in_first{limit}={len(jumps)}")
    for item in jumps:
        print(f"jump={item}")

    print("__DETAIL__")
    for index, frame in enumerate(checked):
        print(
            f"{index:03d} "
            f"frame={frame.record_index} "
            f"kind={frame.kind} "
            f"cpi={frame.cpi} "
            f"channel={frame.channel} "
            f"prt={frame.prt} "
            f"packet_index={frame.packet_index} "
            f"src={frame.src_ip}:{frame.src_port} "
            f"dst={frame.dst_ip}:{frame.dst_port}"
        )


def export_csv(frames: list[ProtocolFrame], csv_out: Path) -> None:
    csv_out.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "record_index",
        "timestamp_ns",
        "queue_id",
        "kind",
        "cpi",
        "channel",
        "prt",
        "packet_index",
        "tail",
        "src_ip",
        "dst_ip",
        "src_port",
        "dst_port",
        "frame_length",
    ]
    with csv_out.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for frame in frames:
            writer.writerow(frame.__dict__)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="导出 rawbin 中可识别的协议帧，并检查通道号跳变")
    parser.add_argument("rawbin", help="rawbin 文件路径")
    parser.add_argument("--limit", type=int, default=100, help="检查前多少个协议帧，默认 100")
    parser.add_argument("--csv-out", help="把全部协议帧导出成 CSV")
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()
    rawbin = Path(args.rawbin)
    if not rawbin.is_file():
        print(f"rawbin 文件不存在: {rawbin}", file=sys.stderr)
        return 1

    frames = load_protocol_frames(rawbin)
    print_summary(frames, args.limit)
    if args.csv_out:
        export_csv(frames, Path(args.csv_out))
        print(f"csv_exported={args.csv_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
