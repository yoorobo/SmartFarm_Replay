"""
가상 AGV 로봇 (TCP 8000 바이너리 통신)
- 시나리오: A01(입고장)에서 화분 싣고 → S11 이동 → S11에서 화분 싣고 → A04(출고장)로 이동 → 내려놓기
- 실제 경로를 따라 노드 단위로 이동하며 SFAM 텔레메트리 전송
"""
import socket
import time
import sys
import os

# 부모 디렉토리를 path에 추가하여 network 모듈을 임포트
sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from network.sfam_protocol import build_packet, MSG_AGV_TELEMETRY, ID_SERVER

TCP_IP = "127.0.0.1"
TCP_PORT = 8000
ROBOT_ID = 0x01  # R01

# 노드 인덱스 매핑 (트랙맵 기준)
NODE_MAP = {
    'A01': 1,  'A02': 2,  'A03': 3,  'A04': 4,
    'S05': 5,  'S06': 6,  'S07': 7,
    'R08': 8,  'R09': 9,  'R10': 10,
    'S11': 11, 'S12': 12, 'S13': 13,
    'R14': 14, 'R15': 15, 'R16': 16,
}

# 시나리오 정의: (노드이름, 대기시간(초), 상태설명)
# status_id: 1=MOVING, 2=IDLE, 3=LOADING, 4=UNLOADING
SCENARIO = [
    # ── 화분 적재 (입고장 A01) ──
    ('A01', 3, 2, '입고장 대기 (IDLE)'),
    ('A01', 2, 3, '🪴 화분 적재 중...'),

    # ── A01 → S11 이동 (경로: A01 → S05 → S11) ──
    ('S05', 2, 1, '이동 중: A01 → S05'),
    ('S11', 2, 1, '이동 중: S05 → S11'),

    # ── S11 도착, 화분 하차 ──
    ('S11', 3, 2, '묘종실 S11 도착 (IDLE)'),
    ('S11', 2, 4, '🪴 화분 하차 중...'),

    # ── S11에서 새 화분 적재 ──
    ('S11', 2, 3, '🪴 새 화분 적재 중...'),

    # ── S11 → A04 이동 (경로: S11 → S05 → A01 → A02 → A03 → A04) ──
    ('S05', 2, 1, '이동 중: S11 → S05'),
    ('A01', 2, 1, '이동 중: S05 → A01'),
    ('A02', 2, 1, '이동 중: A01 → A02'),
    ('A03', 2, 1, '이동 중: A02 → A03'),
    ('A04', 2, 1, '이동 중: A03 → A04'),

    # ── A04 도착, 화분 하차 ──
    ('A04', 3, 2, '출고장 A04 도착 (IDLE)'),
    ('A04', 2, 4, '🪴 화분 하차 완료'),

    # ── 복귀 (A04 → A03 → A02 → A01) ──
    ('A03', 2, 1, '복귀 중: A04 → A03'),
    ('A02', 2, 1, '복귀 중: A03 → A02'),
    ('A01', 2, 1, '복귀 중: A02 → A01'),
    ('A01', 3, 2, '입고장 복귀 완료 (IDLE)'),
]

def run_fake_robot():
    print(f"🤖 [가상 로봇] {TCP_IP}:{TCP_PORT} 접속 시도...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((TCP_IP, TCP_PORT))
        print("✅ 연결 성공!")
        print("📋 시나리오: A01(입고) → S11(묘종실) → A04(출고) → A01(복귀)")
        print("=" * 60)
        
        seq = 0
        battery = 95  # 시작 배터리
        cycle = 1
        
        while True:
            print(f"\n{'='*60}")
            print(f"🔄 사이클 #{cycle} 시작")
            print(f"{'='*60}")
            
            for node_name, wait_sec, status_id, description in SCENARIO:
                node_idx = NODE_MAP[node_name]
                
                # 텔레메트리 Payload 구조 (8 bytes):
                # [0] status_id    [1] battery   [2] node_idx
                # [3] task_id_hi   [4] task_id_lo
                # [5] line_sensor  [6] motor_pwm [7] error_id
                motor_pwm = 100 if status_id == 1 else 0
                payload = bytes([status_id, battery, node_idx, 0, cycle, 0, motor_pwm, 0])
                packet = build_packet(MSG_AGV_TELEMETRY, ROBOT_ID, ID_SERVER, seq, payload)
                
                sock.sendall(packet)
                
                status_emoji = {1: '🚚', 2: '⏸️', 3: '📦⬆️', 4: '📦⬇️'}.get(status_id, '❓')
                print(f"  {status_emoji} [{node_name}] {description} | 🔋{battery}%")
                
                seq = (seq + 1) % 256
                battery = max(10, battery - 1)  # 배터리 점진 감소
                
                time.sleep(wait_sec)
            
            # 사이클 완료 후 충전 시뮬레이션
            battery = min(100, battery + 30)
            print(f"\n🔋 충전 완료! 배터리: {battery}%")
            cycle += 1
            time.sleep(3)
            
    except KeyboardInterrupt:
        print("\n🛑 가상 로봇 종료")
    except Exception as e:
        print(f"❌ 접속 실패: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    run_fake_robot()
