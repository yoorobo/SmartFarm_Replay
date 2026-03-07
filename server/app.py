"""
app.py - Flask 웹서버 + TCP 로봇 브로커 + ESP32-CAM UDP 수신
=======================================
- 웹 UI: 목적지 입력, 카메라 뷰
- SQLite: 작업 저장
- TCP(8080): ESP32 로봇 연결 수락 및 GOTO 명령 전송
- UDP(7070): ESP32-CAM 이미지 수신 → 웹에서 실시간 스트리밍
"""

import json
import select
import socket
import threading
import time

from flask import Flask, Response, render_template, request, jsonify

from db import init_db, add_task, get_recent_tasks, mark_task_sent, VALID_NODES

app = Flask(__name__)

# ========== ESP32-CAM UDP 수신 (포트 7070) ==========
CAMERA_UDP_PORT = 7070
_latest_camera_frame: bytes | None = None
_camera_frame_lock = threading.Lock()
_camera_frame_event = threading.Event()
_camera_fps = 0.0
_camera_last_update = 0.0

# 연결된 로봇 클라이언트: { robot_id: socket }
_connected_robots: dict[str, socket.socket] = {}
_robots_lock = threading.Lock()

# 로봇이 보낸 최신 상태 (ROBOT_STATE 수신 시 갱신)
_latest_robot_state: dict | None = None
_robot_state_lock = threading.Lock()

# 로봇 수신용 버퍼 (rid -> 누적 문자열)
_robot_recv_buffers: dict[str, str] = {}

# TCP 서버 소켓
_tcp_server: socket.socket | None = None


def node_name_to_index(node_name: str) -> int | None:
    """노드 이름(a01 등)을 인덱스(0~)로 변환. 없으면 None."""
    if not node_name:
        return None
    name = node_name.strip().lower()
    try:
        return VALID_NODES.index(name)
    except ValueError:
        return None


def tcp_listen_thread():
    """로봇 TCP 연결 대기 스레드 (포트 8080)"""
    global _tcp_server
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", 8080))
    server.listen(5)
    _tcp_server = server
    print("[TCP] 로봇 대기 포트 8080에서 수신 대기 중...")

    while True:
        try:
            client, addr = server.accept()
            client.settimeout(30)
            # 로봇 ID는 첫 수신 메시지에서 추출 가능. 일단 addr로 식별
            rid = f"{addr[0]}:{addr[1]}"
            with _robots_lock:
                # 기존 같은 주소 연결이 있으면 닫기
                if rid in _connected_robots:
                    try:
                        _connected_robots[rid].close()
                    except Exception:
                        pass
                _connected_robots[rid] = client
            print(f"[TCP] 로봇 연결: {addr} (id={rid})")
        except Exception as e:
            if _tcp_server and server.fileno() != -1:
                print(f"[TCP] accept 오류: {e}")
            break


