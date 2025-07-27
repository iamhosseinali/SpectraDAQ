#!/usr/bin/env python3
"""
Complete workflow test for SpectraDAQ binary logging and conversion
"""
import socket
import struct
import time
import os

def test_complete_workflow():
    """Test the complete binary logging and conversion workflow"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    host = '127.0.0.1'
    port = 2023
    
    print("SpectraDAQ Complete Workflow Test")
    print("=" * 50)
    print("This test verifies:")
    print("1. Binary logging during capture")
    print("2. Automatic conversion to CSV after capture")
    print("3. Both files are created correctly")
    print()
    print("Steps:")
    print("1. Start SpectraDAQ")
    print("2. Set struct: uint64_t data;")
    print("3. Check 'Binary Logging' checkbox")
    print("4. Click 'Log to CSV' and set duration to 10 seconds")
    print("5. Run this script to send test data")
    print("6. Wait for conversion to complete")
    print("-" * 50)
    
    input("Press Enter when ready to start test...")
    
    # Send data for 8 seconds (leaving 2 seconds for conversion)
    start_time = time.time()
    packet_count = 0
    
    print("Sending test data...")
    try:
        while time.time() - start_time < 8:
            # Send packet with incrementing value
            data = struct.pack('<Q', packet_count)  # Little-endian uint64
            sock.sendto(data, (host, port))
            packet_count += 1
            
            # Progress indicator
            if packet_count % 1000 == 0:
                elapsed = time.time() - start_time
                rate = packet_count / elapsed
                print(f"Sent {packet_count:,} packets, Rate: {rate:.0f} packets/sec")
            
            time.sleep(0.001)  # 1ms delay for ~1000 packets/sec
            
    except KeyboardInterrupt:
        print("\nStopped by user")
    
    sock.close()
    
    elapsed = time.time() - start_time
    print(f"\nTest completed!")
    print(f"Total packets sent: {packet_count:,}")
    print(f"Total time: {elapsed:.2f} seconds")
    print(f"Average rate: {packet_count/elapsed:.0f} packets/sec")
    print()
    print("Now wait for:")
    print("1. Logging to finish (10 seconds total)")
    print("2. Binary to CSV conversion to complete")
    print("3. Check that both .bin and .csv files are created")
    print()
    print("Expected files:")
    print("- test.bin (binary data)")
    print("- test.csv (converted CSV data)")

if __name__ == "__main__":
    test_complete_workflow() 