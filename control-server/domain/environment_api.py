"""
서버 환경 모니터링 API
- 통합 프론트엔드 대시보드(SPA)와 통신하기 위한 JSON API 제공
"""
import psutil
import time
from flask import Blueprint, jsonify, request
from core.sensor_controller import latest_data, manual_overrides

env_bp = Blueprint('environment', __name__)

SERVER_START_TIME = time.time()

@env_bp.route('/api/system_status')
def system_status():
    """서버 시스템 및 육묘장 연결 상태 확인 API"""
    uptime = time.time() - SERVER_START_TIME
    
    # 육묘장 센서 연결 여부 (최근 데이터 갱신 시간 기준)
    now = time.time()
    nursery_connected = False
    if "S11" in latest_data:
        # 10초 이내 갱신되었으면 연결된 것으로 간주
        if (now - latest_data["S11"].get("last_updated", 0)) < 10:
            nursery_connected = True
            
    return jsonify({
        "ok": True,
        "nursery_connected": nursery_connected,
        "uptime_seconds": int(uptime),
        "cpu_percent": psutil.cpu_percent(),
        "memory_percent": psutil.virtual_memory().percent
    })


@env_bp.route('/api/sensor/latest')
def get_latest_sensors():
    """각 노드별 최신 온습도, 조도 센서값"""
    # JS에서 받아서 카드를 채우도록 포맷 변경
    response_data = {}
    for node_id, data in latest_data.items():
        response_data[node_id.lower()] = {
            "temperature": data.get("temp", 0),
            "humidity": data.get("hum", 0),
            "light": data.get("light", 0),
            "updated_at": data.get("last_updated", 0)
        }
    return jsonify({"ok": True, "sensors": response_data})


@env_bp.route('/api/sensor/control', methods=['POST'])
def control_sensor():
    """웹 GUI의 수동 제어 명령을 받아 ESP32에 반영 및 즉시 로깅"""
    from database.db_config import get_db_connection
    from datetime import datetime
    
    data = request.get_json()
    node_id = data.get('node_id', '').lower()
    device = data.get('device')  # 'led', 'val', 'fan'
    state = data.get('state')    # 'ON', 'OFF'
    
    if not node_id or not device or not state:
        return jsonify({"ok": False, "error": "Invalid params"}), 400
        
    if node_id not in manual_overrides:
        manual_overrides[node_id] = {}
        
    cmd = f"{device.upper()}_{state.upper()}"
    manual_overrides[node_id][device.lower()] = cmd
    
    # --- 즉시 로깅 추가 ---
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            # 1. user_action_logs 기록
            user_id = 1 # 기본 admin/system ID (세션 연동 시 세션값 사용 가능)
            action_detail = {"node_id": node_id, "device": device, "state": state, "manual": True}
            cursor.execute("""
                INSERT INTO user_action_logs (user_id, action_type_id, target_id, action_detail, action_result, action_time)
                VALUES (%s, 19, %s, %s, 1, NOW())
            """, (user_id, node_id, json.dumps(action_detail)))
            
            # 2. nursery_actuator_logs 기록 (장치 타입 매핑: VAL=1, FAN=2, LED=3)
            a_type_id = {"val": 1, "fan": 2, "led": 3}.get(device.lower())
            if a_type_id:
                # 해당 노드의 동작기 ID 찾기
                cursor.execute("""
                    SELECT actuator_id FROM nursery_actuators na
                    JOIN nursery_controllers nc ON na.controller_id = nc.controller_id
                    WHERE nc.node_id = %s AND na.actuator_type_id = %s
                """, (node_id.upper(), a_type_id))
                act_row = cursor.fetchone()
                
                if act_row:
                    cursor.execute("""
                        INSERT INTO nursery_actuator_logs (actuator_id, state_value, trigger_id, logged_at)
                        VALUES (%s, %s, 1, NOW())
                    """, (act_row['actuator_id'], state.upper()))
        conn.commit()
    except Exception as e:
        print(f"Manual Log Error: {e}")
    finally:
        conn.close()
        
    return jsonify({"ok": True, "message": f"{node_id.upper()} {device.upper()} -> {state} 설정 및 로그 기록 완료"})


@env_bp.route('/api/logs/inout')
def get_inout_logs():
    """입출고 관련 로그 (A01, A04 노드 관련 운송 작업)"""
    from database.db_config import get_db_connection
    limit = request.args.get('limit', 20, type=int)
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            cursor.execute("""
                SELECT task_id, source_node, destination_node, task_status, ordered_at as time,
                       CASE WHEN source_node = 'a01' THEN '입고' ELSE '출고' END as type
                FROM transport_tasks 
                WHERE source_node IN ('a01', 'a04') OR destination_node IN ('a01', 'a04')
                ORDER BY task_id DESC LIMIT %s
            """, (limit,))
            rows = cursor.fetchall()
            for r in rows:
                if r['time']: r['time'] = r['time'].isoformat()
            return jsonify({"ok": True, "logs": rows})
    finally:
        conn.close()