def robot_state_reader_thread():
    """로봇이 보내는 ROBOT_STATE 메시지를 수신·파싱하여 _latest_robot_state에 저장"""
    global _latest_robot_state, _robot_recv_buffers
    while True:
        try:
            with _robots_lock:
                socks = list(_connected_robots.items())
            if not socks:
                time.sleep(0.5)
                continue

            rlist, _, xlist = select.select(
                [s for _, s in socks],
                [],
                [s for _, s in socks],
                0.5,
            )

            for sock in rlist + xlist:
                rid = None
                with _robots_lock:
                    for k, v in _connected_robots.items():
                        if v is sock:
                            rid = k
                            break
                if rid is None:
                    continue

                try:
                    if sock in xlist:
                        raise OSError("소켓 예외")
                    data = sock.recv(4096).decode("utf-8", errors="ignore")
                    if not data:
                        raise ConnectionError("연결 종료")
                except Exception as e:
                    print(f"[TCP] 로봇 수신 오류 ({rid}): {e} - 연결 제거")
                    with _robots_lock:
                        if rid in _connected_robots and _connected_robots[rid] is sock:
                            try:
                                sock.close()
                            except Exception:
                                pass
                            del _connected_robots[rid]
                    if rid in _robot_recv_buffers:
                        del _robot_recv_buffers[rid]
                    continue

                if rid not in _robot_recv_buffers:
                    _robot_recv_buffers[rid] = ""
                _robot_recv_buffers[rid] += data

                while "\n" in _robot_recv_buffers[rid]:
                    line, _robot_recv_buffers[rid] = _robot_recv_buffers[rid].split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        doc = json.loads(line)
                        if doc.get("type") == "ROBOT_STATE":
                            node_idx = doc.get("node_idx", -1)
                            state = doc.get("state", 0)
                            node_name = (
                                VALID_NODES[node_idx]
                                if 0 <= node_idx < len(VALID_NODES)
                                else "-"
                            )
                            with _robot_state_lock:
                                _latest_robot_state = {
                                    "robot_id": doc.get("robot_id", "R01"),
                                    "node_idx": node_idx,
                                    "node_name": node_name,
                                    "state": state,
                                    "arrived": state == 11,  # ARRIVED
                                    "dir": doc.get("dir", 0),
                                }
                    except json.JSONDecodeError:
                        pass

        except Exception as e:
            print(f"[robot_state_reader] 오류: {e}")
            time.sleep(1)


def camera_udp_receiver_thread():
    """ESP32-CAM UDP 패킷 수신·조립 스레드 (포트 7070)"""
    global _latest_camera_frame, _camera_fps, _camera_last_update

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.settimeout(1.0)
    try:
        sock.bind(("0.0.0.0", CAMERA_UDP_PORT))
    except OSError as e:
        print(f"[UDP] 포트 {CAMERA_UDP_PORT} 바인드 실패: {e}")
        return
    print(f"[UDP] ESP32-CAM 수신 대기 중... 포트 {CAMERA_UDP_PORT}")

    frames: dict[int, dict] = {}
    last_frame_no: dict[int, int] = {}

    while True:
        try:
            data, _ = sock.recvfrom(2048)
            if len(data) < 5:
                continue

            vehicle_id = data[0]
            f_no = data[1]
            p_no = data[2]
            received_checksum = data[3]
            chunk = data[4:]

            if vehicle_id not in last_frame_no:
                last_frame_no[vehicle_id] = -1
            lfn = last_frame_no[vehicle_id]
            if f_no < lfn and (lfn - f_no) < 200:
                continue

            if vehicle_id not in frames:
                frames[vehicle_id] = {}
            vframes = frames[vehicle_id]

            if f_no not in vframes:
                if len(vframes) > 3:
                    del vframes[min(vframes.keys())]
                vframes[f_no] = {"chunks": {}, "target_checksum": 0}
            vframes[f_no]["chunks"][p_no] = chunk

            if chunk.find(b"\xff\xd9") != -1:
                vframes[f_no]["target_checksum"] = received_checksum
                indices = sorted(vframes[f_no]["chunks"].keys())

                if indices and indices[0] == 0 and len(indices) == indices[-1] + 1:
                    full_data = b"".join([vframes[f_no]["chunks"][i] for i in indices])
                    calculated_checksum = sum(full_data) % 256

                    if calculated_checksum == vframes[f_no]["target_checksum"]:
                        with _camera_frame_lock:
                            _latest_camera_frame = full_data
                        _camera_frame_event.set()
                        now_t = time.time()
                        if _camera_last_update > 0:
                            _camera_fps = 1.0 / (now_t - _camera_last_update)
                        _camera_last_update = now_t
                        last_frame_no[vehicle_id] = f_no

                frames[vehicle_id] = {k: v for k, v in vframes.items() if k > f_no}
        except socket.timeout:
            continue
        except Exception as e:
            print(f"[UDP] 수신 오류: {e}")
            time.sleep(0.5)


