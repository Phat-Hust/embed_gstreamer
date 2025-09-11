#!/usr/bin/env python3
import cv2
import subprocess
import os
import sys

def extract_frames_with_klv_mapping(ts_file):
    # Extract video frames starting from 0
    os.makedirs("frames", exist_ok=True)
    cmd = [
        'ffmpeg', '-i', ts_file,
        '-vf', 'fps=30',
        '-start_number', '0',
        'frames/frame_%06d.png',
        '-y'
    ]
    print("Extracting frames...")
    subprocess.run(cmd)
    
    # Extract KLV data
    cmd = [
        'ffmpeg', '-i', ts_file,
        '-map', '0:1', '-c', 'copy', '-f', 'data',
        'extracted_klv.bin',
        '-y'
    ]
    print("Extracting KLV data...")
    subprocess.run(cmd)
    
    # Parse KLV manually since klvdata might have issues
    sys.path.append('/media/phat-hust/research/VHT/Gstreamer/gstreamer/subprojects/gst-docs/examples/tutorials/klvdata')
    
    frame_klv_map = {}
    
    try:
        with open('extracted_klv.bin', 'rb') as f:
            data = f.read()
            
        print(f"📊 KLV file size: {len(data)} bytes")
        
        # Parse KLV packets manually
        uas_key = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00'
        offset = 0
        packet_count = 0
        
        while offset < len(data):
            try:
                if data[offset:offset+16] == uas_key:
                    print(f"📦 Processing packet {packet_count} at offset {offset}")
                    
                    # Parse length
                    length_byte = data[offset + 16]
                    if length_byte & 0x80:
                        # Long form
                        length_bytes = length_byte & 0x7F
                        if offset + 17 + length_bytes <= len(data):
                            length = int.from_bytes(data[offset+17:offset+17+length_bytes], 'big')
                            data_start = offset + 17 + length_bytes
                        else:
                            offset += 1
                            continue
                    else:
                        # Short form
                        length = length_byte
                        data_start = offset + 17
                    
                    if data_start + length <= len(data):
                        # Extract KLV data
                        klv_data = data[data_start:data_start+length]
                        
                        # Try to parse with klvdata
                        metadata = {}
                        frame_id = None
                        
                        try:
                            import klvdata
                            from klvdata.misb0601 import UASLocalMetadataSet
                            
                            uas_set = UASLocalMetadataSet(klv_data)
                            metadata_list = uas_set.MetadataList()
                            
                            for key, (lds, esd, uds, value) in metadata_list.items():
                                metadata[lds] = value
                                if 'mission' in lds.lower() or 'frame' in str(value):
                                    frame_id = value
                                elif 'Precision Time Stamp' in lds:
                                    frame_id = f"frame_{packet_count:06d}_{value}"
                                elif key == 2:  # MISB 0601 key 2 is often Mission ID
                                    frame_id = value
                            
                            if frame_id is None:
                                frame_id = f"frame_{packet_count:06d}"
                                    
                            print(f"  ✅ Parsed {len(metadata)} metadata items")
                            
                        except Exception as e:
                            print(f"  ⚠️ Parse error: {e}")
                            # Create basic metadata from raw data
                            metadata = {
                                'raw_data_length': length,
                                'packet_offset': offset,
                                'raw_data_hex': klv_data[:20].hex() + '...' if len(klv_data) > 20 else klv_data.hex()
                            }
                        
                        frame_klv_map[packet_count] = {
                            'frame_id': frame_id,
                            'metadata': metadata,
                            'frame_file': f"frames/frame_{packet_count:06d}.png"
                        }
                        
                        offset = data_start + length
                        packet_count += 1
                    else:
                        offset += 1
                else:
                    offset += 1
                    
            except Exception as e:
                print(f"  ❌ Error at offset {offset}: {e}")
                offset += 1
                
        print(f"📊 Total packets processed: {packet_count}")
        
        # Save mapping
        import json
        with open('frame_klv_mapping.json', 'w') as f:
            json.dump(frame_klv_map, f, indent=2, default=str)
            
        print(f"✅ Created mapping for {packet_count} packets")
        print("📄 Mapping saved to frame_klv_mapping.json")
        
    except Exception as e:
        print(f"❌ Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 extract_frames_with_klv.py <file.ts>")
        sys.exit(1)
    
    extract_frames_with_klv_mapping(sys.argv[1])