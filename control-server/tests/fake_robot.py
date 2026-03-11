"""
가상 AGV 로봇 (TCP 8000 바이너리 통신)
- 주기적으로 로봇 상태(위치, 배터리)를 SFAM 바이너리 패킷으로 전송합니다.
"""
import socket
import time
import random
import sys
import os

# 부모 디렉토리를 path에 추가하여 network 모듈을 임포트
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from network.sfam_protocol import build_packet, MSG_AGV_TELEMETRY, ID_SERVER

TCP_IP = "127.0.0.1"
TCP_PORT = 8000
ROBOT_ID = 0x01 # R01

# 노드 번호 (a01=1, a02=2, s05=5, 등)
nodes = [1, 2, 3, 4, 5, 11, 14, 8, 9]

def run_fake_robot():
    print(f"🤖 [가상 로봇] {TCP_IP}:{TCP_PORT} 접속 시도...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((TCP_IP, TCP_PORT))
        print("✅ 연결 성공!")
        
        seq = 0
        while True:
            # 텔레메트리 Payload 구조:
            # [0] status_id
            # [1] battery_level
            # [2] current_node_idx
            # [3] task_id_hi
            # [4] task_id_lo
            # [5] line_sensor_state
            # [6] motor_pwm
            # [7] error_id
            batt = random.randint(20, 100)
            node_idx = random.choice(nodes)
            
            payload = bytes([1, batt, node_idx, 0, 0, 0, 100, 0])
            packet = build_packet(MSG_AGV_TELEMETRY, ROBOT_ID, ID_SERVER, seq, payload)
            
            sock.sendall(packet)
            print(f"📤 [상태 전송] 위치: 노드{node_idx} (배터리 {batt}%) | Packet: {packet.hex()}")
            
            seq = (seq + 1) % 256
            time.sleep(2)
            
    except Exception as e:
        print(f"❌ 접속 실패: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    run_fake_robot()