def send_to_robot(robot_id: str | None, cmd: dict) -> bool:
    """로봇에게 JSON 명령 전송. robot_id가 None이면 첫 번째 연결된 로봇에 전송.
    전송 실패 시 해당 소켓을 _connected_robots에서 제거한다."""
    payload = json.dumps(cmd) + "\n"
    with _robots_lock:
        if robot_id and robot_id in _connected_robots:
            rid = robot_id
            sock = _connected_robots[rid]
        elif _connected_robots:
            rid = next(iter(_connected_robots.keys()))
            sock = _connected_robots[rid]
        else:
            return False
        try:
            sock.sendall(payload.encode("utf-8"))
            return True
        except Exception as e:
            print(f"[TCP] 전송 실패 ({rid}): {e} - 연결 제거")
            try:
                sock.close()
            except Exception:
                pass
            if rid in _connected_robots and _connected_robots[rid] is sock:
                del _connected_robots[rid]
            return False


@app.route("/")
def index():
    return render_template("index.html", nodes=VALID_NODES)


@app.route("/api/tasks", methods=["POST"])
def create_task():
    """목적지 작업 생성 및 로봇에 전송"""
    data = request.get_json() or request.form
    destination = (data.get("destination") or "").strip().lower()
    robot_id = (data.get("robot_id") or "R01").strip()

    if not destination:
        return jsonify({"ok": False, "error": "목적지를 입력하세요"}), 400

    try:
        task_id = add_task(destination, robot_id)
    except ValueError as e:
        return jsonify({"ok": False, "error": str(e)}), 400

    # GOTO 명령 구성 (로봇이 target_node로 인식)
    cmd = {"cmd": "GOTO", "target_node": destination}

    if send_to_robot(robot_id, cmd):
        mark_task_sent(task_id)
        return jsonify({"ok": True, "task_id": task_id, "message": f"목적지 {destination}로 전송 완료"})
    else:
        return jsonify({
            "ok": False,
            "error": "연결된 로봇이 없습니다. 로봇 전원과 Wi-Fi 연결을 확인하세요.",
            "task_id": task_id,
        }), 503


@app.route("/api/tasks")
def list_tasks():
    """최근 작업 목록"""
    limit = request.args.get("limit", 20, type=int)
    tasks = get_recent_tasks(limit=limit)
    return jsonify({"ok": True, "tasks": tasks})


@app.route("/api/robots")
def list_robots():
    """연결된 로봇 목록"""
    with _robots_lock:
        robots = list(_connected_robots.keys())
    return jsonify({"ok": True, "robots": robots})


@app.route("/api/robot_state")
def robot_state():
    """로봇의 최신 상태 (현재 노드, 도착 여부)"""
    with _robot_state_lock:
        st = _latest_robot_state
    if st is None:
        return jsonify({"ok": True, "state": None, "message": "아직 로봇 상태를 수신하지 않았습니다."})
    return jsonify({"ok": True, "state": st})


# 최소 1x1 회색 JPEG (카메라 미연결 시 placeholder)
_PLACEHOLDER_JPEG = (
    b"\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
    b"\xff\xdb\x00C\x00\x08\x06\x06\x07\x06\x05\x08\x07\x07\x07\t\t\x08\n\x0c\x14\r\x0c\x0b\x0b\x0c\x19\x12\x13\x0f"
    b"\x14\x1d\x1a\x1f\x1e\x1d\x1a\x1c\x1c $.' \",#\x1c\x1c(7),01444\x1f'9=82<.342\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00\xff\xc4\x00\x1f\x00\x00\x01\x05\x01\x01\x01\x01\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x02\x03\x04\x05\x06\x07\x08\t\n\x0b\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00?\x00\xb8\xe0\x7f\xff\xd9"
)


