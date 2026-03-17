#!/usr/bin/env python3
import argparse
import socket
import struct
import sys
import time


def parse_mac(text: str) -> bytes:
    parts = text.split(":")
    if len(parts) != 6:
        raise ValueError("invalid mac")
    return bytes(int(part, 16) for part in parts)


def ipv4_checksum(data: bytes) -> int:
    if len(data) % 2 == 1:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) + data[i + 1]
    while total > 0xFFFF:
        total = (total & 0xFFFF) + (total >> 16)
    return (~total) & 0xFFFF


def udp_checksum(src_ip: bytes, dst_ip: bytes, udp_header: bytes, payload: bytes) -> int:
    pseudo = src_ip + dst_ip + struct.pack("!BBH", 0, socket.IPPROTO_UDP, len(udp_header) + len(payload))
    data = pseudo + udp_header + payload
    if len(data) % 2 == 1:
        data += b"\x00"
    total = 0
    for i in range(0, len(data), 2):
        total += (data[i] << 8) + data[i + 1]
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
                    payload_size: int,
                    sequence: int) -> bytes:
    src_ip = socket.inet_aton(src_ip_text)
    dst_ip = socket.inet_aton(dst_ip_text)
    payload = struct.pack("!Q", sequence) + bytes((i % 256 for i in range(max(0, payload_size - 8))))

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


def main() -> int:
    parser = argparse.ArgumentParser(description="Controlled Ethernet/UDP sender for AF_XDP validation.")
    parser.add_argument("--iface", required=True)
    parser.add_argument("--dst-mac", required=True)
    parser.add_argument("--src-mac", required=True)
    parser.add_argument("--src-ip", default="192.168.1.101")
    parser.add_argument("--dst-ip", default="192.168.1.102")
    parser.add_argument("--src-port", type=int, default=40000)
    parser.add_argument("--dst-port", type=int, default=9999)
    parser.add_argument("--duration", type=int, default=10)
    parser.add_argument("--pps", type=int, default=1000)
    parser.add_argument("--payload-size", type=int, default=512)
    args = parser.parse_args()

    dst_mac = parse_mac(args.dst_mac)
    src_mac = parse_mac(args.src_mac)

    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    sock.bind((args.iface, 0))

    deadline = time.time() + args.duration
    sent_packets = 0
    sent_bytes = 0
    interval = 1.0 / max(1, args.pps)
    sequence = 0

    while time.time() < deadline:
        frame = build_udp_frame(
            dst_mac=dst_mac,
            src_mac=src_mac,
            src_ip_text=args.src_ip,
            dst_ip_text=args.dst_ip,
            src_port=args.src_port,
            dst_port=args.dst_port,
            payload_size=max(32, args.payload_size),
            sequence=sequence,
        )
        sock.send(frame)
        sent_packets += 1
        sent_bytes += len(frame)
        sequence += 1
        time.sleep(interval)

    print(
        f"RAW_ETH_SENDER success iface={args.iface} duration={args.duration} "
        f"pps_target={args.pps} packets={sent_packets} bytes={sent_bytes} "
        f"src_ip={args.src_ip} dst_ip={args.dst_ip} dst_port={args.dst_port}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
