#!/usr/bin/env python3
"""
Test script for AT TCP commands on RP6502 RIA.
Demonstrates CIPSTART, CIPSEND, CIPRECVDATA, CIPSTATUS, and CIPCLOSE.

Usage:
    python3 test_at_tcp.py /dev/ttyACM0

Requirements:
    pip install pyserial
"""

import serial
import sys
import time

def send_at(ser, cmd, wait=0.5):
    """Send AT command and read response."""
    print(f">>> {cmd}")
    ser.write(f"{cmd}\r".encode('ascii'))
    time.sleep(wait)
    response = b""
    while ser.in_waiting:
        response += ser.read(ser.in_waiting)
        time.sleep(0.05)
    decoded = response.decode('ascii', errors='replace')
    print(f"<<< {decoded}")
    return decoded

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <serial_port>")
        print("Example: python3 test_at_tcp.py /dev/ttyACM0")
        sys.exit(1)
    
    port = sys.argv[1]
    
    # Open serial connection
    print(f"Opening {port} at 115200 baud...")
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(0.5)
    
    # Flush any startup messages
    ser.reset_input_buffer()
    
    # Basic AT test
    send_at(ser, "AT")
    
    # Check status (should be ON_HOOK)
    send_at(ser, "AT+CIPSTATUS?")
    
    # Connect to echo server (tcpbin.com:4242 or any available echo service)
    # Note: Replace with your own test server if needed
    send_at(ser, 'AT+CIPSTART="tcpbin.com",4242', wait=3.0)
    
    # Check status (should be CONNECTED)
    send_at(ser, "AT+CIPSTATUS?")
    
    # Send data using quoted mode
    send_at(ser, 'AT+CIPSEND="Hello from RP6502\\n"')
    
    # Send data using length-prompt mode
    send_at(ser, "AT+CIPSEND=10", wait=0.2)
    ser.write(b"TEST12345\n")
    time.sleep(0.5)
    
    # Read response
    while ser.in_waiting:
        print(f"<<< {ser.read(ser.in_waiting).decode('ascii', errors='replace')}")
    
    # Wait for echo response (allow time for data to arrive)
    time.sleep(1.5)
    
    # Pull received data
    send_at(ser, "AT+CIPRECVDATA=50", wait=0.5)
    
    # Close connection
    send_at(ser, "AT+CIPCLOSE", wait=1.0)
    
    # Check status (should be ON_HOOK again)
    send_at(ser, "AT+CIPSTATUS?")
    
    ser.close()
    print("Test complete!")

if __name__ == "__main__":
    main()
