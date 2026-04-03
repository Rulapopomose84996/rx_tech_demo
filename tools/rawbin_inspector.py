#!/usr/bin/env python3
"""Inspect RXRAW01 raw frame segments written by RawFrameRecorder."""

from __future__ import annotations

import argparse
import csv
import socket
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
FINAL_TAIL = 0x55AAFF30


@dataclass
class RawFrameFileHeader:
    magic: bytes
    version: int
    header_size: int
    created_unix_ns: int
    max_frame_bytes: int
    reserved: int


@dataclass
class RawFrameRecord:
    index: int
    timestamp_ns: int
    queue_id: int
    frame: bytes


def parse_file_header(data: bytes) -> RawFrameFileHeader:
    if len(data) < FILE_HEADER_STRUCT.size:
        raise ValueError("文件太小，无法读取 rawbin 文件头")
    magic, version, header_size, created_unix_ns, max_frame_bytes, reserved = FILE_HEADER_STRUCT.unpack_from(data, 0)
    if magic != FILE_MAGIC:
        raise ValueError(f"文件 magic 不匹配: {magic!r}")
    if header_size < FILE_HEADER_STRUCT.size:
        raise ValueError(f"header_size 非法: {header_size}")
    return RawFrameFileHeader(magic, version, header_size, created_unix_ns, max_frame_bytes, reserved)


def iter_records(data: bytes, header_size: int) -> Iterator[RawFrameRecord]:
    offset = header_size
    index = 0
    while offset + RECORD_HEADER_STRUCT.size <= len(data):
        timestamp_ns, frame_length, queue_id = RECORD_HEADER_STRUCT.unpack_from(data, offset)
        offset += RECORD_HEADER_STRUCT.size
        end = offset + frame_length
        if end > len(data):
            raise ValueError(f"第 {index} 条记录越界: frame_length={frame_length}, offset={offset}, file_size={len(data)}")
        yield RawFrameRecord(index=index, timestamp_ns=timestamp_ns, queue_id=queue_id, frame=data[offset:end])
        offset = end
        index += 1


def format_hex(data: bytes, limit: int) -> str:
    shown = data[:limit]
    text = " ".join(f"{byte:02X}" for byte in shown)
    if len(data) > limit:
        return text + " ..."
    return text


def try_parse_udp_payload(frame: bytes) -> Optional[dict]:
    if len(frame) < 14 + 20:
        return None

    ethertype = struct.unpack_from(">H", frame, 12)[0]
    if ethertype != ETHERTYPE_IPV4:
        return None

    version_ihl = frame[14]
    version = version_ihl >> 4
    ihl = (version_ihl & 0x0F) * 4
    if version != 4 or ihl < 20 or len(frame) < 14 + ihl + 8:
        return None

    if frame[23] != IPPROTO_UDP:
        return None

    total_length = struct.unpack_from(">H", frame, 16)[0]
    if len(frame) < 14 + total_length:
        return None

    src_ip = socket.inet_ntoa(frame[26:30])
    dst_ip = socket.inet_ntoa(frame[30:34])

    udp_offset = 14 + ihl
    src_port, dst_port, udp_length = struct.unpack_from(">HHH", frame, udp_offset)
    if udp_length < 8 or len(frame) < udp_offset + udp_length:
        return None

    payload = frame[udp_offset + 8 : udp_offset + udp_length]
    result = {
        "src_ip": src_ip,
        "dst_ip": dst_ip,
        "src_port": src_port,
        "dst_port": dst_port,
        "payload": payload,
    }

    if len(payload) >= 16:
        magic = struct.unpack_from("<I", payload, 0)[0]
        result["payload_magic_le"] = magic
        if magic == CONTROL_MAGIC:
            result["protocol_kind"] = "control_table"
            result["cpi"] = struct.unpack_from("<H", payload, 4)[0]
        elif magic == DATA_MAGIC:
            cpi, channel, prt, packet_index = struct.unpack_from("<HHHH", payload, 4)
            tail = struct.unpack_from("<I", payload, 12)[0]
            result["protocol_kind"] = "data_packet"
            result["cpi"] = cpi
            result["channel"] = channel
            result["prt"] = prt
            result["packet_index"] = packet_index
            result["tail"] = tail
            result["tail_is_final"] = tail == FINAL_TAIL
    return result


def iter_csv_rows(data: bytes,
                  header_size: int,
                  start: int,
                  limit: int,
                  protocol_only: bool) -> Iterator[dict]:
    emitted = 0
    for record in iter_records(data, header_size):
        if record.index < start:
            continue
        if emitted >= limit:
            break

        udp = try_parse_udp_payload(record.frame)
        if protocol_only:
            if udp is None or udp.get("payload_magic_le") not in (CONTROL_MAGIC, DATA_MAGIC):
                continue

        row = {
            "record_index": record.index,
            "timestamp_ns": record.timestamp_ns,
            "queue_id": record.queue_id,
            "frame_length": len(record.frame),
            "ethertype": "",
            "src_ip": "",
            "dst_ip": "",
            "src_port": "",
            "dst_port": "",
            "payload_length": "",
            "payload_magic_le": "",
            "protocol_kind": "",
            "cpi": "",
            "channel": "",
            "prt": "",
            "packet_index": "",
            "tail_le": "",
            "tail_is_final": "",
        }

        if len(record.frame) >= 14:
            row["ethertype"] = f"0x{struct.unpack_from('>H', record.frame, 12)[0]:04X}"

        if udp is not None:
            row["src_ip"] = udp["src_ip"]
            row["dst_ip"] = udp["dst_ip"]
            row["src_port"] = udp["src_port"]
            row["dst_port"] = udp["dst_port"]
            row["payload_length"] = len(udp["payload"])

            magic = udp.get("payload_magic_le")
            if magic is not None:
                row["payload_magic_le"] = f"0x{magic:08X}"
            row["protocol_kind"] = udp.get("protocol_kind", "")
            if "cpi" in udp:
                row["cpi"] = udp["cpi"]
            if "channel" in udp:
                row["channel"] = udp["channel"]
            if "prt" in udp:
                row["prt"] = udp["prt"]
            if "packet_index" in udp:
                row["packet_index"] = udp["packet_index"]
            if "tail" in udp:
                row["tail_le"] = f"0x{udp['tail']:08X}"
            if "tail_is_final" in udp:
                row["tail_is_final"] = str(bool(udp["tail_is_final"])).lower()

        yield row
        emitted += 1


