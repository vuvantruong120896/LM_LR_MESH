#!/usr/bin/env python3
"""
start_provision.py

Helper to send UART_CMD_START_PROVISIONING to the Bridge using the project's
UART framing: start bytes 0x4C,0x4D, header (packetType, payloadLength, seq),
payload (command + data), XOR checksum over header+payload, end byte 0x55.

Usage: python3 tools/start_provision.py --port /dev/ttyACM0 --baud 115200 --duration 600000

Prints the hex frame and sends it over serial.
"""
import argparse
import struct
import sys
import time

try:
    import serial
except Exception:
    serial = None


START1 = 0x4C
START2 = 0x4D
PACKET_TYPE_COMMAND = 0x04
END_BYTE = 0x55
UART_CMD_START_PROVISIONING = 0x15


def xor_checksum(data: bytes) -> int:
    chk = 0
    for b in data:
        chk ^= b
    return chk & 0xFF


def build_start_provision_packet(seq: int, duration_ms: int, max_sessions: int, auth_method: int):
    # Build UartProvisioningControl struct: action(1), durationMs(4), maxSessions(1), authMethod(1)
    action = 1  # start
    payload_control = struct.pack('<BIBB', action, duration_ms & 0xFFFFFFFF, max_sessions & 0xFF, auth_method & 0xFF)

    # Full payload = command byte + control struct
    payload = bytes([UART_CMD_START_PROVISIONING]) + payload_control

    payload_length = len(payload)

    header = bytes([PACKET_TYPE_COMMAND, payload_length, seq & 0xFF])

    checksum = xor_checksum(header + payload)

    frame = bytes([START1, START2]) + header + payload + bytes([checksum, END_BYTE])
    return frame


def hexdump(b: bytes) -> str:
    return ' '.join(f"{x:02X}" for x in b)


def main():
    parser = argparse.ArgumentParser(description='Send START_PROVISIONING UART command to Bridge')
    parser.add_argument('--port', '-p', required=False, default='/dev/ttyACM0', help='Serial port')
    parser.add_argument('--baud', '-b', required=False, type=int, default=115200, help='Baud rate')
    parser.add_argument('--duration', '-d', required=False, type=int, default=600000, help='Duration ms (0=indefinite)')
    parser.add_argument('--max', required=False, type=int, default=4, help='Max sessions')
    parser.add_argument('--auth', required=False, type=int, default=1, help='Auth method')
    parser.add_argument('--seq', required=False, type=int, default=1, help='Sequence number')
    parser.add_argument('--dry-run', action='store_true', help='Print frame but do not send')

    args = parser.parse_args()

    frame = build_start_provision_packet(args.seq, args.duration, args.max, args.auth)

    print('Frame (hex):', hexdump(frame))
    print('Payload length:', len(frame))

    if args.dry_run:
        return

    if serial is None:
        print('pyserial not installed. Install with: pip3 install pyserial', file=sys.stderr)
        return

    try:
        with serial.Serial(args.port, args.baud, timeout=1) as ser:
            # Flush
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            print(f'Sending to {args.port} @ {args.baud}...')
            ser.write(frame)
            ser.flush()
            time.sleep(0.1)
            # Try to read response (ACK/status)
            resp = ser.read(256)
            if resp:
                print('Received:', hexdump(resp))
            else:
                print('No response received (timeout)')
    except Exception as e:
        print('Serial error:', e, file=sys.stderr)


if __name__ == '__main__':
    main()
