"""
RFID 카드 인식 처리 모듈
- 작업자/화분 카드 인식 및 입출고 처리
- 원본: integrated_server.py → RFID 관련 라우트 핸들러
"""
import re
from datetime import datetime
from flask import Blueprint, request, jsonify
from database.db_config import get_db_connection

rfid_bp = Blueprint('rfid', __name__)

# ─── 전역 상태 ───
station_state = {"mode": "IDLE"}

# ─── RFID 카드 등록 정보 ───
REGISTERED_USERS = {
    "412E261B": {"name": "관리자 A", "role": "admin", "action": "open_door"},
    "049C37026C2190": {"name": "작업자 B", "role": "worker", "action": "call_robot_R01"},
    "049C36026C2190": {"name": "작업자 C", "role": "worker", "action": "call_robot_R02"}
}

# 입고장 작업자 제어용 마스터 카드
WORKER_CARDS = {
    "B034105F": "INBOUND",
    "412E261B": "OUTBOUND"
}

POT_CARDS = [
    "049C37026C2190", "049C36026C2190", "049C33026C2190", "049C32026C2190",
    "049C31026C2190", "049C30026C2190", "049C2F026C2190", "049C2E026C2190",
    "049C2D026C2190", "049C2A026C2190"
]


@rfid_bp.route('/api/rfid', methods=['POST'])
def receive_rfid():
    content = request.get_json()
    uid = content.get('uid')

    if not uid:
        return jsonify({"status": "error", "message": "UID is missing"}), 400

    print(f"💳 [RFID 인식] 수신된 UID: {uid}")

    if uid in REGISTERED_USERS:
        user_info = REGISTERED_USERS[uid]
        print(f"✅ 인증 성공: {user_info['name']} ({user_info['role']})")
        return jsonify({"status": "success", "message": f"{user_info['name']}님 환영합니다."}), 200
    else:
        print(f"❌ 미등록 카드 접근 시도: {uid}")
        return jsonify({"status": "error", "message": "미등록 카드입니다."}), 403


@rfid_bp.route('/api/rfid_inoutbound', methods=['POST'])
def receive_rfid_inoutbound():
    global station_state

    content = request.get_json()
    if not content:
        return jsonify({"status": "error", "message": "No JSON data"}), 400

    uid = content.get('uid')
    if not uid:
        return jsonify({"status": "error", "message": "UID is missing"}), 400

    uid = str(uid).replace(" ", "").upper()

    if uid in WORKER_CARDS:
        new_mode = WORKER_CARDS[uid]
        station_state["mode"] = new_mode
        mode_kr = "입고(INBOUND)" if new_mode == "INBOUND" else "출고(OUTBOUND)"
        print(f"\n🔄 [모드 변경] 작업자가 {mode_kr} 카드를 태그했습니다. 화분 카드를 스캔해주세요.")
        return jsonify({"status": "success", "mode": new_mode, "message": f"{mode_kr} 대기 중"}), 200

    elif uid in POT_CARDS:
        current_mode = station_state["mode"]

        if current_mode == "IDLE":
            print(f"⚠️ [경고] 화분({uid})이 스캔되었으나, 작업 모드가 설정되지 않았습니다.")
            return jsonify({"status": "error", "message": "먼저 입고 또는 출고 카드를 태그하세요!"}), 200

        elif current_mode == "INBOUND":
            print(f"📦 [입고 처리] 화분(NFC:{uid}) 입고 등록 시도 중...")

            try:
                conn = get_db_connection()
                with conn.cursor() as cursor:
                    # 1. 태그 UID로 트레이 정보 확인
                    cursor.execute("""
                        SELECT tray_id, variety_id, tray_status
                        FROM trays WHERE nfc_uid = %s AND is_active = TRUE
                    """, (uid,))
                    tray_info = cursor.fetchone()

                    if not tray_info:
                        return jsonify({"status": "error", "message": f"NFC({uid})에 매핑된 활성 트레이가 없습니다."}), 404
                    
                    if tray_info['tray_status'] in [2, 3, 4]:  # IN_TRANSIT, EMPTY, DAMAGED
                        return jsonify({"status": "error", "message": "입고할 수 없는 트레이 상태입니다."}), 400

                    variety_id = tray_info['variety_id']

                    # 2. 해당 품종(variety)을 보관할 수 있고 빈자리가 있는 재배구역(node_type=3) 노드 탐색
                    cursor.execute("""
                        SELECT node_id, node_name, (max_capacity - current_quantity) as empty_spots
                        FROM farm_nodes 
                        WHERE node_type_id = 3 
                          AND current_variety_id = %s 
                          AND is_active = TRUE
                          AND current_quantity < max_capacity
                        ORDER BY empty_spots DESC 
                        LIMIT 1
                    """, (variety_id,))
                    node_info = cursor.fetchone()

                    if not node_info:
                        return jsonify({"status": "error", "message": "해당 품종을 적재할 빈 재배구역이 없습니다!"}), 404

                    target_node = node_info['node_id']
                    source_node = 'A01' # 입고장

                    # 3. 빈 AGV 찾기 (현재 가장 단순하게 R01 고정, 추후 스케줄러가 알아서 할당하도록 null 처리도 가능. 여기서는 임의로 R01 지정)
                    agv_id = 'R01'

                    # 4. 배차 지시 저장 (transport_tasks)
                    sql = """INSERT INTO transport_tasks 
                            (agv_id, variety_id, source_node, destination_node, ordered_by, quantity, task_status, ordered_at) 
                            VALUES (%s, %s, %s, %s, %s, %s, %s, %s)"""
                    task_values = (agv_id, variety_id, source_node, target_node, 1, 1, 0, datetime.now())
                    cursor.execute(sql, task_values)
                    
                    task_id = cursor.lastrowid
                    
                    # 5. 트레이 상태 IN_TRANSIT(2) 변경
                    cursor.execute("UPDATE trays SET tray_status = 2 WHERE nfc_uid = %s", (uid,))

                conn.commit()
                print(f"🤖 AGV 배차 완료! [입고장({source_node}) → {node_info['node_name']}({target_node})] (Task ID: {task_id})")

                station_state["mode"] = "IDLE"
                return jsonify({
                    "status": "success", 
                    "message": f"화분 {uid[-4:]} 입고. 목적지 탐색 성공: {node_info['node_name']}"
                }), 200

            except Exception as e:
                print(f"❌ DB 입고 스캔 에러: {e}")
                return jsonify({"status": "error", "message": "DB 처리 중 오류 발생"}), 500
            finally:
                if 'conn' in locals() and conn:
                    conn.close()

        elif current_mode == "OUTBOUND":
            print(f"🚚 [출고 처리] 화분({uid}) 출고 등록 완료!")
            return jsonify({"status": "success", "message": f"화분 {uid[-4:]} 출고 완료"}), 200

    else:
        print(f"❌ [에러] 미등록 카드 태그됨: {uid}")
        return jsonify({"status": "error", "message": "등록되지 않은 카드입니다."}), 200
