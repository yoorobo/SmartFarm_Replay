"""
gui_network_client.py
=====================
PyQt6 기반 관제 대시보드에서 사용하는 네트워크 클라이언트 모듈.

역할:
    1) UdpReceiver  – QThread를 상속, 백그라운드에서 UDP 데이터를 수신하여
                      PyQt6 시그널(Signal)로 메인 GUI 스레드에 전달.
    2) TcpCommander – 버튼 클릭 등 사용자 액션 시 서버에 TCP 명령을 전송하고
                      응답을 받는 유틸리티 클래스.

[통신 규격]

  ● UDP 수신 (서버 → GUI):
    - 센서: {"type": "SENSOR", "node_id": "NODE-A1-001", "temp": 24.5, "hum": 65.0}
    - 로봇: {"type": "ROBOT_STATE", "robot_id": "R01", "pos_x": 120, "pos_y": 350, "battery": 80}

  ● TCP 송신 (GUI → 서버):
    - 이동: {"cmd": "MOVE", "target_node": "NODE-A1-001"}
    - 작업: {"cmd": "TASK", "action": "PICK_AND_PLACE", "count": 5}
    - 수동: {"cmd": "MANUAL", "device": "FAN", "state": "ON"}

  ● TCP 응답 (서버 → GUI):
    - {"status": "SUCCESS", "msg": "..."}
"""

import json
import socket

from PyQt6.QtCore import QThread, pyqtSignal


# ============================================================
#  서버 접속 정보 (팀 전용 Private 레포 – 하드코딩)
# ============================================================
SERVER_IP = "3.35.24.94"        # AWS EC2 서버 IP
UDP_PORT = 9000                 # UDP 수신 포트 (센서/로봇 상태)
TCP_PORT = 9001                 # TCP 통신 포트 (제어 명령)
BUFFER_SIZE = 4096              # 수신 버퍼 크기 (바이트)


# ============================================================
#  UDP 수신 스레드 (QThread)
# ============================================================

class UdpReceiver(QThread):
    """
    백그라운드 스레드에서 UDP 소켓으로 서버의 브로드캐스트 데이터를 수신하고,
    PyQt6 시그널을 통해 GUI 메인 스레드에 데이터를 전달하는 클래스.

    시그널:
        sensor_received(dict)      – 센서 데이터 수신 시 발행
        robot_state_received(dict) – 로봇 상태 수신 시 발행

    사용 예 (메인 윈도우에서):
        self.udp_thread = UdpReceiver()
        self.udp_thread.sensor_received.connect(self.update_sensor_display)
        self.udp_thread.robot_state_received.connect(self.update_robot_display)
        self.udp_thread.start()
    """

    # ── PyQt6 시그널 정의 ──
    # GUI 메인 스레드의 슬롯(함수)에 연결하여 UI 업데이트에 사용한다.
    sensor_received = pyqtSignal(dict)       # 센서 데이터 수신 시그널
    robot_state_received = pyqtSignal(dict)  # 로봇 상태 수신 시그널

    def __init__(self, port: int = UDP_PORT, parent=None):
        super().__init__(parent)
        self._port = port
        self._running = True  # 스레드 실행 플래그

    def run(self):
        """
        QThread의 메인 루프. UDP 소켓을 열고 데이터를 무한 수신한다.
        수신된 JSON을 파싱하여 type에 따라 적절한 시그널을 emit한다.

        TODO (팀원 구현):
            1) UDP 소켓 바인딩 및 수신 루프 완성
            2) 수신 데이터를 JSON으로 파싱
            3) type 필드에 따라 sensor_received 또는 robot_state_received 시그널 emit
        """
        print(f"📡 [UdpReceiver] UDP 수신 스레드 시작 (포트: {self._port})")

        # ── UDP 소켓 생성 및 바인딩 ──
        udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        udp_socket.bind(("", self._port))
        udp_socket.settimeout(1.0)  # 1초 타임아웃 (스레드 종료 체크용)

        while self._running:
            try:
                data, addr = udp_socket.recvfrom(BUFFER_SIZE)
                raw_str = data.decode("utf-8")

                # JSON 파싱
                message = json.loads(raw_str)
                msg_type = message.get("type")

                # ── 메시지 타입에 따라 시그널 발행 ──
                if msg_type == "SENSOR":
                    # GUI의 센서 디스플레이 슬롯에 연결됨
                    self.sensor_received.emit(message)

                elif msg_type == "ROBOT_STATE":
                    # GUI의 로봇 상태 디스플레이 슬롯에 연결됨
                    self.robot_state_received.emit(message)

                else:
                    print(f"⚠️ [UdpReceiver] 알 수 없는 타입: {msg_type}")

            except socket.timeout:
                # 타임아웃은 정상 – _running 플래그 체크를 위해 필요
                continue
            except json.JSONDecodeError as e:
                print(f"❌ [UdpReceiver] JSON 파싱 실패: {e}")
            except Exception as e:
                print(f"❌ [UdpReceiver] 수신 오류: {e}")

        udp_socket.close()
        print("🔌 [UdpReceiver] UDP 수신 스레드 종료")

    def stop(self):
        """스레드를 안전하게 종료한다."""
        self._running = False
        self.wait()  # 스레드 종료 대기


