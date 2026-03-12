"""
서버 환경 모니터링 API
- 통합 프론트엔드 대시보드(SPA)와 통신하기 위한 JSON API 제공
"""
import psutil
import time
import json
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
            "humidity": data.get("humi", 0),
            "light": data.get("light", 0),
            "updated_at": data.get("last_updated", 0)
        }
    return jsonify({"ok": True, "sensors": response_data})


@env_bp.route('/api/sensor/control', methods=['POST'])
def control_sensor():
    """웹 GUI의 수동 제어 명령을 받아 ESP32에 반영 및 즉시 로깅"""
    from database.db_config import get_db_connection
    from network.tcp_robot_server import send_actuator_command
    from datetime import datetime
    
    data = request.get_json()
    node_id = data.get('node_id', '').lower()
    device = data.get('device')  # 'led', 'val', 'fan'
    state = data.get('state')    # 'ON', 'OFF'
    
    if not node_id or not device or not state:
        return jsonify({"ok": False, "error": "Invalid params"}), 400
        
    # --- 펌웨어(ESP32)로 명령어 바이너리 전송 ---
    # device = 'mode', 'pump'(val), 'fan', 'heater', 'led'
    # 0=MODE, 1=PUMP, 2=FAN, 3=HEATER, 4=LED
    device_lower = device.lower()
    actuator_id_map = {
        'mode': 0,
        'pump': 1,
        'val': 1, # 호환성 유지
        'fan': 2,
        'heater': 3,
        'led': 4
    }
    
    act_id = actuator_id_map.get(device_lower)
    if act_id is None:
        return jsonify({"ok": False, "error": f"Unknown device: {device}"}), 400
        
    value = 0
    if isinstance(state, str):
        if state.upper() == 'ON':
            value = 1
        elif state.upper() == 'OFF':
            value = 0
        elif state.isdigit():
            value = max(0, min(100, int(state)))
    elif isinstance(state, (int, float)):
        value = max(0, min(100, int(state)))

    # LED 0~100 처리나 기타 필요시 value 확장 가능
    print(f"🎮 [API 수동제어] node={node_id}, device={device}(act_id={act_id}), state={state}, value={value}")
    
    # TCP 소켓으로 직접 전송!
    # 먼저 수동 모드(ACT_ID_MODE=0, VALUE=0)로 강제 전환 후, 장치 제어 전송
    # (Arduino SoftwareSerial 반이중 통신 패킷 유실 방지를 위해 0.2초 대기)
    if act_id != 0:
        send_actuator_command(node_id, 0, 0)
        import time
        time.sleep(0.2)
        
    success = send_actuator_command(node_id, act_id, value)
    if not success:
        return jsonify({"ok": False, "error": f"Node {node_id} is offline or command failed"}), 503

    # 내부 상태 저장
    if node_id not in manual_overrides:
        manual_overrides[node_id] = {}
        
    if isinstance(state, str) and (state.upper() == 'ON' or state.upper() == 'OFF'):
        cmd = f"{device.upper()}_{state.upper()}"
    else:
        cmd = f"{device.upper()}_{value}"
    
    manual_overrides[node_id][device_lower] = cmd
    
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
                    act_id = act_row['actuator_id']
                else:
                    # 자동 등록: 컨트롤러 ID 먼저 확인
                    cursor.execute("SELECT controller_id FROM nursery_controllers WHERE node_id = %s LIMIT 1", (node_id.upper(),))
                    ctrl_row = cursor.fetchone()
                    if ctrl_row:
                        ctrl_id = ctrl_row['controller_id']
                    else:
                        ctrl_id = f"CTRL_{node_id.upper()}"
                        cursor.execute("INSERT IGNORE INTO nursery_controllers (controller_id, node_id) VALUES (%s, %s)", (ctrl_id, node_id.upper()))
                    
                    # 액추에이터 등록 (핀 번호는 0으로 임시 설정)
                    cursor.execute("""
                        INSERT INTO nursery_actuators (controller_id, actuator_type_id, pin_number)
                        VALUES (%s, %s, 0)
                    """, (ctrl_id, a_type_id))
                    act_id = cursor.lastrowid

                # 로그 삽입
                cursor.execute("""
                    INSERT INTO nursery_actuator_logs (actuator_id, state_value, trigger_id, logged_at)
                    VALUES (%s, %s, 1, NOW())
                """, (act_id, state.upper()))
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
                SELECT * FROM (
                    (SELECT 'CONTROL' as type, logged_at as time, 
                            CONCAT(nc.node_id, ' ', CASE na.actuator_type_id WHEN 1 THEN '밸브' WHEN 2 THEN '팬' ELSE 'LED' END, ' ', state_value) as msg
                     FROM nursery_actuator_logs nal
                     JOIN nursery_actuators na ON nal.actuator_id = na.actuator_id
                     JOIN nursery_controllers nc ON na.controller_id = nc.controller_id
                     ORDER BY nal.log_id DESC LIMIT %s)
                    UNION ALL
                    (SELECT 'SENSOR' as type, measured_at as time,
                            CONCAT(nc.node_id, 
                                   MAX(CASE WHEN ns.sensor_type_id = 1 THEN CONCAT(' 온도:', value, '℃') ELSE '' END),
                                   MAX(CASE WHEN ns.sensor_type_id = 2 THEN CONCAT(' 습도:', value, CHAR(37)) ELSE '' END),
                                   MAX(CASE WHEN ns.sensor_type_id = 3 THEN CONCAT(' 광량:', value, 'lx') ELSE '' END)) as msg
                     FROM nursery_sensor_logs nsl
                     JOIN nursery_sensors ns ON nsl.sensor_id = ns.sensor_id
                     JOIN nursery_controllers nc ON ns.controller_id = nc.controller_id
                     GROUP BY nc.node_id, measured_at
                     ORDER BY measured_at DESC LIMIT %s)
                ) combined
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
