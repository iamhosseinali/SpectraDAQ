#!/usr/bin/env python3
"""
Test script to verify binary logging functionality
"""
import socket
import struct
import time
import os

def test_binary_logging():
    """Test binary logging with simple data"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    host = '127.0.0.1'
    port = 2023
    
    print("Testing binary logging...")
    print("1. Start SpectraDAQ")
    print("2. Set struct: uint64_t data;")
    print("3. Check 'Binary Logging' checkbox")
    print("4. Click 'Log to CSV' and set duration to 5 seconds")
    print("5. Run this script to send test data")
    print("-" * 50)
    
    input("Press Enter when ready to send test data...")
    
    # Send 1000 packets with incrementing values
    for i in range(1000):
        data = struct.pack('<Q', i)  # Little-endian uint64
        sock.sendto(data, (host, port))
        time.sleep(0.001)  # 1ms delay
        
        if i % 100 == 0:
            print(f"Sent {i} packets...")
    
    sock.close()
    print("Test data sent!")
    print("Check if a .bin file was created in the same directory as the CSV file.")

if __name__ == "__main__":
    test_binary_logging() 