# ============================================================
#  TCP 명령 송신 클래스
# ============================================================

class TcpCommander:
    """
    GUI에서 버튼 클릭 등의 이벤트 발생 시
    서버에 TCP로 제어 명령(JSON)을 전송하고 응답을 받는 클래스.

    사용 예 (메인 윈도우에서):
        self.tcp = TcpCommander()

        # 이동 버튼 클릭 시
        response = self.tcp.send_move_command("NODE-A1-001")

        # 작업 버튼 클릭 시
        response = self.tcp.send_task_command("PICK_AND_PLACE", count=5)

        # 팬 ON 버튼 클릭 시
        response = self.tcp.send_manual_command("FAN", "ON")
    """

    def __init__(self, server_ip: str = SERVER_IP, server_port: int = TCP_PORT):
        """
        Args:
            server_ip   : 서버 IP 주소
            server_port : 서버 TCP 포트
        """
        self.server_ip = server_ip
        self.server_port = server_port

    # ──────────── 공통: TCP 전송 & 응답 수신 ────────────
    def _send_and_receive(self, command: dict) -> dict | None:
        """
        JSON 명령을 서버에 TCP로 전송하고, 응답을 딕셔너리로 반환한다.

        Args:
            command : 전송할 명령 딕셔너리

        Returns:
            서버 응답 딕셔너리 또는 실패 시 None

        TODO (팀원 구현):
            - 연결 실패 시 재시도 로직
            - 타임아웃 설정 조정
            - 에러 시 GUI에 팝업 알림 등
        """
        try:
            # TCP 소켓 생성 및 서버 연결
            tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            tcp_socket.settimeout(5.0)  # 5초 타임아웃
            tcp_socket.connect((self.server_ip, self.server_port))

            # 명령 JSON 전송 (줄바꿈으로 메시지 구분 – 서버 프로토콜)
            json_str = json.dumps(command, ensure_ascii=False) + "\n"
            tcp_socket.sendall(json_str.encode("utf-8"))
            print(f"📤 [TcpCommander] 명령 전송: {json_str.strip()}")

            # 서버 응답 수신
            response_data = tcp_socket.recv(BUFFER_SIZE)
            response = json.loads(response_data.decode("utf-8"))
            print(f"📥 [TcpCommander] 응답 수신: {response}")

            tcp_socket.close()
            return response

        except socket.timeout:
            print("❌ [TcpCommander] 서버 응답 타임아웃")
            return None
        except ConnectionRefusedError:
            print("❌ [TcpCommander] 서버 연결 거부 – 서버가 실행 중인지 확인하세요")
            return None
        except Exception as e:
            print(f"❌ [TcpCommander] TCP 통신 오류: {e}")
            return None

    # ──────────── 이동 명령 전송 ────────────
    def send_move_command(self, target_node: str) -> dict | None:
        """
        로봇에게 특정 노드로 이동하라는 명령을 서버에 전송한다.

        전송 포맷:
            {"cmd": "MOVE", "target_node": "NODE-A1-001"}

        Args:
            target_node : 이동 목표 노드 ID

        Returns:
            서버 응답 딕셔너리
        """
        command = {
            "cmd": "MOVE",
            "target_node": target_node,
        }
        return self._send_and_receive(command)

    # ──────────── 작업 명령 전송 ────────────
    def send_task_command(self, action: str, count: int = 1) -> dict | None:
        """
        로봇에게 특정 작업(Pick-and-Place 등)을 수행하도록 명령한다.

        전송 포맷:
            {"cmd": "TASK", "action": "PICK_AND_PLACE", "count": 5}

        Args:
            action : 작업 종류 (예: "PICK_AND_PLACE")
            count  : 반복 횟수 (기본 1)

        Returns:
            서버 응답 딕셔너리
        """
        command = {
            "cmd": "TASK",
            "action": action,
            "count": count,
        }
        return self._send_and_receive(command)

    # ──────────── 수동 제어 명령 전송 ────────────
    def send_manual_command(self, device: str, state: str) -> dict | None:
        """
        특정 장치(팬, 히터, 가습기 등)를 수동으로 ON/OFF 제어한다.

        전송 포맷:
            {"cmd": "MANUAL", "device": "FAN", "state": "ON"}

        Args:
            device : 제어 대상 장치명 (예: "FAN", "HEATER", "HUMIDIFIER")
            state  : 제어 상태 ("ON" 또는 "OFF")

        Returns:
            서버 응답 딕셔너리
        """
        command = {
            "cmd": "MANUAL",
            "device": device,
            "state": state,
        }
        return self._send_and_receive(command)
