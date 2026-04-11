#!/usr/bin/env python3
"""三通道波位 UDP 数据包离线解析与中文可视化工具。"""

from __future__ import annotations

import argparse
import html
import json
import os
import struct
import sys
from collections import OrderedDict, defaultdict
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Tuple


PACKET_HEADER = b"\x55\xAA\xFF\x03"
PACKET_HEADER_VALUE = 0x55AAFF03
FINAL_PACKET_TAIL = 0x55AAFF30
PCAP_ETHERNET = 1
DEFAULT_CHANNEL_NAMES = OrderedDict(
    [
        (0, "和路"),
        (1, "俯仰差"),
        (2, "方位差"),
        (3, "辅助通道"),
    ]
)


@dataclass
class PacketRecord:
    raw_length: int
    raw_payload: bytes
    cpi: int
    channel: int
    channel_name: str
    prt: int
    packet_index: int
    tail: int
    tail_hex: str
    iq_count: int
    payload_bytes: int
    zero_padding_bytes: int
    data_payload: bytes


def take_first_n_cpis(packets: Iterable[PacketRecord], cpi_count: int) -> List[PacketRecord]:
    if cpi_count <= 0:
        return list(packets)

    selected_cpis = []
    selected_set = set()
    filtered = []
    for packet in packets:
        if packet.cpi not in selected_set:
            if len(selected_cpis) >= cpi_count:
                break
            selected_cpis.append(packet.cpi)
            selected_set.add(packet.cpi)
        if packet.cpi in selected_set:
            filtered.append(packet)
    return filtered


