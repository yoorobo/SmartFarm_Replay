"""
통합 시나리오 가상 시뮬레이터 (test_rfid_scenario.py)
---------------------------------------------------
1. 서버 띄우기 (별도 터미널)
2. 이 스크립트를 실행하여 아래의 하드웨어 동작을 시뮬레이션:
   - [작업자] 육묘장 RFID로 "입고(INBOUND)" 카드 태그 -> POST /api/rfid_inoutbound
   - [작업자] 화분(POT) 카드 태그 -> POST /api/rfid_inoutbound (서버에서 빈자리 탐색 후 DB 배차 트리거)
   - [AGV] 픽업지로 이동하여 트레이 밑면 RFID 스캔 (0x24 바이너리 패킷 전송) -> Task IN_PROGRESS
   - [AGV] 목적지 도착하여 안착 후 RFID 스캔 (0x24 바이너리 패킷 전송) -> Task DONE & 빈자리 감소
"""
import requests
import socket
import struct
import time
from sfam_protocol import build_packet, MSG_RFID_EVENT, ID_SERVER

SERVER_HTTP_URL = "http://127.0.0.1:5001/api/rfid_inoutbound"
SERVER_TCP_HOST = "127.0.0.1"
SERVER_TCP_PORT = 8000

# 테스트에 사용할 RFID 태그 정보 (DB에 등록되어 있다고 가정)
CARD_INBOUND = "B034105F"
CARD_POT_TRAY = "049C37026C2190"  # 등록된 화분 1
AGV_ID = 0x01 # R01

print("========================================")
print("🍓 스마트팜 AGV 물류 자동화 통합 시뮬레이터 🚀")
print("========================================")
time.sleep(1)

# ─── 1. 작업자 입고 모드 활성화 ───
print("\n[SCENE 1] 작업자가 입고장 리더기에 '입고(INBOUND)' 카드를 태그합니다.")
resp = requests.post(SERVER_HTTP_URL, json={"uid": CARD_INBOUND})
print(f"-> 서버 응답: {resp.json()}")
time.sleep(2)

# ─── 2. 모종 화분 태그 (DB 배차 지시 유발) ───
print("\n[SCENE 2] 작업자가 화분(TRAY) 카드를 리더기에 태그합니다.")
resp = requests.post(SERVER_HTTP_URL, json={"uid": CARD_POT_TRAY})
print(f"-> 서버 응답: {resp.json()}")
time.sleep(3) # DB에 배차(Task 0)가 들어가고, Task Dispatcher가 읽을 시간을 줌

# ─── 3. AGV TCP 접속 및 픽업 스캔 시뮬레이션 ───
try:
    print("\n[SCENE 3] AGV가 입고장으로 도착하여 트레이 RFID를 스캔합니다.")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((SERVER_TCP_HOST, SERVER_TCP_PORT))
    
    # 0x24 MSG_RFID_EVENT 바이너리 패킷 전송 (Payload: UID 텍스트)
    payload = CARD_POT_TRAY.encode('ascii')
    pickup_pkt = build_packet(MSG_RFID_EVENT, AGV_ID, ID_SERVER, 1, payload)
    
    sock.sendall(pickup_pkt)
    print("-> AGV: 픽업 스캔 바이너리 패킷 전송 완료! (Task 1: IN_PROGRESS 상태 예상)")
    time.sleep(4) # 주행 시간 시뮬레이션
    
# ─── 4. AGV 목적지 도착 하차 스캔 ───
    print("\n[SCENE 4] AGV가 목적지(재배구역)에 도착하여 안착 확인 스캔을 진행합니다.")
    dropoff_pkt = build_packet(MSG_RFID_EVENT, AGV_ID, ID_SERVER, 2, payload)
    sock.sendall(dropoff_pkt)
    print("-> AGV: 안착 스캔 바이너리 패킷 전송 완료! (Task 2: DONE 상태 예상)")
    
    time.sleep(1)
    sock.close()
except ConnectionRefusedError:
    print("❌ TCP 서버(8000)가 켜져 있지 않습니다. 서버를 띄운 후 실행하세요.")
except Exception as e:
    print(f"❌ 시뮬레이션 에러: {e}")

print("\n🎉 모든 통합 테스트 시나리오가 종료되었습니다. DB(task_status, quantities)를 확인하세요.")
