#!/usr/bin/env python3
"""
tools/udp_listener.py - desk-test receiver for the 788B CSI packet.

Stand in for the RPi `sensing` service while bench-testing: bind :5005,
unpack each datagram, validate the wire contract, and print per-node
rate / sequence-loss / RSSI so you can confirm a flashed node is alive.

Usage (on the laptop the firmware's TARGET_IP points at):
    python tools/udp_listener.py            # listen 0.0.0.0:5005
    python tools/udp_listener.py --port 5005 --verbose

Windows: allow inbound UDP on the port first, e.g. (admin PowerShell)
    New-NetFirewallRule -DisplayName "CSI 5005" -Direction Inbound `
        -Protocol UDP -LocalPort 5005 -Action Allow
"""

import argparse
import socket
import struct
import time

# Must match src/packet.h BinaryPacket / db_spec "<4sBBHIIhH192f>"
PACKET_FORMAT = "<4sBBHIIhH192f"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)  # 788
MAGIC = b"CSI!"


def main():
    ap = argparse.ArgumentParser(description="788B CSI UDP test listener")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=5005)
    ap.add_argument("--verbose", action="store_true",
                    help="print first few amplitudes per packet")
    ap.add_argument("--report", type=float, default=2.0,
                    help="seconds between per-node summary lines")
    args = ap.parse_args()

    assert PACKET_SIZE == 788, f"format size {PACKET_SIZE} != 788"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.host, args.port))
    print(f"listening udp {args.host}:{args.port}  (expecting {PACKET_SIZE}B packets)")
    print("Ctrl-C to stop\n")

    # per-node stats: count, bad, last_seq, lost, last_rssi, window_start_count
    stats = {}
    last_report = time.monotonic()

    try:
        while True:
            data, addr = sock.recvfrom(2048)

            if len(data) != PACKET_SIZE:
                print(f"[WARN] {addr[0]} sent {len(data)}B (expected {PACKET_SIZE})")
                continue

            fields = struct.unpack(PACKET_FORMAT, data)
            magic, node_id, _rsv, n_samples, seq, ts_ms, rssi, _rsv2 = fields[:8]
            floats = fields[8:]  # 192 floats
            raw = floats[0:64]
            resp = floats[64:128]
            heart = floats[128:192]

            if magic != MAGIC:
                print(f"[WARN] {addr[0]} bad magic {magic!r} - discarding")
                continue

            s = stats.get(node_id)
            if s is None:
                s = {"rx": 0, "lost": 0, "last_seq": None, "rssi": rssi,
                     "ip": addr[0], "win_rx": 0}
                stats[node_id] = s
                print(f"[NEW] node {node_id} from {addr[0]}  "
                      f"n_samples={n_samples}")

            s["rx"] += 1
            s["win_rx"] += 1
            s["rssi"] = rssi
            s["ip"] = addr[0]
            if s["last_seq"] is not None:
                gap = (seq - s["last_seq"] - 1) & 0xFFFFFFFF
                if 0 < gap < 1000:  # ignore wrap/restart
                    s["lost"] += gap
            s["last_seq"] = seq

            if args.verbose:
                print(f"node {node_id} seq={seq} ts={ts_ms} rssi={rssi} "
                      f"raw[:3]={[round(x, 3) for x in raw[:3]]} "
                      f"resp[0]={resp[0]:+.5f} heart[0]={heart[0]:+.5f}")

            now = time.monotonic()
            if now - last_report >= args.report:
                dt = now - last_report
                for nid, st in sorted(stats.items()):
                    hz = st["win_rx"] / dt if dt else 0.0
                    loss = 100.0 * st["lost"] / max(1, st["rx"] + st["lost"])
                    print(f"  node {nid} @ {st['ip']:<15} "
                          f"rate={hz:5.1f}Hz  rx={st['rx']:<7} "
                          f"lost={st['lost']}({loss:.1f}%)  rssi={st['rssi']}dBm")
                    st["win_rx"] = 0
                print()
                last_report = now

    except KeyboardInterrupt:
        print("\n--- final ---")
        for nid, st in sorted(stats.items()):
            loss = 100.0 * st["lost"] / max(1, st["rx"] + st["lost"])
            print(f"node {nid}: rx={st['rx']} lost={st['lost']}({loss:.1f}%) "
                  f"last_rssi={st['rssi']}dBm")


if __name__ == "__main__":
    main()