def parse_packet(
    payload: bytes,
    endian: str = "little",
    expected_payload_bytes: int = 2048,
    packets_per_channel: int = 9,
    full_packet_iq: int = 508,
    last_packet_iq: int = 476,
    packet_index_base: int = 1,
    channel_names: Optional[Dict[int, str]] = None,
) -> PacketRecord:
    if len(payload) < 16:
        raise ValueError("UDP 负载不足 16 字节，无法解析包头")
    order = "<" if endian == "little" else ">"
    header_value = struct.unpack(order + "I", payload[:4])[0]
    if header_value != PACKET_HEADER_VALUE:
        raise ValueError("UDP 负载包头不是 0x55AAFF03")
    if expected_payload_bytes > 0 and len(payload) != expected_payload_bytes:
        raise ValueError("UDP 负载长度不是期望的 {} 字节".format(expected_payload_bytes))

    cpi, channel, prt, packet_index = struct.unpack(order + "HHHH", payload[4:12])
    tail = struct.unpack(order + "I", payload[12:16])[0]

    data_bytes = max(0, len(payload) - 16)
    last_packet_index = packet_index_base + packets_per_channel - 1
    if packet_index == last_packet_index:
        iq_count = min(last_packet_iq, data_bytes // 4)
    else:
        iq_count = min(full_packet_iq, data_bytes // 4)
    effective_bytes = iq_count * 4
    zero_padding_bytes = max(0, data_bytes - effective_bytes)

    channels = dict(DEFAULT_CHANNEL_NAMES)
    if channel_names:
        channels.update(channel_names)
    channel_name = channels.get(channel, "未知通道({})".format(channel))

    return PacketRecord(
        raw_length=len(payload),
        raw_payload=payload,
        cpi=cpi,
        channel=channel,
        channel_name=channel_name,
        prt=prt,
        packet_index=packet_index,
        tail=tail,
        tail_hex="0x{:08X}".format(tail),
        iq_count=iq_count,
        payload_bytes=data_bytes,
        zero_padding_bytes=zero_padding_bytes,
        data_payload=payload[16:],
    )


def analyze_packets(
    packets: Iterable[PacketRecord],
    expected_channels: int = 3,
    packets_per_channel: int = 9,
    packet_index_base: int = 1,
    channel_names: Optional[Dict[int, str]] = None,
    invalid_packets: int = 0,
) -> Dict[str, object]:
    packet_list = list(packets)
    channels = dict(DEFAULT_CHANNEL_NAMES)
    if channel_names:
        channels.update(channel_names)

    channel_stats = OrderedDict()
    prt_map = defaultdict(lambda: defaultdict(set))
    cpi_set = set()
    packet_index_hist = defaultdict(int)
    warnings = []
    final_tail_packets = 0
    cpi_packet_counts = defaultdict(int)
    cpi_prts = defaultdict(set)
    cpi_channels = defaultdict(lambda: defaultdict(int))

    valid_packet_indexes = list(range(packet_index_base, packet_index_base + packets_per_channel))

    for channel_id in range(expected_channels):
        channel_label = channels.get(channel_id, "通道{}".format(channel_id))
        channel_stats[channel_label] = {
            "通道号": channel_id,
            "包数": 0,
            "IQ 总数": 0,
            "数据字节": 0,
            "补零字节": 0,
            "PRT 数": 0,
        }

    for packet in packet_list:
        cpi_set.add(packet.cpi)
        cpi_packet_counts[packet.cpi] += 1
        cpi_prts[packet.cpi].add(packet.prt)
        cpi_channels[packet.cpi][packet.channel_name] += 1
        packet_index_hist[packet.packet_index] += 1
        if packet.tail == FINAL_PACKET_TAIL:
            final_tail_packets += 1

        if packet.channel_name not in channel_stats:
            channel_stats[packet.channel_name] = {
                "通道号": packet.channel,
                "包数": 0,
                "IQ 总数": 0,
                "数据字节": 0,
                "补零字节": 0,
                "PRT 数": 0,
            }

        stat = channel_stats[packet.channel_name]
        stat["包数"] += 1
        stat["IQ 总数"] += packet.iq_count
        stat["数据字节"] += packet.payload_bytes
        stat["补零字节"] += packet.zero_padding_bytes
        prt_map[(packet.cpi, packet.prt)][packet.channel_name].add(packet.packet_index)

        if packet.packet_index not in valid_packet_indexes:
            warnings.append(
                "PRT {} 通道 {} 出现越界包计数 {}".format(
                    packet.prt, packet.channel_name, packet.packet_index
                )
            )

    prt_rows = []
    complete_prt_count = 0
    for cpi, prt in sorted(prt_map):
        channel_coverage = OrderedDict()
        prt_complete = True
        for channel_id in range(expected_channels):
            channel_label = channels.get(channel_id, "通道{}".format(channel_id))
            packet_indexes = sorted(prt_map[(cpi, prt)].get(channel_label, set()))
            received = len(packet_indexes)
            missing = [index for index in valid_packet_indexes if index not in packet_indexes]
            if missing:
                prt_complete = False
            channel_coverage[channel_label] = {
                "已收包数": received,
                "缺失包序号": missing,
            }
        if prt_complete:
            complete_prt_count += 1
        prt_rows.append(
            {
                "CPI": cpi,
                "PRT": prt,
                "完整": prt_complete,
                "通道覆盖": channel_coverage,
            }
        )

    for channel_label, stat in channel_stats.items():
        stat["PRT 数"] = sum(1 for prt_info in prt_rows if channel_label in prt_info["通道覆盖"])

    summary = OrderedDict()
    summary["总包数"] = len(packet_list)
    summary["异常包数"] = invalid_packets
    summary["CPI 数"] = len(cpi_set)
    summary["PRT 数"] = len(prt_rows)
    summary["完整 PRT 数"] = complete_prt_count
    summary["最终包尾数量"] = final_tail_packets
    summary["每通道包数"] = packets_per_channel
    summary["包序号起始值"] = packet_index_base
    summary["通道统计"] = channel_stats
    summary["PRT 完整度"] = prt_rows
    summary["包计数直方图"] = OrderedDict(sorted(packet_index_hist.items()))
    summary["CPI 明细"] = [
        OrderedDict(
            [
                ("CPI", cpi),
                ("包数", cpi_packet_counts[cpi]),
                ("PRT 数", len(cpi_prts[cpi])),
                ("通道分布", OrderedDict(sorted(cpi_channels[cpi].items()))),
            ]
        )
        for cpi in sorted(cpi_set)
    ]
    summary["告警"] = warnings
    return summary


def render_terminal_report(summary: Dict[str, object]) -> str:
    lines = []
    lines.append("UDP 数据包解析摘要")
    lines.append("=" * 72)
    for key in ["总包数", "异常包数", "CPI 数", "PRT 数", "完整 PRT 数", "最终包尾数量"]:
        lines.append("{:<12} {}".format(key + "：", summary[key]))

    if summary.get("CPI 明细"):
        lines.append("")
        lines.append("前序 CPI 概览")
        lines.append("-" * 72)
        for item in summary["CPI 明细"]:
            channel_text = " | ".join(
                "{}:{}".format(name, count) for name, count in item["通道分布"].items()
            )
            lines.append(
                "CPI {:>6} 包数 {:>6} PRT 数 {:>4} {}".format(
                    item["CPI"], item["包数"], item["PRT 数"], channel_text
                )
            )

    lines.append("")
    lines.append("通道统计")
    lines.append("-" * 72)
    header = "{:<10} {:>8} {:>10} {:>12} {:>12}".format("通道", "包数", "IQ 总数", "数据字节", "补零字节")
    lines.append(header)
    for channel_name, stat in summary["通道统计"].items():
        lines.append(
            "{:<10} {:>8} {:>10} {:>12} {:>12}".format(
                channel_name,
                stat["包数"],
                stat["IQ 总数"],
                stat["数据字节"],
                stat["补零字节"],
            )
        )

    lines.append("")
    lines.append("PRT 完整度")
    lines.append("-" * 72)
    for row in summary["PRT 完整度"]:
        status = "完整" if row["完整"] else "缺包"
        parts = []
        for channel_name, coverage in row["通道覆盖"].items():
            if coverage["缺失包序号"]:
                parts.append(
                    "{}: {}/{}，缺失 {}".format(
                        channel_name,
                        coverage["已收包数"],
                        summary.get("每通道包数", 9),
                        ",".join(str(v) for v in coverage["缺失包序号"]),
                    )
                )
            else:
                parts.append(
                    "{}: {}/{}".format(
                        channel_name, coverage["已收包数"], summary.get("每通道包数", 9)
                    )
                )
        lines.append(
            "CPI {:>6} PRT {:>6} [{}] {}".format(
                row.get("CPI", "-"), row["PRT"], status, " | ".join(parts)
            )
        )

    if summary["告警"]:
        lines.append("")
        lines.append("告警")
        lines.append("-" * 72)
        lines.extend(summary["告警"])

    return "\n".join(lines)


def render_html_report(summary: Dict[str, object], title: str = "UDP 数据包中文解析报告") -> str:
    channel_rows = []
    max_packets = 1
    for stat in summary["通道统计"].values():
        max_packets = max(max_packets, int(stat["包数"]))

    for channel_name, stat in summary["通道统计"].items():
        percent = (float(stat["包数"]) / float(max_packets)) * 100.0
        channel_rows.append(
            """
            <tr>
              <td>{channel_name}</td>
              <td>{packet_count}</td>
              <td>{iq_count}</td>
              <td>{data_bytes}</td>
              <td>{padding_bytes}</td>
              <td>
                <div class="bar"><span style="width:{percent:.2f}%"></span></div>
              </td>
            </tr>
            """.format(
                channel_name=html.escape(channel_name),
                packet_count=stat["包数"],
                iq_count=stat["IQ 总数"],
                data_bytes=stat["数据字节"],
                padding_bytes=stat["补零字节"],
                percent=percent,
            )
        )

    prt_rows = []
    for row in summary["PRT 完整度"]:
        coverage_text = []
        for channel_name, coverage in row["通道覆盖"].items():
            if coverage["缺失包序号"]:
                coverage_text.append(
                    "{}：{}/9，缺失 {}".format(
                        channel_name,
                        coverage["已收包数"],
                        ",".join(str(v) for v in coverage["缺失包序号"]),
                    )
                )
            else:
                coverage_text.append("{}：{}/9".format(channel_name, coverage["已收包数"]))
        prt_rows.append(
            """
            <tr>
              <td>{cpi}</td>
              <td>{prt}</td>
              <td class="{status_class}">{status_text}</td>
              <td>{coverage}</td>
            </tr>
            """.format(
                cpi=row.get("CPI", "-"),
                prt=row["PRT"],
                status_class="ok" if row["完整"] else "warn",
                status_text="完整" if row["完整"] else "缺包",
                coverage=html.escape("；".join(coverage_text)),
            )
        )

    warning_items = []
    for warning in summary["告警"]:
        warning_items.append("<li>{}</li>".format(html.escape(str(warning))))
    if not warning_items:
        warning_items.append("<li>未发现额外协议告警。</li>")

    histogram_items = []
    for packet_index, count in summary["包计数直方图"].items():
        histogram_items.append(
            "<tr><td>{}</td><td>{}</td></tr>".format(packet_index, count)
        )

    cards_html = []
    for key in ["总包数", "异常包数", "CPI 数", "PRT 数", "完整 PRT 数", "最终包尾数量"]:
        cards_html.append(
            '<div class="card"><div class="label">{}</div><div class="value">{}</div></div>'.format(
                html.escape(key), summary[key]
            )
        )

    return """<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>{title}</title>
  <style>
    body {{
      margin: 0;
      font-family: "Noto Sans SC", "Microsoft YaHei", sans-serif;
      background: linear-gradient(135deg, #0d3b66, #1b998b 60%, #f4d35e 140%);
      color: #16324f;
    }}
    .wrap {{
      max-width: 1200px;
      margin: 0 auto;
      padding: 24px;
    }}
    .hero, .panel {{
      background: rgba(255, 255, 255, 0.92);
      border-radius: 18px;
      padding: 20px;
      box-shadow: 0 18px 50px rgba(0, 0, 0, 0.18);
      margin-bottom: 18px;
    }}
    h1, h2 {{
      margin-top: 0;
    }}
    .summary-grid {{
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 12px;
      margin-top: 16px;
    }}
    .card {{
      border-radius: 14px;
      background: #f7fbfc;
      padding: 14px;
      border: 1px solid #d9ebef;
    }}
    .label {{
      font-size: 13px;
      color: #406882;
      margin-bottom: 6px;
    }}
    .value {{
      font-size: 28px;
      font-weight: 700;
    }}
    table {{
      width: 100%;
      border-collapse: collapse;
      margin-top: 10px;
    }}
    th, td {{
      text-align: left;
      border-bottom: 1px solid #e2ecef;
      padding: 10px 8px;
      vertical-align: top;
    }}
    .bar {{
      width: 100%;
      height: 12px;
      background: #dceff2;
      border-radius: 999px;
      overflow: hidden;
    }}
    .bar span {{
      display: block;
      height: 100%;
      background: linear-gradient(90deg, #1b998b, #ef476f);
    }}
    .ok {{
      color: #18794e;
      font-weight: 700;
    }}
    .warn {{
      color: #b54708;
      font-weight: 700;
    }}
    ul {{
      padding-left: 22px;
    }}
  </style>
</head>
<body>
  <div class="wrap">
    <section class="hero">
      <h1>{title}</h1>
      <p>用于离线解析三通道 UDP 波位数据包，按通道和 PRT 输出中文摘要。默认假设每通道 9 包，前三个通道依次为和路、俯仰差、方位差。</p>
      <div class="summary-grid">
        {cards_html}
      </div>
    </section>

    <section class="panel">
      <h2>通道统计</h2>
      <table>
        <thead>
          <tr>
            <th>通道</th>
            <th>包数</th>
            <th>IQ 总数</th>
            <th>数据字节</th>
            <th>补零字节</th>
            <th>可视化</th>
          </tr>
        </thead>
        <tbody>
          {channel_rows}
        </tbody>
      </table>
    </section>

    <section class="panel">
      <h2>PRT 完整度</h2>
      <table>
        <thead>
          <tr>
            <th>CPI</th>
            <th>PRT</th>
            <th>状态</th>
            <th>通道覆盖</th>
          </tr>
        </thead>
        <tbody>
          {prt_rows}
        </tbody>
      </table>
    </section>

    <section class="panel">
      <h2>包计数直方图</h2>
      <table>
        <thead>
          <tr>
            <th>包计数</th>
            <th>出现次数</th>
          </tr>
        </thead>
        <tbody>
          {histogram_rows}
        </tbody>
      </table>
    </section>

    <section class="panel">
      <h2>协议告警</h2>
      <ul>
        {warning_items}
      </ul>
    </section>
  </div>
</body>
</html>
""".format(
        title=html.escape(title),
        cards_html="".join(cards_html),
        channel_rows="".join(channel_rows),
        prt_rows="".join(prt_rows),
        histogram_rows="".join(histogram_items),
        warning_items="".join(warning_items),
    )


def _read_u32(data: bytes, offset: int, little_endian: bool) -> int:
    if little_endian:
        return (
            data[offset]
            | (data[offset + 1] << 8)
            | (data[offset + 2] << 16)
            | (data[offset + 3] << 24)
        )
    return (
        (data[offset] << 24)
        | (data[offset + 1] << 16)
        | (data[offset + 2] << 8)
        | data[offset + 3]
    )


def _parse_pcap_records(path: str) -> Iterable[bytes]:
    with open(path, "rb") as fh:
        global_header = fh.read(24)
        if len(global_header) != 24:
            raise ValueError("PCAP 全局头不足 24 字节")

        magic_le = _read_u32(global_header, 0, True)
        if magic_le in (0xA1B2C3D4, 0xA1B23C4D):
            little_endian = True
        else:
            magic_be = _read_u32(global_header, 0, False)
            if magic_be not in (0xA1B2C3D4, 0xA1B23C4D):
                raise ValueError("仅支持标准 PCAP，不支持 pcapng")
            little_endian = False

        network = _read_u32(global_header, 20, little_endian)
        if network != PCAP_ETHERNET:
            raise ValueError("当前仅支持 DLT_EN10MB 以太网链路类型")

        fragments = {}
        while True:
            record_header = fh.read(16)
            if not record_header:
                break
            if len(record_header) != 16:
                raise ValueError("PCAP 记录头被截断")
            incl_len = _read_u32(record_header, 8, little_endian)
            packet = fh.read(incl_len)
            if len(packet) != incl_len:
                raise ValueError("PCAP 数据包被截断")
            for udp_payload in _extract_udp_payloads(packet, fragments):
                if udp_payload is not None:
                    yield udp_payload


def _extract_udp_payloads(frame: bytes, fragments: Dict[Tuple[bytes, bytes, int, int], Dict[str, object]]) -> List[bytes]:
    if len(frame) < 14 + 20 + 8:
        return []
    eth_type = struct.unpack("!H", frame[12:14])[0]
    if eth_type != 0x0800:
        return []

    ip_offset = 14
    version_ihl = frame[ip_offset]
    version = version_ihl >> 4
    ihl = (version_ihl & 0x0F) * 4
    if version != 4 or ihl < 20:
        return []
    total_length = struct.unpack("!H", frame[ip_offset + 2 : ip_offset + 4])[0]
    if len(frame) < ip_offset + total_length:
        return []
    protocol = frame[ip_offset + 9]
    if protocol != 17:
        return []
    src = frame[ip_offset + 12 : ip_offset + 16]
    dst = frame[ip_offset + 16 : ip_offset + 20]
    flags_and_offset = struct.unpack("!H", frame[ip_offset + 6 : ip_offset + 8])[0]
    more_fragments = bool(flags_and_offset & 0x2000)
    fragment_offset_bytes = (flags_and_offset & 0x1FFF) * 8
    identification = struct.unpack("!H", frame[ip_offset + 4 : ip_offset + 6])[0]
    ip_payload = frame[ip_offset + ihl : ip_offset + total_length]

    if fragment_offset_bytes == 0 and not more_fragments:
        if len(ip_payload) < 8:
            return []
        udp_length = struct.unpack("!H", ip_payload[4:6])[0]
        return [ip_payload[8 : 8 + max(0, udp_length - 8)]]

    key = (src, dst, identification, protocol)
    entry = fragments.setdefault(
        key, {"pieces": {}, "total_length": None}
    )
    entry["pieces"][fragment_offset_bytes] = ip_payload
    if not more_fragments:
        entry["total_length"] = fragment_offset_bytes + len(ip_payload)

    total_payload_length = entry["total_length"]
    if total_payload_length is None:
        return []

    pieces = entry["pieces"]
    assembled = bytearray()
    offset = 0
    while offset < total_payload_length:
        piece = pieces.get(offset)
        if piece is None:
            return []
        assembled.extend(piece)
        offset += len(piece)

    del fragments[key]
    if len(assembled) < 8:
        return []
    udp_length = struct.unpack("!H", assembled[4:6])[0]
    return [bytes(assembled[8 : 8 + max(0, udp_length - 8)])]


def _load_raw_payloads(path: str, expected_payload_bytes: int) -> Iterable[bytes]:
    with open(path, "rb") as fh:
        data = fh.read()
    if len(data) % expected_payload_bytes != 0:
        raise ValueError(
            "原始二进制长度 {} 不是 {} 的整数倍".format(len(data), expected_payload_bytes)
        )
    for offset in range(0, len(data), expected_payload_bytes):
        yield data[offset : offset + expected_payload_bytes]


def _load_hex_dump_payloads(path: str, expected_payload_bytes: int) -> Iterable[bytes]:
    hex_parts = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            for token in line.replace(",", " ").split():
                token = token.strip()
                if len(token) == 2:
                    try:
                        int(token, 16)
                    except ValueError:
                        continue
                    hex_parts.append(token)

    raw = bytes.fromhex("".join(hex_parts))
    if len(raw) % expected_payload_bytes != 0:
        raise ValueError(
            "十六进制文本还原后长度 {} 不是 {} 的整数倍".format(len(raw), expected_payload_bytes)
        )
    for offset in range(0, len(raw), expected_payload_bytes):
        yield raw[offset : offset + expected_payload_bytes]


def load_payloads(args: argparse.Namespace) -> Iterable[bytes]:
    if args.pcap:
        return _parse_pcap_records(args.pcap)
    if args.raw:
        return _load_raw_payloads(args.raw, args.packet_bytes)
    if args.hex_dump:
        return _load_hex_dump_payloads(args.hex_dump, args.packet_bytes)
    raise ValueError("必须提供 --pcap、--raw 或 --hex-dump 之一")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="解析波位 UDP 数据包并生成中文可视化报告。")
    parser.add_argument("--pcap", help="PCAP 文件路径，脚本会自动提取 UDP 负载。")
    parser.add_argument("--raw", help="原始 UDP 负载文件路径，要求按 2048 字节连续拼接。")
    parser.add_argument("--hex-dump", help="十六进制文本文件路径，支持 Wireshark 导出的字节文本。")
    parser.add_argument("--packet-bytes", type=int, default=2048, help="单个 UDP 负载长度，默认 2048。")
    parser.add_argument("--expected-channels", type=int, default=3, help="期望通道数，默认 3。")
    parser.add_argument("--packets-per-channel", type=int, default=9, help="每通道包数，默认 9。")
    parser.add_argument("--full-packet-iq", type=int, default=508, help="前 8 包的 IQ 数，默认 508。")
    parser.add_argument("--last-packet-iq", type=int, default=476, help="最后 1 包的 IQ 数，默认 476。")
    parser.add_argument("--packet-index-base", type=int, default=1, help="包计数起始值，默认 1。")
    parser.add_argument("--first-cpi-count", type=int, default=0, help="仅保留按到达顺序出现的前 N 个 CPI，0 表示不过滤。")
    parser.add_argument("--endian", choices=["little", "big"], default="little", help="多字节字段字节序，默认 little。")
    parser.add_argument("--html-out", default="udp_packet_report_cn.html", help="HTML 报告输出路径。")
    parser.add_argument("--json-out", default="", help="可选：将摘要同时输出为 JSON 文件。")
    parser.add_argument("--title", default="三通道 UDP 解析报告", help="HTML 报告标题。")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    parsed_packets = []
    invalid_packets = 0

    try:
        for payload in load_payloads(args):
            try:
                parsed_packets.append(
                    parse_packet(
                        payload,
                        endian=args.endian,
                        expected_payload_bytes=args.packet_bytes,
                        packets_per_channel=args.packets_per_channel,
                        full_packet_iq=args.full_packet_iq,
                        last_packet_iq=args.last_packet_iq,
                        packet_index_base=args.packet_index_base,
                    )
                )
            except ValueError:
                invalid_packets += 1

        if args.first_cpi_count > 0:
            parsed_packets = take_first_n_cpis(parsed_packets, args.first_cpi_count)

        summary = analyze_packets(
            parsed_packets,
            expected_channels=args.expected_channels,
            packets_per_channel=args.packets_per_channel,
            packet_index_base=args.packet_index_base,
            invalid_packets=invalid_packets,
        )
        report = render_terminal_report(summary)
        print(report)

        html_text = render_html_report(summary, title=args.title)
        with open(args.html_out, "w", encoding="utf-8") as fh:
            fh.write(html_text)
        print("\nHTML 报告已生成：{}".format(os.path.abspath(args.html_out)))

        if args.json_out:
            with open(args.json_out, "w", encoding="utf-8") as fh:
                json.dump(summary, fh, ensure_ascii=False, indent=2)
            print("JSON 摘要已生成：{}".format(os.path.abspath(args.json_out)))
    except Exception as exc:
        print("解析失败：{}".format(exc), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
