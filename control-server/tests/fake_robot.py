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
from network.sfam_protocol import build_packet, MSG_AGV_TELEMETRY, MSG_RFID_EVENT, ID_SERVER
from database.db_config import get_db_connection

TCP_IP = "127.0.0.1"
TCP_PORT = 8000
ROBOT_ID = 0x01

# 노드 이름 <-> ID 매핑 (PathFinder 그래프 기준)
NODE_MAP = {
    'A01': 1,  'A02': 2,  'A03': 3,  'A04': 4,
    'S05': 5,  'S06': 6,  'S07': 7,
    'R08': 8,  'R09': 9,  'R10': 10,
    'S11': 11, 'S12': 12, 'S13': 13,
    'R14': 14, 'R15': 15, 'R16': 16,
    None: 0,   # 다음 노드 없음 (정지 상태, 애니메이션 없음)
}

# 시나리오: (현재노드, 다음노드, 대기시간, status_id, 설명, trigger_rfid)
# trigger_rfid: True면 해당 단계 종료 후 RFID 이벤트 전송 (로그 발생 지점)
SCENARIO = [
    # ── 출발지(A01)에서 화분 적재 후 출발 시 스캔 ──
    ('A01', 'A02', 4, 2, '입고장 대기 (IDLE)', False),
    ('A01', 'A02', 3, 3, '🪴 화분 적재 중...', True), # 스캔 -> 작업 시작 로그

    ('A02', 'S06', 4, 1, '이동 중: A01 → A02', False),
    ('S06', 'S05', 4, 1, '이동 중: A02 → S06 (상단 분기)', False),
    ('S05', 'S11', 4, 1, '이동 중: S06 → S05', False),
    ('S11', None, 4, 1, '이동 중: S05 → S11', False),

    # ── 목적지(S11) 도착 후 하차 시 스캔 ──
    ('S11', None, 4, 2, '묘종실 S11 도착 (IDLE)', False),
    ('S11', None, 3, 4, '🪴 화분 하차 중...', True), # 스캔 -> 작업 완료 로그

    # ── 다시 S11에서 새 화분 적재 후 출발 시 스캔 ──
    ('S11', 'S05', 3, 3, '🪴 새 화분 적재 중...', True), # 스캔 -> 새 작업(출고) 시작 로그

    ('S05', 'S06', 4, 1, '이동 중: S11 → S05', False),
    ('S06', 'A02', 4, 1, '이동 중: S05 → S06', False),
    ('A02', 'A03', 4, 1, '이동 중: S06 → A02 (메인 합류)', False),
    ('A03', 'A04', 4, 1, '이동 중: A02 → A03', False),
    ('A04', None, 4, 1, '이동 중: A03 → A04', False),

    # ── 목적지(A04) 도착 후 하차 시 스캔 ──
    ('A04', None, 4, 2, '출고장 A04 도착 (IDLE)', False),
    ('A04', None, 3, 4, '🪴 화분 하차 완료', True), # 스캔 -> 작업 완료 로그

    # ── 공차 복귀 ──
    ('A03', 'A02', 4, 1, '복귀 중: A04 → A03', False),
    ('A02', 'A01', 4, 1, '복귀 중: A03 → A02', False),
    ('A01', None, 4, 1, '복귀 중: A02 → A01', False),
    ('A01', None, 5, 2, '입고장 복귀 완료 (IDLE)', False),
]

def get_db_info():
    """테스트를 위해 현재 활성 작업 ID와 유효한 카드 ID를 DB에서 조회"""
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            # 상태가 0(대기) 또는 1(진행중)인 가장 최근 작업 ID
            cursor.execute("SELECT task_id FROM transport_tasks WHERE task_status IN (0, 1) ORDER BY task_id DESC LIMIT 1")
            task = cursor.fetchone()
            # 등록된 활성 트레이의 NFC UID
            cursor.execute("SELECT nfc_uid FROM trays WHERE is_active=TRUE LIMIT 1")
            tray = cursor.fetchone()
            return (task['task_id'] if task else 0, tray['nfc_uid'] if tray else "ABC12345")
    except:
        return (0, "ABC12345")
    finally:
        conn.close()

