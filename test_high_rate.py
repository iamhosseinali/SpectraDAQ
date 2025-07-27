#!/usr/bin/env python3
"""
High-rate UDP data sender for testing SpectraDAQ performance
"""
import socket
import struct
import time
import sys

def send_high_rate_data(host='127.0.0.1', port=2023, rate_mbps=1, duration_sec=10):
    """
    Send high-rate UDP data for testing
    
    Args:
        host: Target host IP
        port: Target port
        rate_mbps: Data rate in Mbps
        duration_sec: Test duration in seconds
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Calculate packet rate for uint64_t data (8 bytes)
    bytes_per_packet = 8
    packets_per_second = (rate_mbps * 1000000) // (bytes_per_packet * 8)
    delay_between_packets = 1.0 / packets_per_second
    
    print(f"Testing {rate_mbps} Mbps data rate")
    print(f"Packets per second: {packets_per_second:,}")
    print(f"Delay between packets: {delay_between_packets*1000:.3f} ms")
    print(f"Duration: {duration_sec} seconds")
    print("-" * 50)
    
    start_time = time.time()
    packet_count = 0
    
    try:
        while time.time() - start_time < duration_sec:
            # Create uint64_t data packet
            data = struct.pack('<Q', packet_count)  # Little-endian uint64
            sock.sendto(data, (host, port))
            
            packet_count += 1
            
            # Sleep to maintain rate
            time.sleep(delay_between_packets)
            
            # Progress indicator
            if packet_count % 1000 == 0:
                elapsed = time.time() - start_time
                actual_rate = (packet_count * bytes_per_packet * 8) / (elapsed * 1000000)
                print(f"Sent {packet_count:,} packets, Rate: {actual_rate:.2f} Mbps")
                
    except KeyboardInterrupt:
        print("\nStopped by user")
    
    elapsed = time.time() - start_time
    actual_rate = (packet_count * bytes_per_packet * 8) / (elapsed * 1000000)
    
    print("-" * 50)
    print(f"Test completed!")
    print(f"Total packets sent: {packet_count:,}")
    print(f"Total time: {elapsed:.2f} seconds")
    print(f"Average rate: {actual_rate:.2f} Mbps")
    print(f"Packets per second: {packet_count/elapsed:.0f}")
    
    sock.close()

if __name__ == "__main__":
    # Parse command line arguments
    rate = 1  # Default 1 Mbps
    duration = 10  # Default 10 seconds
    
    if len(sys.argv) > 1:
        rate = float(sys.argv[1])
    if len(sys.argv) > 2:
        duration = int(sys.argv[2])
    
    print("SpectraDAQ High-Rate Test Sender")
    print("=" * 40)
    
    send_high_rate_data(rate_mbps=rate, duration_sec=duration) 