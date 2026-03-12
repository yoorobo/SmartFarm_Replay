"""
가상 AGV 로봇 (TCP 8000 바이너리 통신)
- 시나리오: A01(입고장)에서 화분 싣고 → S11 이동 → S11에서 화분 싣고 → A04(출고장)로 이동 → 내려놓기
- PathFinder.cpp 실제 그래프 기반 경로
- next_node 정보를 payload에 포함하여 프론트엔드 방향 애니메이션 지원
"""
import socket
import time
import sys
import os

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from network.sfam_protocol import build_packet, MSG_AGV_TELEMETRY, ID_SERVER

TCP_IP = "127.0.0.1"
TCP_PORT = 8000
ROBOT_ID = 0x01

NODE_MAP = {
    'A01': 1,  'A02': 2,  'A03': 3,  'A04': 4,
    'S05': 5,  'S06': 6,  'S07': 7,
    'R08': 8,  'R09': 9,  'R10': 10,
    'S11': 11, 'S12': 12, 'S13': 13,
    'R14': 14, 'R15': 15, 'R16': 16,
    None: 0,   # 다음 노드 없음 (정지)
}

# 시나리오: (현재노드, 다음노드, 대기시간, status_id, 설명)
# status_id: 1=MOVING, 2=IDLE, 3=LOADING, 4=UNLOADING
# 다음노드=None → 정지 상태 (애니메이션 없음)
SCENARIO = [
    # ── 화분 적재 (입고장 A01) ──
    ('A01', 'A02', 4, 2, '입고장 대기 (IDLE)'),
    ('A01', 'A02', 3, 3, '🪴 화분 적재 중...'),

    # ── A01 → S11 이동 (경로: A01 → A02 → S06 → S05 → S11) ──
    ('A02', 'S06', 4, 1, '이동 중: A01 → A02'),
    ('S06', 'S05', 4, 1, '이동 중: A02 → S06 (상단 분기)'),
    ('S05', 'S11', 4, 1, '이동 중: S06 → S05'),
    ('S11', None, 4, 1, '이동 중: S05 → S11'),

    # ── S11 도착, 화분 하차 ──
    ('S11', None, 4, 2, '묘종실 S11 도착 (IDLE)'),
    ('S11', None, 3, 4, '🪴 화분 하차 중...'),

    # ── S11에서 새 화분 적재 ──
    ('S11', 'S05', 3, 3, '🪴 새 화분 적재 중...'),

    # ── S11 → A04 이동 (경로: S11 → S05 → S06 → A02 → A03 → A04) ──
    ('S05', 'S06', 4, 1, '이동 중: S11 → S05'),
    ('S06', 'A02', 4, 1, '이동 중: S05 → S06'),
    ('A02', 'A03', 4, 1, '이동 중: S06 → A02 (메인 합류)'),
    ('A03', 'A04', 4, 1, '이동 중: A02 → A03'),
    ('A04', None, 4, 1, '이동 중: A03 → A04'),

    # ── A04 도착, 화분 하차 ──
    ('A04', None, 4, 2, '출고장 A04 도착 (IDLE)'),
    ('A04', None, 3, 4, '🪴 화분 하차 완료'),

    # ── 복귀 (A04 → A03 → A02 → A01) ──
    ('A03', 'A02', 4, 1, '복귀 중: A04 → A03'),
    ('A02', 'A01', 4, 1, '복귀 중: A03 → A02'),
    ('A01', None, 4, 1, '복귀 중: A02 → A01'),
    ('A01', None, 5, 2, '입고장 복귀 완료 (IDLE)'),
]

def run_fake_robot():
    print(f"🤖 [가상 로봇] {TCP_IP}:{TCP_PORT} 접속 시도...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((TCP_IP, TCP_PORT))
        print("✅ 연결 성공!")
        print("📋 시나리오: A01(입고) → A02 → S06 → S05 → S11(묘종실)")
        print("         S11 → S05 → S06 → A02 → A03 → A04(출고) → A01(복귀)")
        print("=" * 60)
        
        seq = 0
        battery = 95
        cycle = 1
        
        while True:
            print(f"\n{'='*60}")
            print(f"🔄 사이클 #{cycle} 시작")
            print(f"{'='*60}")
            
            for node_name, next_node, wait_sec, status_id, description in SCENARIO:
                node_idx = NODE_MAP[node_name]
                next_idx = NODE_MAP[next_node]  # 0 if next_node is None
                
                # 텔레메트리 Payload (8 bytes):
                # [0] status_id  [1] battery  [2] current_node_idx
                # [3] next_node_idx (다음 목적지)  [4] task_id_lo
                # [5] line_sensor  [6] motor_pwm  [7] error_id
                motor_pwm = 100 if status_id == 1 else 0
                payload = bytes([status_id, battery, node_idx, next_idx, cycle, 0, motor_pwm, 0])
                packet = build_packet(MSG_AGV_TELEMETRY, ROBOT_ID, ID_SERVER, seq, payload)
                
                sock.sendall(packet)
                
                status_emoji = {1: '🚚', 2: '⏸️', 3: '📦⬆️', 4: '📦⬇️'}.get(status_id, '❓')
                next_label = next_node or '정지'
                print(f"  {status_emoji} [{node_name}→{next_label}] {description} | 🔋{battery}%")
                
                seq = (seq + 1) % 256
                battery = max(10, battery - 1)
                
                time.sleep(wait_sec)
            
            battery = min(100, battery + 30)
            print(f"\n🔋 충전 완료! 배터리: {battery}%")
            cycle += 1
            time.sleep(5)
            
    except KeyboardInterrupt:
        print("\n🛑 가상 로봇 종료")
    except Exception as e:
        print(f"❌ 접속 실패: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    run_fake_robot()
