"""
자동 작업 배차 루프 (Task Dispatcher)
- 3초 주기로 transport_tasks 테이블에서 대기 중인 작업을 확인하고,
  TCP 연결된 로봇에게 이동 명령을 전송합니다.
- 원본: integrated_server.py → task_dispatcher_loop()
"""
import json
import time
from datetime import datetime
from database.db_config import get_db_connection
from network.tcp_robot_server import active_tcp_connections


def task_dispatcher_loop():
    """자동 작업 반장 (3초 주기 폴링)"""
    print("🚚 [Task Dispatcher] 자동 작업 반장 가동 시작! (3초 주기)")
    while True:
        time.sleep(3)
        conn = None
        try:
            conn = get_db_connection()
            with conn.cursor() as cursor:
                # task_status: 0 (대기중)인 작업만 가져오기
                sql = "SELECT * FROM transport_tasks WHERE task_status = 0 ORDER BY ordered_at ASC"
                cursor.execute(sql)
                pending_tasks = cursor.fetchall()

                for task in pending_tasks:
                    agv_id = task['agv_id']
                    task_id = task['task_id']
                    dest_node = task['destination_node']

                    if agv_id in active_tcp_connections:
                        sock = active_tcp_connections[agv_id]
                        command_json = json.dumps({"cmd": "MOVE", "target_node": dest_node}) + "\n"

                        try:
                            sock.sendall(command_json.encode('utf-8'))
                            print(f"\n🚀 [명령 하달 성공!] Task ID: {task_id} -> {agv_id} 로봇 [{dest_node}] 이동!")

                            # 명령 전송 성공 시 상태를 1(진행중)로 변경
                            update_sql = "UPDATE transport_tasks SET task_status = 1, started_at = %s WHERE task_id = %s"
                            cursor.execute(update_sql, (datetime.now(), task_id))
                            conn.commit()

                        except Exception as e:
                            print(f"❌ 명령 전송 실패 (소켓 에러): {e}")
                            del active_tcp_connections[agv_id]

        except Exception:
            pass  # DB 연결이 안 될 때는 조용히 다음 주기를 기다림
        finally:
            if conn:
                conn.close()
