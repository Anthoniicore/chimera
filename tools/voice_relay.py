#!/usr/bin/env python3
"""Minimal UDP relay for Chimera Voice Chat VCH1 (v2, room-aware) packets.

Meant to run as a single always-on relay shared by many unrelated Halo
servers. Clients are grouped by the room_id embedded in each packet (a hash
of the Halo server's connect address, computed client-side) and audio is
only ever forwarded to other clients in the same room.
"""
import argparse
import socket
import struct
import time

MAGIC = 0x56434831
VERSION = 2
HEADER = struct.Struct('<IBBHIIII')  # magic, version, flags, payload_size, room_id, sender_id, sequence, timestamp
TIMEOUT = 60.0


def parse_header(data: bytes):
    if len(data) < HEADER.size:
        return None
    magic, version, _flags, payload_size, room_id, sender_id, _sequence, _timestamp = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        return None
    if payload_size == 0 or payload_size > 1275:
        return None
    if len(data) != HEADER.size + payload_size:
        return None
    return room_id, sender_id


def main() -> None:
    parser = argparse.ArgumentParser(description='Chimera Voice Chat UDP relay (room-aware)')
    parser.add_argument('--host', default='0.0.0.0')
    parser.add_argument('--port', type=int, default=30777)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    # room_id -> { address: (sender_id, last_seen) }
    rooms: dict[int, dict[tuple, tuple]] = {}
    print(f'Chimera voice relay listening on {args.host}:{args.port}')
    print('Waiting for packets... (nothing will print here until a client sends valid audio)')

    while True:
        data, address = sock.recvfrom(2048)
        parsed = parse_header(data)
        if parsed is None:
            print(f'[dropped] {len(data)} bytes from {address} did not look like a valid v{VERSION} VCH1 packet')
            continue
        room_id, sender_id = parsed

        room = rooms.setdefault(room_id, {})
        now = time.monotonic()
        is_new = address not in room
        room[address] = (sender_id, now)
        if is_new:
            print(f'[new client] {address} joined room 0x{room_id:08x} (sender {sender_id}); '
                  f'{len(room)} client(s) now in that room')

        forwarded_to = 0
        for client, (_client_sender, seen) in list(room.items()):
            if now - seen > TIMEOUT:
                del room[client]
                print(f'[timeout] {client} removed from room 0x{room_id:08x} (inactive for {TIMEOUT:.0f}s)')
                continue
            if client != address:
                try:
                    sock.sendto(data, client)
                    forwarded_to += 1
                except OSError as exc:
                    print(f'[error] failed to forward to {client}: {exc}')

        if not room:
            del rooms[room_id]

        if forwarded_to == 0:
            print(f'[no peers] got a packet from {address} in room 0x{room_id:08x} '
                  f'but no other client is registered in that room yet')


if __name__ == '__main__':
    main()
