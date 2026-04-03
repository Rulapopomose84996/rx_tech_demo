#!/usr/bin/env python3
"""Capture sender-side traffic to pcap and decode rx_tech_demo protocol fields."""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import ipaddress
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional


PCAP_GLOBAL_HEADER = struct.Struct("<IHHIIII")
PCAP_PACKET_HEADER = struct.Struct("<IIII")

ETHERTYPE_IPV4 = 0x0800
IPPROTO_UDP = 17
CONTROL_MAGIC = 0x55AAFF00
DATA_MAGIC = 0x55AAFF03
TAIL_MAGIC = 0x55AAFF30


@dataclass
class CapturedFrame:
    index: int
    timestamp_sec: int
    timestamp_usec: int
    data: bytes


@dataclass
class ProtocolFrame:
    capture_index: int
    timestamp_sec: int
    timestamp_usec: int
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


def timestamp_label() -> str:
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def parse_pcap_frames(pcap_path: Path) -> Iterator[CapturedFrame]:
    data = pcap_path.read_bytes()
    if len(data) < PCAP_GLOBAL_HEADER.size:
        raise ValueError("pcap 文件过小")

    magic, version_major, version_minor, tz, sigfigs, snaplen, network = PCAP_GLOBAL_HEADER.unpack_from(data, 0)
    if magic != 0xA1B2C3D4:
        raise ValueError(f"不支持的 pcap magic: 0x{magic:08X}")
    if network != 1:
        raise ValueError(f"当前只支持 Ethernet pcap，network={network}")

    offset = PCAP_GLOBAL_HEADER.size
    index = 0
    while offset + PCAP_PACKET_HEADER.size <= len(data):
        ts_sec, ts_usec, incl_len, orig_len = PCAP_PACKET_HEADER.unpack_from(data, offset)
        offset += PCAP_PACKET_HEADER.size
        end = offset + incl_len
        if end > len(data):
            break
        yield CapturedFrame(index=index, timestamp_sec=ts_sec, timestamp_usec=ts_usec, data=data[offset:end])
        offset = end
        index += 1


def parse_protocol_frame(frame: CapturedFrame) -> Optional[ProtocolFrame]:
    data = frame.data
    if len(data) < 14 + 20 + 8 + 16:
        return None

    ethertype = struct.unpack_from(">H", data, 12)[0]
    if ethertype != ETHERTYPE_IPV4:
        return None

    version_ihl = data[14]
    version = version_ihl >> 4
    ihl = (version_ihl & 0x0F) * 4
    if version != 4 or ihl < 20 or len(data) < 14 + ihl + 8 + 16:
        return None

    if data[23] != IPPROTO_UDP:
        return None

    total_length = struct.unpack_from(">H", data, 16)[0]
    if len(data) < 14 + total_length:
        return None

    udp_offset = 14 + ihl
    src_port, dst_port, udp_length = struct.unpack_from(">HHH", data, udp_offset)
    if udp_length < 8 or len(data) < udp_offset + 8:
        return None

    payload = data[udp_offset + 8 :]
    if len(payload) < 16:
        return None

    magic = struct.unpack_from("<I", payload, 0)[0]
    if magic not in (CONTROL_MAGIC, DATA_MAGIC, TAIL_MAGIC):
        return None

    kind = {
        CONTROL_MAGIC: "control",
        DATA_MAGIC: "data",
        TAIL_MAGIC: "tail",
    }[magic]

    return ProtocolFrame(
        capture_index=frame.index,
        timestamp_sec=frame.timestamp_sec,
        timestamp_usec=frame.timestamp_usec,
        kind=kind,
        cpi=struct.unpack_from("<H", payload, 4)[0],
        channel=struct.unpack_from("<H", payload, 6)[0],
        prt=struct.unpack_from("<H", payload, 8)[0],
        packet_index=struct.unpack_from("<H", payload, 10)[0],
        tail=struct.unpack_from("<I", payload, 12)[0],
        src_ip=str(ipaddress.IPv4Address(data[26:30])),
        dst_ip=str(ipaddress.IPv4Address(data[30:34])),
        src_port=src_port,
        dst_port=dst_port,
        frame_length=len(data),
    )


def load_protocol_frames(pcap_path: Path) -> list[ProtocolFrame]:
    frames: list[ProtocolFrame] = []
    for frame in parse_pcap_frames(pcap_path):
        parsed = parse_protocol_frame(frame)
        if parsed is not None:
            frames.append(parsed)
    return frames


