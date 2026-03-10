"""
가상 AGV 로봇 (TCP 8000)
- 주기적으로 로봇 상태(위치, 배터리)를 TCP 소켓으로 전송합니다.
"""
import socket
import json
import time
import random

TCP_IP = "127.0.0.1"
TCP_PORT = 8000
ROBOT_ID = "R01"

nodes = ["a01", "a02", "a03", "s05", "r08"]

def run_fake_robot():
    print(f"🤖 [가상 로봇] {TCP_IP}:{TCP_PORT} 연결 시도...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((TCP_IP, TCP_PORT))
        print("✅ 연결 성공!")
        
        while True:
            state = {
                "type": "ROBOT_STATE",
                "robot_id": ROBOT_ID,
                "node": random.choice(nodes),
                "pos_x": random.randint(0, 100),
                "pos_y": random.randint(0, 1000),
                "battery": random.randint(20, 100)
            }
            msg = json.dumps(state) + "\n"
            sock.sendall(msg.encode("utf-8"))
            print(f"📤 [상태 전송] {state['node']} (배터리 {state['battery']}%)")
            time.sleep(2)
            
    except Exception as e:
        print(f"❌ 연결 실패: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    run_fake_robot()