@app.route("/api/camera/stream")
def camera_stream():
    """MJPEG 스트림 (ESP32-CAM 실시간 영상)"""
    def generate():
        boundary = "frame"
        while True:
            got_event = _camera_frame_event.wait(timeout=2.0)
            with _camera_frame_lock:
                frame = _latest_camera_frame
            jpeg = frame if frame else _PLACEHOLDER_JPEG
            yield (
                f"--{boundary}\r\n"
                f"Content-Type: image/jpeg\r\n"
                f"Content-Length: {len(jpeg)}\r\n\r\n"
            ).encode()
            yield jpeg
            yield b"\r\n"
            if frame:
                _camera_frame_event.clear()

    return Response(
        generate(),
        mimetype="multipart/x-mixed-replace; boundary=frame",
        headers={"Cache-Control": "no-cache"},
    )


@app.route("/api/camera/snapshot")
def camera_snapshot():
    """최신 프레임 단건 반환 (image/jpeg)"""
    with _camera_frame_lock:
        frame = _latest_camera_frame
    if not frame:
        return Response("카메라 연결 대기 중", status=503, mimetype="text/plain")
    return Response(frame, mimetype="image/jpeg")


@app.route("/api/camera/status")
def camera_status():
    """카메라 연결/FPS 상태"""
    with _camera_frame_lock:
        has_frame = _latest_camera_frame is not None
    return jsonify({
        "ok": True,
        "connected": has_frame,
        "fps": round(_camera_fps, 1) if has_frame else 0,
    })


@app.route("/api/set_location", methods=["POST"])
def set_location():
    """현재 로봇 위치를 수동으로 설정 (SET_LOC)"""
    data = request.get_json() or request.form

    node_name = (data.get("node_name") or "").strip().lower()
    direction = (data.get("direction") or "").strip().upper()
    robot_id = (data.get("robot_id") or "").strip() or None

    if not node_name:
        return jsonify({"ok": False, "error": "노드 이름을 선택하세요."}), 400
    if not direction:
        return jsonify({"ok": False, "error": "방향을 선택하세요."}), 400

    idx = node_name_to_index(node_name)
    if idx is None:
        return jsonify({"ok": False, "error": f"유효하지 않은 노드: {node_name}"}), 400

    dir_map = {"N": 0, "E": 1, "S": 2, "W": 3}
    if direction not in dir_map:
        return jsonify({"ok": False, "error": f"유효하지 않은 방향: {direction} (N/E/S/W 사용)"}), 400
    dir_code = dir_map[direction]

    cmd = {"cmd": "SET_LOC", "node": idx, "dir": dir_code}

    if send_to_robot(robot_id, cmd):
        return jsonify(
            {
                "ok": True,
                "message": f"현재 위치를 {node_name} ({direction})로 설정 요청을 전송했습니다.",
                "node_index": idx,
                "dir": dir_code,
            }
        )
    else:
        return (
            jsonify(
                {
                    "ok": False,
                    "error": "연결된 로봇이 없습니다. 로봇 전원과 Wi-Fi 연결을 확인하세요.",
                }
            ),
            503,
        )


def main():
    init_db()
    threading.Thread(target=tcp_listen_thread, daemon=True).start()
    threading.Thread(target=robot_state_reader_thread, daemon=True).start()
    threading.Thread(target=camera_udp_receiver_thread, daemon=True).start()

    host = "0.0.0.0"
    port = 5000
    print(f"\n[웹서버] http://{host}:{port} 에서 실행 중")
    print("[안내] 노트북 IP로 웹 브라우저 접속 후 목적지를 입력하세요.")
    print("       로봇(ESP32): 이 PC의 IP:8080 으로 TCP 연결")
    print(f"       ESP32-CAM: 이 PC의 IP:{CAMERA_UDP_PORT} 으로 UDP 전송\n")
    app.run(host=host, port=port, debug=False, use_reloader=False)


if __name__ == "__main__":
    main()
