"""
가상 ESP32-CAM (UDP 7070)
- 빈 JPEG 이미지를 조립해서 UDP 패킷으로 전송합니다.
"""
import socket
import time

UDP_IP = "127.0.0.1"
UDP_PORT = 7070

# 1x1 회색 JPEG
JPEG_DATA = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xdb\x00C\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\t\t\x08\n\x0c\x14\r\x0c\x0b\x0b\x0c\x19\x12\x13\x0f"
    b"\x14\x1d\x1a\x1f\x1e\x1d\x1a\x1c\x1c $.' \",#\x1c\x1c(7),01444\x1f'9=82<.342\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00?\x00\xb8\xe0\x7f\xff\xd9"
)

def send_fake_camera():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    print(f"📷 [가상 카메라] {UDP_IP}:{UDP_PORT} 로 패킷 전송 시작...")
    
    frame_no = 0
    vehicle_id = 1
    
    while True:
        # 단일 청크로 전송 (패킷 구조: V_ID, F_NO, P_NO, CHKSUM, DATA...)
        checksum = sum(JPEG_DATA) % 256
        header = bytes([vehicle_id, frame_no % 256, 0, checksum])
        packet = header + JPEG_DATA
        
        sock.sendto(packet, (UDP_IP, UDP_PORT))
        print(f"📤 [프레임 전송] Frame #{frame_no} (크기: {len(JPEG_DATA)} bytes)")
        
        frame_no += 1
        time.sleep(1.0) # 1 FPS

if __name__ == "__main__":
    send_fake_camera()
