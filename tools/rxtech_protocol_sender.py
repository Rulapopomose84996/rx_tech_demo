#!/usr/bin/env python3
import argparse
import socket
import struct
import sys
import time


MAGIC_CONTROL = 0x55AAFF00
MAGIC_DATA = 0x55AAFF03
MAGIC_TAIL = 0x55AAFF30
HEADER_SIZE = 16
IQ_BYTES = 4
DEFAULT_IQ_PER_FULL_PACKET = 508
DEFAULT_IQ_PER_LAST_PACKET = 476


def parse_mac(text: str) -> bytes:
    parts = text.split(":")
    if len(parts) != 6:
        raise ValueError("invalid mac")
    return bytes(int(part, 16) for part in parts)


def ipv4_checksum(data: bytes) -> int:
    if len(data) % 2 == 1:
        data += b"\x00"
    total = 0
    for index in range(0, len(data), 2):
        total += (data[index] << 8) + data[index + 1]
    while total > 0xFFFF:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def udp_checksum(src_ip: bytes, dst_ip: bytes, udp_header: bytes, payload: bytes) -> int:
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, socket.IPPROTO_UDP, len(udp_header) + len(payload))
    data = pseudo + udp_header + payload
    if len(data) % 2 == 1:
        data += b"\x00"
    total = 0
    for index in range(0, len(data), 2):
        total += (data[index] << 8) + data[index + 1]
    while total > 0xFFFF:
        total = (total & 0xFFFF) + (total >> 16)
    checksum = (~total) & 0xFFFF
    return checksum if checksum != 0 else 0xFFFF


def build_udp_frame(dst_mac: bytes,
                    src_mac: bytes,
                    src_ip_text: str,
                    dst_ip_text: str,
                    src_port: int,
                    dst_port: int,
                    payload: bytes,
                    sequence: int) -> bytes:
    src_ip = socket.inet_aton(src_ip_text)
    dst_ip = socket.inet_aton(dst_ip_text)

    udp_length = 8 + len(payload)
    udp_header = struct.pack("!HHHH", src_port, dst_port, udp_length, 0)
    udp_csum = udp_checksum(src_ip, dst_ip, udp_header, payload)
    udp_header = struct.pack("!HHHH", src_port, dst_port, udp_length, udp_csum)

    total_length = 20 + udp_length
    ip_header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,
        0,
        total_length,
        sequence & 0xFFFF,
        0,
        64,
        socket.IPPROTO_UDP,
        0,
        src_ip,
        dst_ip,
    )
    ip_csum = ipv4_checksum(ip_header)
    ip_header = struct.pack(
        "!BBHHHBBH4s4s",
        0x45,
        0,
        total_length,
        sequence & 0xFFFF,
        0,
        64,
        socket.IPPROTO_UDP,
        ip_csum,
        src_ip,
        dst_ip,
    )

    ethertype = b"\x08\x00"
    return dst_mac + src_mac + ethertype + ip_header + udp_header + payload


def make_control_table_payload(cpi: int, udp_packet_size: int) -> bytes:
    payload = bytearray(udp_packet_size)
    struct.pack_into("<I", payload, 0, MAGIC_CONTROL)
    struct.pack_into("<H", payload, 4, cpi)
    return bytes(payload)


def make_data_packet_payload(cpi: int,
                             channel: int,
                             prt: int,
                             packet_index: int,
                             packets_per_channel: int,
                             udp_packet_size: int,
                             iq_per_full_packet: int,
                             iq_per_last_packet: int) -> bytes:
    if udp_packet_size < HEADER_SIZE:
        raise ValueError("udp packet size must be at least 16 bytes")

    payload = bytearray(udp_packet_size)
    struct.pack_into("<I", payload, 0, MAGIC_DATA)
    struct.pack_into("<H", payload, 4, cpi)
    struct.pack_into("<H", payload, 6, channel)
    struct.pack_into("<H", payload, 8, prt)
    struct.pack_into("<H", payload, 10, packet_index)
    tail = MAGIC_TAIL if packet_index == packets_per_channel else 0
    struct.pack_into("<I", payload, 12, tail)

    data_pattern = ((cpi + channel + prt + packet_index) & 0xFF)
    for index in range(HEADER_SIZE, udp_packet_size):
        payload[index] = data_pattern

    if packet_index == packets_per_channel:
        used_bytes = iq_per_last_packet * IQ_BYTES
        if HEADER_SIZE + used_bytes > udp_packet_size:
            raise ValueError("last packet payload exceeds udp packet size")
        for index in range(HEADER_SIZE + used_bytes, udp_packet_size):
            payload[index] = 0
    else:
        used_bytes = iq_per_full_packet * IQ_BYTES
        if HEADER_SIZE + used_bytes > udp_packet_size:
            raise ValueError("full packet payload exceeds udp packet size")

    return bytes(payload)


