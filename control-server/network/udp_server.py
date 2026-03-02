"""
udp_server.py
=============
육묘장 ESP32 등에서 보내는 UDP 데이터를 수신하는 서버.
별도 스레드에서 UDP 패킷을 수신하고 MessageRouter에 전달한다.

역할:
    - UDP 소켓을 열고 센서 데이터 등을 수신
    - 수신된 JSON 데이터를 MessageRouter에 전달
    - GUI 앱에 데이터 포워딩 (향후 확장)
"""

import socket
import threading


class UdpServer:
    """
    UDP 수신 서버.

    사용 예:
        server = UdpServer(host="0.0.0.0", port=9000, message_router=router)
        server.start()
        ...
        server.stop()
    """

    BUFFER_SIZE = 4096

    def __init__(self, host: str, port: int, message_router):
        """
        Args:
            host           : 바인딩할 IP 주소
            port           : UDP 수신 포트
            message_router : MessageRouter 인스턴스
        """
        self.host = host
        self.port = port
        self.message_router = message_router

        self._socket: socket.socket | None = None
        self._running = False
        self._thread: threading.Thread | None = None

    # ──────────── 서버 시작 ────────────
    def start(self):
        """UDP 서버를 백그라운드 스레드에서 시작한다."""
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind((self.host, self.port))
        self._socket.settimeout(1.0)
        self._running = True

        self._thread = threading.Thread(
            target=self._receive_loop,
            name="UDP-Receiver",
            daemon=True,
        )
        self._thread.start()

        print(f"🟢 [UdpServer] UDP 서버 시작 → {self.host}:{self.port}")

    # ──────────── 서버 중지 ────────────
    def stop(self):
        """UDP 서버를 종료한다."""
        self._running = False
        if self._socket:
            self._socket.close()
        print("🔴 [UdpServer] UDP 서버 종료")

    # ──────────── 수신 루프 ────────────
    def _receive_loop(self):
        """UDP 패킷을 수신하고 MessageRouter에 전달한다."""
        while self._running:
            try:
                data, addr = self._socket.recvfrom(self.BUFFER_SIZE)
                raw_str = data.decode("utf-8", errors="replace").strip()

                if not raw_str:
                    continue

                print(f"📡 [UdpServer] {addr[0]}:{addr[1]} → {raw_str[:100]}...")

                # MessageRouter에 전달
                self.message_router.route_udp(raw_str)

            except socket.timeout:
                continue
            except OSError:
                break
            except Exception as e:
                print(f"❌ [UdpServer] 수신 오류: {e}")
