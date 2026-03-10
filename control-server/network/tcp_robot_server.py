"""
로봇/센서 TCP 소켓 서버
- 포트 8000에서 하드웨어(AGV, 육묘장 센서 등)의 TCP 연결을 수신합니다.
- CRC16-CCITT 바이너리 패킷 (SFAM Protocol)을 파싱하여 대시보드 및 DB에 반영합니다.
"""
import socket
import threading
import struct
from datetime import datetime
from database.db_config import get_db_connection
from core.sensor_controller import latest_data
from network.sfam_protocol import (
    SfamParser, build_packet, MSG_HEARTBEAT_REQ, MSG_HEARTBEAT_ACK,
    MSG_AGV_TELEMETRY, MSG_SENSOR_BATCH, MSG_RFID_EVENT, ID_SERVER
)

# TCP 연결된 클라이언트 소켓 관리
active_tcp_connections = {}
last_logged_states = {}

def handle_hardware_client(client_socket, addr):
    """하드웨어 TCP 클라이언트 (바이너리 프로토콜) 수신 스레드"""
    print(f"📡 [TCP Server] 하드웨어 접속: {addr}")
    
    parser = SfamParser()
    client_id = None
    
    while True:
        try:
            data = client_socket.recv(4096)
            if not data:
                break
                
            for b in data:
                packet = parser.process_byte(b)
                if packet:
                    msg_type = packet['msg_type']
                    src_id = packet['src_id']
                    seq = packet['seq']
                    payload = packet['payload']
                    
                    if not client_id:
                        client_id = f"0x{src_id:02X}"
                        active_tcp_connections[client_id] = client_socket
                        
                    # 1. 하트비트 요청 (0x01)
                    if msg_type == MSG_HEARTBEAT_REQ:
                        ack_payload = bytes([1, 0]) # ONLINE
                        ack_pkt = build_packet(MSG_HEARTBEAT_ACK, ID_SERVER, src_id, seq, ack_payload)
                        client_socket.sendall(ack_pkt)
                    
                    # 2. 로봇 텔레메트리 데이터 (0x10)
                    elif msg_type == MSG_AGV_TELEMETRY:
                        # 0: status, 1: batt, 2: node_idx, 3-4: task_id, 5: line_mask, 6: pwm, 7: err
                        if len(payload) >= 8:
                            batt = payload[1]
                            node_idx = payload[2]
                            
                            robot_name = f"r0{8 if src_id == 0x01 else 9}"
                            node_pos = f"NODE-{node_idx}"
                            
                            latest_data[robot_name] = {
                                "status": f"🔋 {batt}%<br>📍 {node_pos}",
                                "last_seen": datetime.now().strftime('%H:%M:%S')
                            }
                            
                            # DB 로깅
                            conn = get_db_connection()
                            try:
                                with conn.cursor() as cursor:
                                    agv_db_id = "R01" if src_id == 0x01 else f"R0{src_id}"
                                    cursor.execute(
                                        "INSERT IGNORE INTO agv_robots (agv_id, status_id) VALUES (%s, 1)",
                                        (agv_db_id,)
                                    )
                                    cursor.execute(
                                        "INSERT INTO agv_telemetry_logs (agv_id, current_node, logged_at) VALUES (%s, %s, %s)",
                                        (agv_db_id, node_pos, datetime.now())
                                    )
                                conn.commit()
                            except Exception as e:
                                print(f"DB Error: {e}")
                            finally:
                                conn.close()
                                
                    # 3. 육묘장 센서 배치 (0x20)
                    elif msg_type == MSG_SENSOR_BATCH:
                        if len(payload) > 0:
                            count = payload[0]
                            idx = 1
                            temp = hum = light = 0
                            
                            for _ in range(count):
                                if idx + 3 >= len(payload): break
                                s_id = payload[idx]
                                # 24bit signed int 변환 (대충)
                                val_bytes = bytes([0]) + payload[idx+1:idx+4]
                                val = struct.unpack('>i', val_bytes)[0] / 10.0
                                
                                if s_id == 0x01: temp = val
                                elif s_id == 0x02: hum = val
                                elif s_id == 0x03: light = val * 10.0 # light는 스케일링 복구
                                idx += 4
                                
                            latest_data["nursery"] = {
                                "status": f"🌡️ {temp:.1f}°C<br>💧 {hum:.1f}%<br>☀️ {int(light)}lx",
                                "last_seen": datetime.now().strftime('%H:%M:%S')
                            }
                            
                            # DB 로깅
                            conn = get_db_connection()
                            try:
                                with conn.cursor() as cursor:
                                    now = datetime.now()
                                    # 예시로 sensor_id 1:온도, 2:습도, 3:조도로 가정하여 저장합니다.
                                    # 실제 구축 시 nursery_sensors 테이블을 참조할 수 있습니다.
                                    cursor.execute(
                                        "INSERT INTO nursery_sensor_logs (sensor_id, value, measured_at) VALUES (%s, %s, %s)",
                                        (1, temp, now)
                                    )
                                    cursor.execute(
                                        "INSERT INTO nursery_sensor_logs (sensor_id, value, measured_at) VALUES (%s, %s, %s)",
                                        (2, hum, now)
                                    )
                                    cursor.execute(
                                        "INSERT INTO nursery_sensor_logs (sensor_id, value, measured_at) VALUES (%s, %s, %s)",
                                        (3, light, now)
                                    )
                                conn.commit()
                            except Exception as e:
                                print(f"Nursery Sensor DB Error: {e}")
                            finally:
                                conn.close()
                                
                    # 4. AGV 로봇이 통신으로 보낸 RFID 태그 인식 이벤트 (0x24)
                    elif msg_type == MSG_RFID_EVENT:
                        print(f"🔗 [AGV RFID EVENT] 수신 Payload: {payload.hex()}")
                        # 가정한 payload 포맷: 카드 UID 문자열 혹은 바이트
                        # 앞뒤 공백 제거나 널바이트 처리 후 문자열 변환
                        scanned_uid = payload.decode('ascii', errors='ignore').strip('\x00').strip().upper()
                        print(f"🤖 [AGV RFID 스캔] 로봇: {src_id:02X}, 카드 UID: {scanned_uid}")
                        
                        conn = get_db_connection()
                        try:
                            with conn.cursor() as cursor:
                                agv_db_id = "R01" if src_id == 0x01 else f"R0{src_id}"
                                
                                # 1단계: 스캔한 카드가 어느 트레이인지 확인
                                cursor.execute("SELECT tray_id FROM trays WHERE nfc_uid = %s", (scanned_uid,))
                                tray_info = cursor.fetchone()
                                
                                if tray_info:
                                    # 2단계: 이 로봇이 현재 수행 중인(또는 대기 중인) 트레이 관련 운송 작업 찾기
                                    # PENDING(0) 이나 IN_PROGRESS(1) 인 작업
                                    cursor.execute("""
                                        SELECT task_id, source_node, destination_node, task_status 
                                        FROM transport_tasks 
                                        WHERE agv_id = %s AND task_status IN (0, 1)
                                    """, (agv_db_id,))
                                    active_task = cursor.fetchone()
                                    
                                    if active_task:
                                        t_id = active_task['task_id']
                                        t_status = active_task['task_status']
                                        src_node = active_task['source_node']
                                        dst_node = active_task['destination_node']
                                        
                                        # 최신 텔레메트리에서 이 로봇의 위치 짐작 (현재 노드)
                                        # (임시 방편: DB의 agv_telemetry_logs 가장 최신값 확인)
                                        cursor.execute("SELECT current_node FROM agv_telemetry_logs WHERE agv_id = %s ORDER BY log_id DESC LIMIT 1", (agv_db_id,))
                                        last_log = cursor.fetchone()
                                        curr_node = last_log['current_node'] if last_log else None
                                        
                                        print(f"   -> [상태 검사] 현재AGV위치: {curr_node}, 목적지: {dst_node}, 작업상태: {t_status}")
                                        
                                        # 시나리오 A: 출발지에서 픽업 확인 (PENDING(0) -> IN_PROGRESS(1))
                                        if t_status == 0:
                                            print(f"   -> [픽업 확인] AGV가 출발지({src_node})에서 트레이 탑재 완료. 이동 시작.")
                                            cursor.execute("UPDATE transport_tasks SET task_status = 1, started_at = %s WHERE task_id = %s", (datetime.now(), t_id))
                                            
                                        # 시나리오 B: 목적지에서 하차 확인 (IN_PROGRESS(1) -> DONE(2))
                                        elif t_status == 1:
                                            # (실제로는 curr_node == dst_node 여부도 검사해야 하나, 단순화)
                                            print(f"   -> [도착/하차 확인] AGV가 목적지({dst_node})에 트레이 안착 완료. (Task: {t_id})")
                                            cursor.execute("UPDATE transport_tasks SET task_status = 2, completed_at = %s WHERE task_id = %s", (datetime.now(), t_id))
                                            # 빈자리 1개 채움 기록
                                            cursor.execute("UPDATE farm_nodes SET current_quantity = current_quantity + 1 WHERE node_id = %s", (dst_node,))
                                            # 트레이 상태 GROWING(5) 으로 업데이트
                                            cursor.execute("UPDATE trays SET tray_status = 1, growth_stage = 5 WHERE nfc_uid = %s", (scanned_uid,))
                                            
                                    else:
                                        print("   -> 할당된 작업이 없는 상태에서 스캔되었습니다.")
                                else:
                                    print("   -> 시스템(trays 테이블)에 등록되지 않은 카드입니다.")
                                    
                            conn.commit()
                        except Exception as e:
                            print(f"AGV RFID DB Error: {e}")
                        finally:
                            conn.close()
            
        except Exception as e:
            print(f"[TCP Server] Error processing data: {e}")
            break

    if client_id and client_id in active_tcp_connections:
        del active_tcp_connections[client_id]
    client_socket.close()


def tcp_robot_server():
    """하드웨어 TCP 서버 메인 루프 (포트 8000)"""
    tcp_port = 8000
    
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        server_socket.bind(('0.0.0.0', tcp_port))
        server_socket.listen(5)
        print(f"\n🚀 [TCP Binary Server] 하드웨어 패킷 수신 대기 (포트: {tcp_port})")
    except OSError as e:
        print(f"\n❌ [TCP Server] 포트 {tcp_port} 사용 불가: {e}")
        return

    while True:
        try:
            client_socket, addr = server_socket.accept()
            threading.Thread(target=handle_hardware_client, args=(client_socket, addr), daemon=True).start()
        except:
            break