def analyze_channel_jumps(frames: list[ProtocolFrame], limit: int) -> list[tuple]:
    checked = frames[:limit]
    jumps = []
    previous: Optional[ProtocolFrame] = None
    for index, frame in enumerate(checked):
        if previous is not None and frame.channel != previous.channel:
            jumps.append(
                (
                    index - 1,
                    previous.capture_index,
                    previous.kind,
                    previous.channel,
                    index,
                    frame.capture_index,
                    frame.kind,
                    frame.channel,
                )
            )
        previous = frame
    return jumps


def write_csv(frames: list[ProtocolFrame], csv_path: Path) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "capture_index",
        "timestamp_sec",
        "timestamp_usec",
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
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for frame in frames:
            writer.writerow(frame.__dict__)


def write_summary(frames: list[ProtocolFrame], limit: int, summary_path: Path) -> None:
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    jumps = analyze_channel_jumps(frames, limit)
    with summary_path.open("w", encoding="utf-8") as handle:
        handle.write(f"protocol_frames_total={len(frames)}\n")
        handle.write(f"protocol_frames_checked={min(limit, len(frames))}\n")
        handle.write(f"channel_jumps_in_first{limit}={len(jumps)}\n")
        for item in jumps:
            handle.write(f"jump={item}\n")
        handle.write("__DETAIL__\n")
        for index, frame in enumerate(frames[:limit]):
            handle.write(
                f"{index:03d} "
                f"frame={frame.capture_index} "
                f"kind={frame.kind} "
                f"cpi={frame.cpi} "
                f"channel={frame.channel} "
                f"prt={frame.prt} "
                f"packet_index={frame.packet_index} "
                f"src={frame.src_ip}:{frame.src_port} "
                f"dst={frame.dst_ip}:{frame.dst_port}\n"
            )


def run_tcpdump_capture(args: argparse.Namespace, pcap_path: Path) -> None:
    pcap_path.parent.mkdir(parents=True, exist_ok=True)
    bpf = (
        f"arp or (udp and src host {args.src_ip} and dst host {args.dst_ip} and dst port {args.dst_port})"
    )
    command = [
        "tcpdump",
        "-i",
        args.iface,
        "-nn",
        "-s",
        "0",
        "-w",
        str(pcap_path),
        bpf,
    ]
    if args.packet_count is not None:
        command.extend(["-c", str(args.packet_count)])
    if args.duration is not None:
        command = ["timeout", str(args.duration)] + command

    print("capture_command=" + " ".join(command))
    subprocess.run(command, check=True)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="抓发送端 pcap 并解出 rx_tech_demo 协议字段")
    parser.add_argument("--iface", help="发送端出口网卡名；不提供时只做离线解析")
    parser.add_argument("--src-ip", default="172.20.11.222")
    parser.add_argument("--dst-ip", default="172.20.11.100")
    parser.add_argument("--dst-port", type=int, default=9999)
    parser.add_argument("--duration", type=int, default=10, help="抓包秒数")
    parser.add_argument("--packet-count", type=int, help="抓到指定包数后停止")
    parser.add_argument("--limit", type=int, default=100, help="摘要里显示前多少个协议帧")
    parser.add_argument("--pcap-in", help="离线解析已有 pcap")
    parser.add_argument("--output-root", default="analysis", help="输出根目录")
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    if not args.iface and not args.pcap_in:
        parser.error("必须提供 --iface 或 --pcap-in")

    output_root = Path(args.output_root)
    run_dir = output_root / f"sender_capture_{timestamp_label()}"
    pcap_path = Path(args.pcap_in) if args.pcap_in else run_dir / "capture.pcap"

    if args.iface:
        run_tcpdump_capture(args, pcap_path)
    elif not pcap_path.is_file():
        print(f"pcap 文件不存在: {pcap_path}", file=sys.stderr)
        return 1

    frames = load_protocol_frames(pcap_path)
    csv_path = run_dir / "protocol_frames.csv"
    summary_path = run_dir / "summary.txt"
    write_csv(frames, csv_path)
    write_summary(frames, args.limit, summary_path)

    print(f"pcap={pcap_path}")
    print(f"protocol_frames_total={len(frames)}")
    print(f"csv={csv_path}")
    print(f"summary={summary_path}")
    jumps = analyze_channel_jumps(frames, args.limit)
    print(f"channel_jumps_in_first{args.limit}={len(jumps)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
