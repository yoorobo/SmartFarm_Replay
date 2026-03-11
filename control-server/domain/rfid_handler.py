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

# ─── 전역 상태 (필요 시 확장용으로 남겨둠) ───
station_state = {"mode": "IDLE"}


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
    """RFID 화분 인식을 통한 즉시 입고 처리 (마스터 카드 필요 없음)"""
    content = request.get_json()
    if not content:
        return jsonify({"status": "error", "message": "No JSON data"}), 400

    uid = content.get('uid')
    if not uid:
        return jsonify({"status": "error", "message": "UID is missing"}), 400

    # 공백 제거 및 대문자 변환
    uid = str(uid).replace(" ", "").upper()
    print(f"📦 [RFID 스캔] 화분(NFC:{uid}) 인식됨. 입고 처리 시도...")

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
                print(f"❌ [에러] NFC({uid})에 매핑된 활성 트레이가 없습니다.")
                return jsonify({"status": "error", "message": f"NFC({uid})에 매핑된 활성 트레이가 없습니다."}), 404
            
            # 2. 트레이 상태 확인 (이미 이동 중이거나 비어있으면 불가)
            if tray_info['tray_status'] in [2, 3, 4]:  # IN_TRANSIT, EMPTY, DAMAGED
                status_map = {2: "이동 중", 3: "비어 있음", 4: "파손"}
                msg = f"상태가 '{status_map.get(tray_info['tray_status'])}'인 트레이는 입고할 수 없습니다."
                print(f"⚠️ [경고] {msg}")
                return jsonify({"status": "error", "message": msg}), 400

            variety_id = tray_info['variety_id']

            # 3. 해당 품종(variety)을 보관할 수 있고 빈자리가 있는 재배구역(node_type=1: STATION) 노드 탐색
            cursor.execute("""
                SELECT node_id, node_name, (max_capacity - current_quantity) as empty_spots
                FROM farm_nodes 
                WHERE node_type_id = 1 
                  AND current_variety_id = %s 
                  AND is_active = TRUE
                  AND current_quantity < max_capacity
                ORDER BY empty_spots DESC 
                LIMIT 1
            """, (variety_id,))
            node_info = cursor.fetchone()

            if not node_info:
                print(f"⚠️ [경고] 품종 ID {variety_id}을 적재할 빈 재배구역이 없습니다!")
                return jsonify({"status": "error", "message": "해당 품종을 적재할 빈 재배구역이 없습니다!"}), 404

            target_node = node_info['node_id']
            source_node = 'A01' # 기본 출발지는 입고장

            # 4. 배차 지시 저장 (transport_tasks)
            # AGV ID는 현재 시스템에서 기본값으로 리팩토링 중이므로 R01 지정
            agv_id = 'R01'
            sql = """INSERT INTO transport_tasks 
                    (agv_id, variety_id, source_node, destination_node, ordered_by, quantity, task_status, ordered_at) 
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s)"""
            task_values = (agv_id, variety_id, source_node, target_node, 1, 1, 0, datetime.now())
            cursor.execute(sql, task_values)
            
            task_id = cursor.lastrowid
            
            # 5. 트레이 상태 IN_TRANSIT(2) 변경
            cursor.execute("UPDATE trays SET tray_status = 2 WHERE nfc_uid = %s", (uid,))

        conn.commit()
        print(f"🤖 [배차 완료] {source_node} → {node_info['node_name']}({target_node}) (Task ID: {task_id})")

        return jsonify({
            "status": "success", 
            "message": f"화분 {uid[-4:]} 입고 시작. 목적지: {node_info['node_name']}",
            "task_id": task_id
        }), 200

    except Exception as e:
        print(f"❌ [DB 에러] {e}")
        return jsonify({"status": "error", "message": f"DB 처리 중 오류 발생: {str(e)}"}), 500
    finally:
        if 'conn' in locals() and conn:
            conn.close()
