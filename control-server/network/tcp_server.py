"""
tcp_server.py
=============
ESP32 디바이스 및 GUI 앱의 TCP 연결을 관리하는 소켓 서버.
threading 기반으로 각 클라이언트를 별도 스레드에서 처리한다.

역할:
    - TCP 소켓 서버를 열고 ESP32/GUI 연결 대기
    - 연결된 클라이언트별로 전담 스레드 생성
    - 수신된 JSON 데이터를 MessageRouter에 전달
    - 응답을 클라이언트에 TCP로 반환
    - 연결된 클라이언트 목록 관리 (AGV에 명령 전송 시 사용)
"""

import socket
import threading
import json


class TcpServer:
    """
    멀티 클라이언트 TCP 소켓 서버.

    사용 예:
        server = TcpServer(host="0.0.0.0", port=8080, message_router=router)
        server.start()  # 백그라운드 스레드에서 시작
        ...
        server.stop()
    """

    BUFFER_SIZE = 4096

    def __init__(self, host: str, port: int, message_router):
        """
        Args:
            host           : 바인딩할 IP 주소 (0.0.0.0 = 모든 인터페이스)
            port           : TCP 포트 번호
            message_router : MessageRouter 인스턴스
        """
        self.host = host
        self.port = port
        self.message_router = message_router

        self._server_socket: socket.socket | None = None
        self._running = False
        self._accept_thread: threading.Thread | None = None

        # 연결된 클라이언트 관리 {client_addr: socket}
        self._clients: dict[str, socket.socket] = {}
        self._clients_lock = threading.Lock()

        # 로봇 클라이언트 추적 (ROBOT_STATE를 보낸 클라이언트)
        self._robot_clients: set[str] = set()

    # ──────────── 서버 시작 ────────────
    def start(self):
        """TCP 서버를 백그라운드 스레드에서 시작한다."""
        self._server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._server_socket.bind((self.host, self.port))
        self._server_socket.listen(5)
        self._server_socket.settimeout(1.0)  # accept 타임아웃 (stop 감지용)
        self._running = True

        self._accept_thread = threading.Thread(
            target=self._accept_loop,
            name="TCP-Accept",
            daemon=True,
        )
        self._accept_thread.start()

        print(f"🟢 [TcpServer] TCP 서버 시작 → {self.host}:{self.port}")

    # ──────────── 서버 중지 ────────────
    def stop(self):
        """TCP 서버를 안전하게 종료한다."""
        self._running = False

        # 모든 클라이언트 연결 해제
        with self._clients_lock:
            for addr, client_sock in self._clients.items():
                try:
                    client_sock.close()
                except Exception:
                    pass
            self._clients.clear()

        # 서버 소켓 닫기
        if self._server_socket:
            self._server_socket.close()

        print("🔴 [TcpServer] TCP 서버 종료")

    # ──────────── 연결 수락 루프 ────────────
    def _accept_loop(self):
        """클라이언트 연결을 수락하고 전담 스레드를 생성한다."""
        while self._running:
            try:
                client_sock, addr = self._server_socket.accept()
                addr_str = f"{addr[0]}:{addr[1]}"
                print(f"🔗 [TcpServer] 새 연결: {addr_str}")

                # 클라이언트 등록
                with self._clients_lock:
                    self._clients[addr_str] = client_sock

                # 전담 스레드 생성
                client_thread = threading.Thread(
                    target=self._handle_client,
                    args=(client_sock, addr_str),
                    name=f"TCP-Client-{addr_str}",
                    daemon=True,
                )
                client_thread.start()

            except socket.timeout:
                continue
            except OSError:
                break

    # ──────────── 클라이언트 처리 ────────────
    def _handle_client(self, client_sock: socket.socket, addr: str):
        """
        개별 클라이언트의 데이터를 수신하고 MessageRouter에 전달한다.
        연결이 끊기면 자동으로 클라이언트를 해제한다.
        """
        client_sock.settimeout(None)  # 블로킹 모드

        try:
            while self._running:
                # 줄바꿈(\n) 단위로 수신 (ESP32는 println으로 전송)
                data = self._recv_line(client_sock)
                if not data:
                    break  # 연결 종료

                raw_str = data.strip()
                if not raw_str:
                    continue

                print(f"📨 [TcpServer] {addr} → {raw_str[:100]}...")

                # ROBOT_STATE를 보내면 로봇으로 등록
                try:
                    msg = json.loads(raw_str)
                    msg_type = msg.get("type", "")
                    if msg_type in ("ROBOT_STATE", "AGV_STATE"):
                        if addr not in self._robot_clients:
                            self._robot_clients.add(addr)
                            print(f"🤖 [TcpServer] 로봇 클라이언트 등록: {addr}")
                except json.JSONDecodeError:
                    pass

                # MessageRouter에 전달하고 응답 받기
                response = self.message_router.route_tcp(raw_str)

                # 응답을 클라이언트에 전송
                if response:
                    response_json = json.dumps(response, ensure_ascii=False) + "\n"
                    client_sock.sendall(response_json.encode("utf-8"))

        except ConnectionResetError:
            print(f"⚠️ [TcpServer] {addr} 연결 리셋됨")
        except Exception as e:
            print(f"❌ [TcpServer] {addr} 오류: {e}")
        finally:
            # 클라이언트 해제
            with self._clients_lock:
                self._clients.pop(addr, None)
            self._robot_clients.discard(addr)
            client_sock.close()
            print(f"🔌 [TcpServer] {addr} 연결 해제")

    # ──────────── 줄 단위 수신 ────────────
    def _recv_line(self, sock: socket.socket) -> str | None:
        """소켓에서 줄바꿈(\\n)까지 데이터를 수신한다."""
        buffer = b""
        while True:
            try:
                chunk = sock.recv(1)
                if not chunk:
                    return None  # 연결 종료
                if chunk == b"\n":
                    return buffer.decode("utf-8", errors="replace")
                buffer += chunk
            except Exception:
                return None

    # ──────────── 특정 클라이언트에 명령 전송 ────────────
    def send_to_client(self, addr: str, command: dict) -> bool:
        """
        특정 클라이언트(ESP32)에 TCP 명령을 전송한다.

        Args:
            addr    : 클라이언트 주소 ("IP:PORT")
            command : 전송할 JSON 명령 딕셔너리

        Returns:
            전송 성공 여부
        """
        with self._clients_lock:
            client_sock = self._clients.get(addr)

        if client_sock is None:
            print(f"⚠️ [TcpServer] 클라이언트 {addr}를 찾을 수 없습니다.")
            return False

        try:
            json_str = json.dumps(command, ensure_ascii=False) + "\n"
            client_sock.sendall(json_str.encode("utf-8"))
            print(f"📤 [TcpServer] {addr} ← {json_str.strip()}")
            return True
        except Exception as e:
            print(f"❌ [TcpServer] {addr} 전송 실패: {e}")
            return False

    # ──────────── 모든 클라이언트에 브로드캐스트 ────────────
    def broadcast(self, command: dict):
        """연결된 모든 클라이언트에 명령을 브로드캐스트한다."""
        with self._clients_lock:
            addrs = list(self._clients.keys())
        for addr in addrs:
            self.send_to_client(addr, command)

    # ──────────── 연결 상태 확인 ────────────
    @property
    def connected_clients(self) -> list[str]:
        """현재 연결된 클라이언트 주소 목록을 반환한다."""
        with self._clients_lock:
            return list(self._clients.keys())

    # ──────────── 로봇 클라이언트에 명령 포워딩 ────────────
    def send_to_robots(self, command: dict):
        """
        등록된 모든 로봇 클라이언트에 명령을 전달한다.
        ROBOT_STATE를 한 번이라도 보낸 클라이언트가 로봇으로 분류된다.
        """
        robot_addrs = list(self._robot_clients)
        if not robot_addrs:
            print("⚠️ [TcpServer] 연결된 로봇 클라이언트가 없습니다.")
            return

        for addr in robot_addrs:
            self.send_to_client(addr, command)
