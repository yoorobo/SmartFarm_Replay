"""
fake_robot.py
=============
ESP32 로봇을 시뮬레이션하는 테스트 클라이언트.
실제 하드웨어 없이 서버 ↔ GUI 통신을 테스트할 때 사용한다.

실행:
    python3 fake_robot.py

동작:
    1) 서버에 TCP 연결
    2) 1초마다 ROBOT_STATE JSON을 서버에 전송
    3) 서버에서 명령(MOVE, TASK 등)이 오면 화면에 출력
"""

import socket
import json
import time
import random
import threading

SERVER_IP = "127.0.0.1"
SERVER_PORT = 8080
ROBOT_ID = "AGV-01"


def receive_commands(sock: socket.socket):
    """서버에서 오는 명령을 수신하고 출력하는 스레드."""
    buffer = ""
    while True:
        try:
            data = sock.recv(4096)
            if not data:
                print("\n🔌 서버 연결 끊김")
                break
            buffer += data.decode("utf-8", errors="replace")

            # 줄바꿈 단위로 메시지 분리
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue

                msg = json.loads(line)
                print(f"\n📥 [서버→로봇] 명령 수신: {json.dumps(msg, ensure_ascii=False)}")
                print("=" * 60)

                # 명령별 시뮬레이션 응답
                cmd = msg.get("cmd")
                if cmd == "MOVE":
                    target = msg.get("target_node", msg.get("path", "?"))
                    print(f"   🚗 이동 명령! 목적지: {target}")
                elif cmd == "MANUAL":
                    device = msg.get("device", "?")
                    state = msg.get("state", "?")
                    print(f"   🔧 수동 제어! {device} → {state}")
                elif cmd == "TASK":
                    action = msg.get("action", "?")
                    print(f"   🎯 작업 명령! 종류: {action}")

                print("=" * 60)

        except json.JSONDecodeError:
            continue
        except Exception as e:
            print(f"\n❌ 수신 오류: {e}")
            break


def main():
    print("=" * 60)
    print("  🤖 가짜 ESP32 로봇 클라이언트 (테스트용)")
    print(f"  서버: {SERVER_IP}:{SERVER_PORT}")
    print("=" * 60)

    # ── 서버 연결 ──
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        sock.connect((SERVER_IP, SERVER_PORT))
        print(f"\n✅ 서버 연결 성공! ({SERVER_IP}:{SERVER_PORT})")
    except ConnectionRefusedError:
        print(f"\n❌ 서버 연결 실패! 서버가 실행 중인지 확인하세요.")
        print(f"   → 먼저 터미널1에서: cd control-server && python3 main_server.py")
        return

    # ── 수신 스레드 시작 ──
    recv_thread = threading.Thread(target=receive_commands, args=(sock,), daemon=True)
    recv_thread.start()

    # ── 상태 전송 루프 ──
    tick = 0
    states = ["IDLE", "FORWARD", "FORWARD", "FORWARD", "SOFT_LEFT",
              "FORWARD", "FORWARD", "SOFT_RIGHT", "FORWARD", "ARRIVED"]

    print("\n📡 1초마다 ROBOT_STATE 전송 시작 (Ctrl+C로 종료)\n")

    try:
        while True:
            tick += 1
            state_idx = tick % len(states)

            # IR 센서 시뮬레이션
            sensors = [0, 0, 1, 0, 0]  # 기본: 중앙만 라인 위
            if "LEFT" in states[state_idx]:
                sensors = [1, 1, 1, 0, 0]
            elif "RIGHT" in states[state_idx]:
                sensors = [0, 0, 1, 1, 1]

            robot_state = {
                "type": "ROBOT_STATE",
                "robot_id": ROBOT_ID,
                "pos_x": (tick * 15) % 500,
                "pos_y": 200 + random.randint(-30, 30),
                "battery": max(20, 100 - tick % 80),
                "state": state_idx,
                "node": f"A{(tick // 5) % 4 + 1}",
                "sensors": sensors,
                "plant_id": "RFID-TOMATO-01" if tick % 15 == 0 else "",
            }

            json_str = json.dumps(robot_state, ensure_ascii=False) + "\n"
            sock.sendall(json_str.encode("utf-8"))
            print(f"📤 [로봇→서버] 상태 전송: 위치=({robot_state['pos_x']},{robot_state['pos_y']}), "
                  f"배터리={robot_state['battery']}%, 상태={states[state_idx]}, "
                  f"센서={sensors}")

            time.sleep(1)

    except KeyboardInterrupt:
        print("\n\n🛑 로봇 클라이언트 종료")
    finally:
        sock.close()


if __name__ == "__main__":
    main()
