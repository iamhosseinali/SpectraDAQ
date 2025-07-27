#!/usr/bin/env python3
"""
Simple UDP sender to test the Qt application
Sends test data with the structure: struct { uint64_t data; }
"""

import socket
import struct
import time
import sys

def send_test_data(host='127.0.0.1', port=2023, count=100):
    """Send test UDP packets to the Qt application"""
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    print(f"Sending {count} test packets to {host}:{port}")
    print("Each packet contains: struct { uint64_t data; }")
    print("Press Ctrl+C to stop")
    
    try:
        for i in range(count):
            # Create test data: uint64_t data = i
            data = struct.pack('<Q', i)  # Little-endian uint64
            
            # Send the packet
            sock.sendto(data, (host, port))
            
            print(f"Sent packet {i+1}/{count}: data={i}")
            time.sleep(0.1)  # 100ms between packets
            
    except KeyboardInterrupt:
        print("\nStopped by user")
    finally:
        sock.close()
        print("UDP sender closed")

if __name__ == "__main__":
    host = '127.0.0.1'  # localhost
    port = 2023         # default Qt app port
    
    if len(sys.argv) > 1:
        host = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])
    
    send_test_data(host, port, 100) 