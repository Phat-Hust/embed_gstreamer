import cv2
import socket
import pickle
import time
import sys
import os
import rclpy
from rclpy.node import Node
from nav_msgs.msg import Odometry

# Add klvdata to path
sys.path.append('/media/phat-hust/research/VHT/Gstreamer/gstreamer/subprojects/gst-docs/examples/tutorials/gstreamer/subprojects/gst-docs/examples/tutorials/klvdata')
import klvdata
from klvdata.misb0601 import UASLocalMetadataSet, SensorLatitude, SensorLongitude, PlatformGroundSpeed, PrecisionTimeStamp
from klvdata.common import datetime_to_bytes
from datetime import datetime, timezone
import math
import struct

UDP_IP = "127.0.0.1"
UDP_PORT = 5000

class KLVSender(Node):
    def __init__(self):
        super().__init__('klv_sender')
        self.latest_odom = None
        self.subscription = self.create_subscription(
            Odometry,
            '/odom',
            self.odom_callback,
            10)
        self.get_logger().info('KLV Sender node initialized')
    
    def odom_callback(self, msg):
        self.latest_odom = msg

def main():
    # Initialize ROS2
    rclpy.init()
    sender_node = KLVSender()
    
    # Initialize UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    # Initialize camera
    cap = cv2.VideoCapture(0)  # /dev/video0
    frame_id = 0
    
    print("Starting KLV video sender...")
    
    try:
        while True:
            # Spin ROS2 node to process callbacks
            rclpy.spin_once(sender_node, timeout_sec=0.001)
            
            ret, frame = cap.read()
            if not ret:
                break

            # Encode frame as JPEG
            ret, encoded = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 40])
            if not ret:
                continue
            encoded_bytes = encoded.tobytes()

            real_time = time.time()
            
            # Create KLV metadata from odometry data
            if sender_node.latest_odom:
                misb_data = []
                x = sender_node.latest_odom.pose.pose.position.x
                y = sender_node.latest_odom.pose.pose.position.y
                z = sender_node.latest_odom.pose.pose.position.z

                vx = sender_node.latest_odom.twist.twist.linear.x
                vy = sender_node.latest_odom.twist.twist.linear.y
                wz = sender_node.latest_odom.twist.twist.linear.z
                timestamp = datetime.fromtimestamp(
                    sender_node.latest_odom.header.stamp.sec + sender_node.latest_odom.header.stamp.nanosec * 1e-9,
                    tz=timezone.utc
                )

                timestamp_bytes = datetime_to_bytes(timestamp)
                speed = math.sqrt(vx**2 + vy**2 + wz**2)
                misb_data.append(PrecisionTimeStamp(timestamp_bytes))
                misb_data.append(SensorLatitude(x))
                misb_data.append(SensorLongitude(y))
                misb_data.append(PlatformGroundSpeed(speed))                
                try:
                    data = pickle.dumps((misb_data, encoded_bytes))
                    print(f"Metadata bytes: {len(misb_data)}, Frame:  {misb_data}")
                    print(f"Frame {frame_id} | KLV encoded | Pos: ({sender_node.latest_odom.pose.pose.position.x:.2f}, {sender_node.latest_odom.pose.pose.position.y:.2f}, {sender_node.latest_odom.pose.pose.position.z:.2f})")
                except Exception as e:
                    print(f"KLV encoding error: {e}")
                    # Fallback to simple metadata
                    metadata = {
                        "id": frame_id,
                        "timestamp": real_time,
                        "meta_str": f"frame_metadata_{frame_id}"
                    }
                    data = pickle.dumps((metadata, encoded_bytes))
            else:
                # Fallback metadata when no odometry available
                metadata = {
                    "id": frame_id,
                    "timestamp": real_time,
                    "meta_str": f"frame_metadata_{frame_id}"
                }
                data = pickle.dumps((metadata, encoded_bytes))
                print(f"Frame {frame_id} | No odometry data | Using fallback metadata")

            # UDP limit check (65kB)
            if len(data) > 65000:
                print(f"⚠️ Frame {frame_id} too big ({len(data)} bytes), skipping")
                frame_id += 1
                continue

            # Send
            sock.sendto(data, (UDP_IP, UDP_PORT))

            # Log
            print(f"Frame {frame_id} | Real time: {real_time:.6f} sec | Size: {len(data)} bytes")

            frame_id += 1
            time.sleep(1/30)  # ~30 fps
            
    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        # Cleanup
        cap.release()
        sock.close()
        sender_node.destroy_node()
        rclpy.shutdown()
        print("Cleanup complete")

if __name__ == '__main__':
    main()