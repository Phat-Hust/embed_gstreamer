#!/usr/bin/env python3
import os
import sys

def debug_klv_file():
    if not os.path.exists('extracted_klv.bin'):
        print("❌ extracted_klv.bin not found")
        return
    
    with open('extracted_klv.bin', 'rb') as f:
        data = f.read()
    
    print(f"📊 KLV file size: {len(data)} bytes")
    print(f"📊 First 100 bytes (hex): {data[:100].hex()}")
    print(f"📊 First 100 bytes (ascii): {data[:100]}")
    
    # Look for UAS Local Set key
    uas_key = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00'
    
    count = 0
    offset = 0
    while True:
        pos = data.find(uas_key, offset)
        if pos == -1:
            break
        print(f"📍 Found UAS key at position {pos}")
        count += 1
        offset = pos + 1
    
    print(f"📊 Total UAS keys found: {count}")

if __name__ == "__main__":
    debug_klv_file()