def fetch_latest_task_id(robot_id=1, auto_create=False):
    """DB에서 해당 로봇의 최신 활성(status 0 or 1) 작업 ID를 가져옴. 
    auto_create=True이면 작업이 없을 때 테스트용으로 강제 생성."""
    # 숫자형 ID가 들어오면 문자형(R01)으로 변환
    if isinstance(robot_id, int):
        agv_id = f"R{robot_id:02d}"
    else:
        agv_id = robot_id

    try:
        conn = get_db_connection()
        with conn.cursor() as cursor:
            cursor.execute(
                "SELECT task_id FROM transport_tasks WHERE agv_id = %s AND task_status IN (0, 1) ORDER BY task_id DESC LIMIT 1",
                (agv_id,)
            )
            row = cursor.fetchone()
            if row:
                return row['task_id']
            elif auto_create:
                print(f"   [DB] 할당된 작업이 없어 테스트용 입고 작업(A01->S11)을 자동 생성합니다.")
                from datetime import datetime
                cursor.execute("INSERT IGNORE INTO agv_robots (agv_id, status_id) VALUES (%s, 1)", (agv_id,))
                cursor.execute("""
                    INSERT INTO transport_tasks 
                    (agv_id, variety_id, source_node, destination_node, ordered_by, quantity, task_status, ordered_at) 
                    VALUES (%s, 1, 'a01', 's11', 1, 1, 0, %s)
                """, (agv_id, datetime.now()))
                conn.commit()
                return cursor.lastrowid
    except Exception as e:
        print(f"⚠️ Task ID 조회 실패: {e}")
    return 0

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
        
        # NFC UID는 시뮬레이션 시작 시 한 번만 가져옴 (실제로는 RFID 태그마다 다를 수 있음)
        _, nfc_uid = get_db_info() 

        while True:
            # 매 사이클 시작 시 실제 작업 정보를 동기화 (없으면 자동 생성)
            task_id = fetch_latest_task_id(ROBOT_ID, auto_create=True)
            print(f"\n{'='*60}")
            print(f"🔄 사이클 #{cycle} 시작 (연동 Task ID: {task_id})")
            print(f"{'='*60}")
            
            for node_name, next_node, wait_sec, status_id, description, trigger_rfid in SCENARIO:
                # A01 또는 S11 전력 노드 근처에서 작업 ID를 적극적으로 갱신 (서버 자동 생성 작업 감지)
                if node_name in ('A01', 'S11', 'A04'):
                    new_task_id = fetch_latest_task_id(ROBOT_ID, auto_create=False)
                    if new_task_id != task_id and new_task_id != 0:
                        print(f"   [SYNC] 새 작업 감지: {task_id} -> {new_task_id}")
                        task_id = new_task_id

                node_idx = NODE_MAP[node_name]
                next_idx = NODE_MAP[next_node]
                
                # 텔레메트리 Payload (8 bytes)
                motor_pwm = 100 if status_id == 1 else 0
                payload = bytes([status_id, battery, node_idx, next_idx, task_id & 0xFF, 0, motor_pwm, 0])
                packet = build_packet(MSG_AGV_TELEMETRY, ROBOT_ID, ID_SERVER, seq, payload)
                sock.sendall(packet)
                
                status_emoji = {1: '🚚', 2: '⏸️', 3: '📦⬆️', 4: '📦⬇️'}.get(status_id, '❓')
                next_label = next_node or '정지'
                print(f"  {status_emoji} [{node_name}→{next_label}] {description} | 🔋{battery}%")
                
                time.sleep(wait_sec)
                
                # RFID 태그 인식 시뮬레이션 (특정 트리거 지점에서만 전송)
                if trigger_rfid: 
                    print(f"  🔗 [RFID 인식] 태그 전송: {nfc_uid} (서버 로그 트리거)")
                    rfid_payload = nfc_uid.encode('ascii')
                    rfid_pkt = build_packet(MSG_RFID_EVENT, ROBOT_ID, ID_SERVER, seq, rfid_payload)
                    sock.sendall(rfid_pkt)
                    time.sleep(1.5) # 서버 처리 여유 시간 넉넉히

                seq = (seq + 1) % 256
                battery = max(10, battery - 1)
            
            battery = min(100, battery + 30)
            print(f"\n🔋 충전 완료! 배터리: {battery}%")
            cycle += 1
            time.sleep(5)
            
    except KeyboardInterrupt:
        print("\n🛑 가상 로봇 종료")
    except Exception as e:
        print(f"❌ 오류 발생: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    run_fake_robot()