def write_csv(data: bytes,
              header_size: int,
              start: int,
              limit: int,
              protocol_only: bool,
              csv_out: str) -> int:
    fieldnames = [
        "record_index",
        "timestamp_ns",
        "queue_id",
        "frame_length",
        "ethertype",
        "src_ip",
        "dst_ip",
        "src_port",
        "dst_port",
        "payload_length",
        "payload_magic_le",
        "protocol_kind",
        "cpi",
        "channel",
        "prt",
        "packet_index",
        "tail_le",
        "tail_is_final",
    ]

    if csv_out == "-":
        stream = sys.stdout
        close_stream = False
    else:
        stream = Path(csv_out).open("w", newline="", encoding="utf-8")
        close_stream = True

    try:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        count = 0
        for row in iter_csv_rows(data, header_size, start, limit, protocol_only):
            writer.writerow(row)
            count += 1
    finally:
        if close_stream:
            stream.close()

    if csv_out != "-":
        print(f"已导出 {count} 条记录到 {csv_out}")
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="解析 RawFrameRecorder 生成的 .rawbin 原始帧文件")
    parser.add_argument("rawbin", help="rawbin 文件路径")
    parser.add_argument("--limit", type=int, default=20, help="最多输出多少条记录，默认 20")
    parser.add_argument("--start", type=int, default=0, help="从第几条记录开始输出，默认 0")
    parser.add_argument("--hex-bytes", type=int, default=32, help="每条记录最多显示多少个十六进制字节，默认 32")
    parser.add_argument("--protocol-only", action="store_true", help="只输出 IPv4/UDP 且负载带 0x55AAFF00/03 魔数的记录")
    parser.add_argument("--csv-out", help="导出 CSV 到指定路径；传 '-' 表示输出到标准输出")
    return parser


def main(argv: list[str]) -> int:
    args = build_arg_parser().parse_args(argv)
    rawbin_path = Path(args.rawbin)
    data = rawbin_path.read_bytes()
    file_header = parse_file_header(data)

    if args.csv_out:
        return write_csv(data, file_header.header_size, args.start, args.limit, args.protocol_only, args.csv_out)

    print("文件头")
    print(f"  path          : {rawbin_path}")
    print(f"  magic         : {file_header.magic!r}")
    print(f"  version       : {file_header.version}")
    print(f"  header_size   : {file_header.header_size}")
    print(f"  created_ns    : {file_header.created_unix_ns}")
    print(f"  max_frame_bytes: {file_header.max_frame_bytes}")
    print(f"  file_size     : {len(data)}")

    shown = 0
    for record in iter_records(data, file_header.header_size):
        if record.index < args.start:
            continue
        if shown >= args.limit:
            break

        udp = try_parse_udp_payload(record.frame)
        if args.protocol_only:
            if udp is None:
                continue
            if udp.get("payload_magic_le") not in (CONTROL_MAGIC, DATA_MAGIC):
                continue

        print(f"\n记录 {record.index}")
        print(f"  timestamp_ns  : {record.timestamp_ns}")
        print(f"  queue_id      : {record.queue_id}")
        print(f"  frame_length  : {len(record.frame)}")
        print(f"  frame_hex     : {format_hex(record.frame, args.hex_bytes)}")

        if udp is None:
            print("  decode        : 非 IPv4/UDP 帧或帧长度不足")
            shown += 1
            continue

        print(
            "  udp           : "
            f"{udp['src_ip']}:{udp['src_port']} -> {udp['dst_ip']}:{udp['dst_port']} payload_len={len(udp['payload'])}"
        )
        print(f"  payload_hex   : {format_hex(udp['payload'], args.hex_bytes)}")

        magic = udp.get("payload_magic_le")
        if magic is None:
            print("  protocol      : UDP payload 不足 16 字节")
        elif magic == CONTROL_MAGIC:
            print(f"  protocol      : control_table cpi={udp['cpi']} magic=0x{magic:08X}")
        elif magic == DATA_MAGIC:
            print(
                "  protocol      : "
                f"data_packet cpi={udp['cpi']} channel={udp['channel']} prt={udp['prt']} "
                f"packet_index={udp['packet_index']} tail=0x{udp['tail']:08X}"
            )
        else:
            print(f"  protocol      : 未识别 payload magic=0x{magic:08X}")

        shown += 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