@env_bp.route('/api/logs/nursery')
def get_nursery_logs():
    """육묘장 통합 로그 (센서 샘플 + 수동 제어)"""
    from database.db_config import get_db_connection
    limit = request.args.get('limit', 20, type=int)
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            # 수동 제어 + 센서 샘플을 통합하여 최신순 정렬
            # 간단하게 연산자 로그와 센서 평균 로그를 섞어서 출력
            cursor.execute("""
                (SELECT 'CONTROL' as type, logged_at as time, 
                        CONCAT(nc.node_id, ' ', CASE na.actuator_type_id WHEN 1 THEN '밸브' WHEN 2 THEN '팬' ELSE 'LED' END, ' ', state_value) as msg
                 FROM nursery_actuator_logs nal
                 JOIN nursery_actuators na ON nal.actuator_id = na.actuator_id
                 JOIN nursery_controllers nc ON na.controller_id = nc.controller_id
                 ORDER BY nal.log_id DESC LIMIT %s)
                UNION ALL
                (SELECT 'SENSOR' as type, measured_at as time,
                        CONCAT(nc.node_id, ' 온도:', value, '℃') as msg
                 FROM nursery_sensor_logs nsl
                 JOIN nursery_sensors ns ON nsl.sensor_id = ns.sensor_id
                 JOIN nursery_controllers nc ON ns.controller_id = nc.controller_id
                 WHERE ns.sensor_type_id = 1
                 ORDER BY nsl.log_id DESC LIMIT %s)
                ORDER BY time DESC LIMIT %s
            """, (limit // 2, limit // 2, limit))
            rows = cursor.fetchall()
            for r in rows:
                if r['time']: r['time'] = r['time'].isoformat()
            return jsonify({"ok": True, "logs": rows})
    finally:
        conn.close()


@env_bp.route('/api/node/<node_id>/details')
def get_node_details(node_id):
    """노드의 상세 정보(품종, 입고일, 예정출고일)를 조회하는 API"""
    from database.db_config import get_db_connection
    from datetime import timedelta
    
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            # 1. 노드, 품종 및 작물 정보 조회
            cursor.execute("""
                SELECT fn.node_name, c.crop_name, sv.variety_name, sv.days_to_harvest
                FROM farm_nodes fn
                JOIN seedling_varieties sv ON fn.current_variety_id = sv.variety_id
                JOIN crops c ON sv.crop_id = c.crop_id
                WHERE fn.node_id = %s
            """, (node_id,))
            node_info = cursor.fetchone()
            
            if not node_info:
                return jsonify({"ok": False, "error": "Node not found"}), 404
            
            # 2. 가장 최근의 입고(운반 완료) 기록 조회 (입고 날짜)
            cursor.execute("""
                SELECT completed_at 
                FROM transport_tasks 
                WHERE destination_node = %s 
                  AND task_status = 2 
                ORDER BY completed_at DESC 
                LIMIT 1
            """, (node_id,))
            task_info = cursor.fetchone()
            
            incoming_date_str = "-"
            outgoing_date_str = "-"
            
            if task_info and task_info['completed_at']:
                incoming_date = task_info['completed_at']
                incoming_date_str = incoming_date.strftime('%Y-%m-%d')
                
                # 출고 예정일 = 입고일 + 수확 일수
                days = node_info['days_to_harvest'] or 0
                outgoing_date = incoming_date + timedelta(days=days)
                outgoing_date_str = outgoing_date.strftime('%Y-%m-%d')
            
            return jsonify({
                "ok": True,
                "node_id": node_id,
                "node_name": node_info['node_name'],
                "crop_name": node_info['crop_name'],
                "variety_name": node_info['variety_name'],
                "incoming_date": incoming_date_str,
                "outgoing_date": outgoing_date_str
            })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    finally:
        conn.close()


@env_bp.route('/api/node/<node_id>/history')
def get_node_history(node_id):
    """노드의 최근 24시간 센서 이력을 조회하는 API"""
    from database.db_config import get_db_connection
    
    conn = get_db_connection()
    try:
        with conn.cursor() as cursor:
            # Temp=1, Humi=2, Light=3
            cursor.execute("""
                SELECT 
                    sl.measured_at,
                    MAX(CASE WHEN s.sensor_type_id = 1 THEN sl.value END) as temp,
                    MAX(CASE WHEN s.sensor_type_id = 2 THEN sl.value END) as humi,
                    MAX(CASE WHEN s.sensor_type_id = 3 THEN sl.value END) as light
                FROM nursery_sensors s
                JOIN nursery_sensor_logs sl ON s.sensor_id = sl.sensor_id
                JOIN nursery_controllers nc ON s.controller_id = nc.controller_id
                WHERE nc.node_id = %s
                AND sl.measured_at >= NOW() - INTERVAL 1 DAY
                GROUP BY sl.measured_at
                ORDER BY sl.measured_at ASC
            """, (node_id,))
            rows = cursor.fetchall()
            
            history = {
                "labels": [],
                "temp": [],
                "humi": [],
                "light": []
            }
            
            for r in rows:
                # ISO 포맷으로 시간 변환
                history["labels"].append(r['measured_at'].strftime('%H:%M'))
                history["temp"].append(float(r['temp']) if r['temp'] is not None else None)
                history["humi"].append(float(r['humi']) if r['humi'] is not None else None)
                history["light"].append(float(r['light']) if r['light'] is not None else None)
                
            return jsonify({
                "ok": True,
                "node_id": node_id,
                "history": history
            })
    except Exception as e:
        return jsonify({"ok": False, "error": str(e)}), 500
    finally:
        conn.close()
