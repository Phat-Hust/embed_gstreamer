import cv2
import socket
import pickle
import numpy as np
import sys
import os
import time

# Add klvdata to path
sys.path.append('/media/phat-hust/research/VHT/Gstreamer/gstreamer/subprojects/gst-docs/examples/tutorials/gstreamer/subprojects/gst-docs/examples/tutorials/klvdata')
import klvdata
from klvdata.misb0601 import UASLocalMetadataSet

UDP_IP = "0.0.0.0"   # listen on all interfaces
UDP_PORT = 5000

VIDEO_FILE = "raw_video.bin"
KLV_FILE = "klv_data.bin"

print(f"Writing to separate files:")
print(f"Video: {VIDEO_FILE}")
print(f"KLV: {KLV_FILE}")

# Open files for writing
video_fd = open(VIDEO_FILE, 'wb')
klv_fd = open(KLV_FILE, 'wb')
print("Files opened. Starting receiver...")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Receiver started on {UDP_IP}:{UDP_PORT}")

frames = []
metadata_list = []
frame_to_klv_map = {}
frame_counter = 0

try:
    while True:
        packet, _ = sock.recvfrom(65536)  # max UDP packet size
        try:
            metadata, encoded_bytes, frame_id = pickle.loads(packet)
            # Map frame to KLV Metadata
            frame_to_klv_map[frame_counter] = {
                'frame_id' : frame_id,
                'klv_metadata' : metadata,
                'timestamp' : time.time()
            }
            print(f"Frame {frame_counter} (original: {frame_id}) | KLV packet mapped")
            frame_counter += 1
            frame = cv2.imdecode(
                np.frombuffer(encoded_bytes, dtype=np.uint8),
                cv2.IMREAD_COLOR
            )
        except Exception as e:
            print("⚠️ Deserialization error:", e)
            continue

        if frame is None:
            print("⚠️ Failed to decode frame")
            continue

        # Resize frame to expected dimensions (640x480)
        frame = cv2.resize(frame, (640, 480))

        # Write raw video frame to file
        video_fd.write(frame.tobytes())

        # Handle different metadata formats
        if isinstance(metadata, list):
            # KLV metadata list - convert to dictionary
            metadata_dict = {
                "type": "klv",
                "packet_size": len(packet)
            }
            
            # Extract values from KLV objects and create KLV packet
            klv_items = []
            for klv_item in metadata:
                try:
                    if hasattr(klv_item, 'value'):
                        if 'PrecisionTimeStamp' in str(type(klv_item)):
                            metadata_dict["timestamp"] = klv_item.value
                        elif 'SensorLatitude' in str(type(klv_item)):
                            metadata_dict["latitude"] = klv_item.value
                        elif 'SensorLongitude' in str(type(klv_item)):
                            metadata_dict["longitude"] = klv_item.value
                        elif 'PlatformGroundSpeed' in str(type(klv_item)):
                            metadata_dict["speed"] = klv_item.value
                        klv_items.append(klv_item)
                except Exception as e:
                    print(f"⚠️ Error extracting KLV value: {e}")
            
            # Create UAS metadata set and encode
            try:
                if klv_items:
                    klv_data = b''
                    for item in klv_items:
                        klv_data += bytes(item)
                    
                    # UAS Local Set Universal Key
                    uas_key = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00'
                    # Length (BER encoding)
                    length = len(klv_data)
                    if length < 128:
                        length_field = bytes([length])
                    else:
                        length_bytes = length.to_bytes((length.bit_length() + 7) // 8, 'big')
                        length_field = bytes([0x80 | len(length_bytes)]) + length_bytes
                    
                    encoded_klv = uas_key + length_field + klv_data
                    klv_fd.write(encoded_klv)
                else:
                    # No valid KLV iteams, write minimal KLV
                    minimal_klv = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00\x04\x00\x00\x00\x01'
                    klv_fd.write(minimal_klv)
            except Exception as e:
                print(f"⚠️ KLV encoding error: {e}")
                # Write minimal KLV data as fallback
                minimal_klv = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00\x04\x00\x00\x00\x01'
                klv_fd.write(minimal_klv)
            
            print(f"📥 KLV Frame | Lat: {metadata_dict.get('latitude', 'N/A')} | Lon: {metadata_dict.get('longitude', 'N/A')} | Speed: {metadata_dict.get('speed', 'N/A')} | Size: {len(packet)} bytes")
            
        elif isinstance(metadata, dict):
            # Regular dictionary metadata - create minimal KLV
            metadata_dict = metadata.copy()
            metadata_dict["packet_size"] = len(packet)
            
            # Write minimal KLV packet with timestamp
            try:
                timestamp_us = int(metadata_dict.get('timestamp', time.time()) * 1000000)
                # Basic KLV packet structure: UL Key + Length + Timestamp
                klv_packet = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00'  # UL Key
                klv_packet += b'\x08'  # Length (8 bytes for timestamp)
                klv_packet += timestamp_us.to_bytes(8, 'big')  # Timestamp
                klv_fd.write(klv_packet)
            except Exception as e:
                print(f"⚠️ KLV encoding error: {e}")
                # Write minimal fallback KLV
                minimal_klv = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00\x04\x00\x00\x00\x01'
                klv_fd.write(minimal_klv)
            
            print(f"📥 Received frame {metadata_dict['id']} | ts={metadata_dict['timestamp']:.6f} | size={metadata_dict['packet_size']} bytes")
        else:
            # Unknown format - write minimal KLV
            metadata_dict = {"type": "unknown", "packet_size": len(packet)}
            # Write minimal fallback KLV
            minimal_klv = b'\x06\x0e\x2b\x34\x02\x0b\x01\x01\x0e\x01\x03\x01\x01\x00\x00\x00\x04\x00\x00\x00\x01'
            klv_fd.write(minimal_klv)
            print(f"⚠️ Unknown metadata format: {type(metadata)}")

        frames.append(frame)
        metadata_list.append(metadata_dict)

        cv2.imshow("Receiver", frame)
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

except KeyboardInterrupt:
    print("\nShutting down...")
finally:
    # Cleanup
    try:
        video_fd.close()
        klv_fd.close()
    except:
        pass
    
    sock.close()
    cv2.destroyAllWindows()
    
    # Save backup files
    if frames:
        h, w, _ = frames[0].shape
        out = cv2.VideoWriter("backup_output.mp4", cv2.VideoWriter_fourcc(*'mp4v'), 30, (w, h))
        for f in frames:
            out.write(f)
        out.release()
        print("✅ Saved backup video as backup_output.mp4")

    with open("output_log.txt", "w") as f:
        f.write("type,timestamp,latitude,longitude,speed,packet_size\n")
        for md in metadata_list:
            f.write(f"{md.get('type', 'fallback')},{md.get('timestamp', 0)},{md.get('latitude', 0)},{md.get('longitude', 0)},{md.get('speed', 0)},{md['packet_size']}\n")

    print("✅ Saved metadata log as output_log.txt")
    print("✅ Raw video saved to raw_video.bin")
    print("✅ KLV data saved to klv_data.bin")
    print("\nTo mux them together, run:")
    print(f"ffmpeg -f rawvideo -pix_fmt bgr24 -s 640x480 -r 30 -i {VIDEO_FILE} \\")
    print(f"       -f data -i {KLV_FILE} \\")
    print(f"       -c:v libx264 -preset ultrafast -crf 23 \\")
    print(f"       -c:d copy -map 0:v -map 1:d \\")
    print(f"       -f mov final_output.mov")