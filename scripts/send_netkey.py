#!/usr/bin/env python3
"""Send a UART_CMD_SET_NETKEY frame to the bridge over serial.

Usage: python3 send_netkey.py /dev/ttyUSB0 --netkey <32-hex-bytes> --token <16-hex-bytes> --netid 0x1234 --version 1

This constructs the frame with two start bytes 0x4C,0x4D and computes XOR checksum as:
 checksum = packetType ^ payloadLength ^ seq ^ payload_bytes...

It then writes the full frame and waits for an ACK response.
"""
import sys
import time
import argparse
import serial
import struct

START1 = 0x4C
START2 = 0x4D
END = 0x55
PACKET_TYPE_COMMAND = 0x04
CMD_SET_NETKEY = 0x14

def hex_to_bytes(s, expected_len=None):
    s = s.replace('0x','').replace(' ','').replace(':','')
    b = bytes.fromhex(s)
    if expected_len and len(b) != expected_len:
        raise ValueError(f"Expected {expected_len} bytes but got {len(b)}")
    return b

parser = argparse.ArgumentParser()
parser.add_argument('port')
parser.add_argument('--baud', type=int, default=115200)
parser.add_argument('--netkey', required=True, help='Network key as 32 hex chars (16 bytes)')
parser.add_argument('--token', required=True, help='Auth token as 16 hex chars (8 bytes)')
parser.add_argument('--netid', required=True, help='Network ID, e.g. 0x1234 or 4660')
parser.add_argument('--version', type=int, default=1, help='Key version (uint8)')
parser.add_argument('--seq', type=int, default=1, help='Sequence number to use in header')
parser.add_argument('--timeout', type=float, default=2.0, help='Serial read timeout seconds')
args = parser.parse_args()

try:
    netkey = hex_to_bytes(args.netkey, expected_len=16)
    token = hex_to_bytes(args.token, expected_len=8)
except Exception as e:
    print('Error parsing hex inputs:', e)
    sys.exit(1)

# parse netid
if isinstance(args.netid, str) and args.netid.startswith('0x'):
    netid = int(args.netid, 16)
else:
    netid = int(args.netid)

version = args.version & 0xFF
seq = args.seq & 0xFF

# timestamp now
timestamp = int(time.time()) & 0xFFFFFFFF

# Build UartNetworkKey packed layout: networkKey[16], authToken[8], networkId(uint16 little), keyVersion(uint8), timestamp(uint32 little)
payload_body = bytearray()
payload_body += netkey
payload_body += token
payload_body += struct.pack('<H', netid)
payload_body += struct.pack('<B', version)
payload_body += struct.pack('<I', timestamp)

# Full payload: first byte is command id, then packed UartNetworkKey
full_payload = bytearray()
full_payload.append(CMD_SET_NETKEY)
full_payload += payload_body

payload_len = len(full_payload)
if payload_len > 200:
    print('Payload too large:', payload_len)
    sys.exit(1)

# Build header
header = bytearray([START1, START2, PACKET_TYPE_COMMAND, payload_len, seq])

# checksum = packetType ^ payloadLength ^ sequenceNumber ^ each payload byte
checksum = 0
checksum ^= PACKET_TYPE_COMMAND
checksum ^= payload_len
checksum ^= seq
for b in full_payload:
    checksum ^= b

frame = header + full_payload + bytes([checksum, END])

print('Sending frame:', frame.hex())

# Send over serial
ser = serial.Serial(args.port, args.baud, timeout=args.timeout)
ser.flushInput()
ser.flushOutput()
ser.write(frame)
ser.flush()
print('Frame written, waiting for response...')

# Read response (simple read of available bytes)
start_time = time.time()
resp = bytearray()
while time.time() - start_time < args.timeout:
    if ser.in_waiting:
        resp += ser.read(ser.in_waiting)
    else:
        time.sleep(0.05)

if resp:
    print('Received:', resp.hex())
else:
    print('No response (timeout)')

ser.close()