def send_protocol_stream(args: argparse.Namespace) -> int:
    dst_mac = parse_mac(args.dst_mac)
    src_mac = parse_mac(args.src_mac)
    af_packet = getattr(socket, "AF_PACKET", None)
    if af_packet is None:
        raise OSError("AF_PACKET is not available on this platform")
    sock = socket.socket(af_packet, socket.SOCK_RAW)
    sock.bind((args.iface, 0))

    sent_packets = 0
    sent_bytes = 0
    sequence = 0
    inter_frame_gap_seconds = max(0, args.inter_frame_gap_us) / 1_000_000.0

    for cpi in range(args.cpi_start, args.cpi_start + args.cpi_count):
        if not args.no_control_table:
            control_payload = make_control_table_payload(cpi, args.udp_packet_size)
            frame = build_udp_frame(
                dst_mac=dst_mac,
                src_mac=src_mac,
                src_ip_text=args.src_ip,
                dst_ip_text=args.dst_ip,
                src_port=args.src_port,
                dst_port=args.dst_port,
                payload=control_payload,
                sequence=sequence,
            )
            sock.send(frame)
            sent_packets += 1
            sent_bytes += len(frame)
            sequence += 1
            if inter_frame_gap_seconds > 0:
                time.sleep(inter_frame_gap_seconds)

        for prt in range(1, args.prt_count + 1):
            for channel in range(args.channels_per_prt):
                for packet_index in range(1, args.packets_per_channel + 1):
                    payload = make_data_packet_payload(
                        cpi=cpi,
                        channel=channel,
                        prt=prt,
                        packet_index=packet_index,
                        packets_per_channel=args.packets_per_channel,
                        udp_packet_size=args.udp_packet_size,
                        iq_per_full_packet=args.iq_per_full_packet,
                        iq_per_last_packet=args.iq_per_last_packet,
                    )
                    frame = build_udp_frame(
                        dst_mac=dst_mac,
                        src_mac=src_mac,
                        src_ip_text=args.src_ip,
                        dst_ip_text=args.dst_ip,
                        src_port=args.src_port,
                        dst_port=args.dst_port,
                        payload=payload,
                        sequence=sequence,
                    )
                    sock.send(frame)
                    sent_packets += 1
                    sent_bytes += len(frame)
                    sequence += 1
                    if inter_frame_gap_seconds > 0:
                        time.sleep(inter_frame_gap_seconds)

    print(
        "RXTECH_PROTOCOL_SENDER success "
        f"iface={args.iface} cpi_count={args.cpi_count} prt_count={args.prt_count} "
        f"channels_per_prt={args.channels_per_prt} packets_per_channel={args.packets_per_channel} "
        f"packets={sent_packets} bytes={sent_bytes} src_ip={args.src_ip} dst_ip={args.dst_ip} "
        f"dst_port={args.dst_port} control_table={'off' if args.no_control_table else 'on'}"
    )
    return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Protocol-correct Ethernet/UDP sender for current rx_tech_demo Phase 3 parsing path.")
    parser.add_argument("--iface", required=True)
    parser.add_argument("--dst-mac", required=True)
    parser.add_argument("--src-mac", required=True)
    parser.add_argument("--src-ip", default="172.20.11.222")
    parser.add_argument("--dst-ip", default="172.20.11.100")
    parser.add_argument("--src-port", type=int, default=30001)
    parser.add_argument("--dst-port", type=int, default=9999)
    parser.add_argument("--cpi-start", type=int, default=1)
    parser.add_argument("--cpi-count", type=int, default=2)
    parser.add_argument("--prt-count", type=int, default=1)
    parser.add_argument("--channels-per-prt", type=int, default=3)
    parser.add_argument("--packets-per-channel", type=int, default=9)
    parser.add_argument("--udp-packet-size", type=int, default=2048)
    parser.add_argument("--iq-per-full-packet", type=int, default=DEFAULT_IQ_PER_FULL_PACKET)
    parser.add_argument("--iq-per-last-packet", type=int, default=DEFAULT_IQ_PER_LAST_PACKET)
    parser.add_argument("--inter-frame-gap-us", type=int, default=0)
    parser.add_argument("--no-control-table", action="store_true")
    return parser


def main() -> int:
    parser = build_arg_parser()
    args = parser.parse_args()

    if args.channels_per_prt <= 0:
        parser.error("channels-per-prt must be greater than 0")
    if args.packets_per_channel <= 0:
        parser.error("packets-per-channel must be greater than 0")
    if args.udp_packet_size <= HEADER_SIZE:
        parser.error("udp-packet-size must be greater than 16")
    if args.cpi_count <= 0 or args.prt_count <= 0:
        parser.error("cpi-count and prt-count must be greater than 0")

    try:
        return send_protocol_stream(args)
    except PermissionError:
        print("raw socket requires root privileges", file=sys.stderr)
        return 1
    except OSError as exc:
        print(f"send failed: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())