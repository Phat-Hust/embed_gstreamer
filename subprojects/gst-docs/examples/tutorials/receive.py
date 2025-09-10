import cv2
import socket
import pickle
import numpy as np

UDP_IP = "0.0.0.0"   # listen on all interfaces
UDP_PORT = 5000

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))

print(f"Receiver started on {UDP_IP}:{UDP_PORT}")

frames = []
metadata_list = []

while True:
    packet, _ = sock.recvfrom(65536)  # max UDP packet size
    try:
        metadata, encoded_bytes = pickle.loads(packet)
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

    # Add packet size info
    metadata["packet_size"] = len(packet)

    frames.append(frame)
    metadata_list.append(metadata)

    print(f"📥 Received frame {metadata['id']} | ts={metadata['timestamp']:.6f} | size={metadata['packet_size']} bytes")

    cv2.imshow("Receiver", frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# Save received video
if frames:
    h, w, _ = frames[0].shape
    out = cv2.VideoWriter("output.mpg", cv2.VideoWriter_fourcc(*'MPG1'), 30, (w, h))
    for f in frames:
        out.write(f)
    out.release()
    print("✅ Saved video as output.mpg")

# Save metadata log
with open("output_log.txt", "w") as f:
    f.write("frame_id,timestamp,packet_size\n")
    for md in metadata_list:
        f.write(f"{md['id']},{md['timestamp']:.6f},{md['packet_size']}\n")

print("✅ Saved metadata log as output_log.txt")

sock.close()
cv2.destroyAllWindows()
