"""
app.py
======
스마트팜 관제 앱 진입점.

실행:
    cd main-gui
    python app.py

옵션:
    --server-ip   : 서버 IP 주소 (기본: 127.0.0.1)
    --udp-port    : 카메라 수신 UDP 포트 (기본: 7070)
    --tcp-port    : 서버 TCP 포트 (기본: 8000)
    --host        : 서버 IP (기본: 0.0.0.0)
    --demo        : 데모 모드 (서버 없이 더미 데이터로 실행)
"""

import sys
import argparse
from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import QTimer
from views.dashboard import DashboardWindow
from network.gui_network_client import UdpReceiver, TcpCommander


def run_demo(window: DashboardWindow):
    """데모 모드: 더미 데이터를 주기적으로 발생시킨다."""
    import random

    demo_tick = [0]

    def generate_dummy():
        demo_tick[0] += 1

        # 센서 데이터 (2초마다)
        if demo_tick[0] % 2 == 0:
            for node_id in ["NODE-A1-001", "NODE-A1-002"]:
                window._on_sensor_data({
                    "node_id": node_id,
                    "temp": 22.0 + random.uniform(-2, 4),
                    "hum": 60.0 + random.uniform(-5, 10),
                    "light": 300 + random.uniform(-50, 100),
                })

        # AGV 상태 (1초마다)
        states = [0, 1, 1, 1, 2, 2, 3, 3, 6, 1, 1, 11]
        state_idx = demo_tick[0] % len(states)
        sensors = [random.choice([0, 1]) for _ in range(5)]
        sensors[2] = 1  # 중앙 센서는 대부분 라인 위

        window._on_agv_state({
            "robot_id": "AGV-01",
            "pos_x": demo_tick[0] * 10 % 500,
            "pos_y": 200 + (demo_tick[0] % 3) * 50,
            "battery": max(20, 100 - demo_tick[0] % 80),
            "state": states[state_idx],
            "node": f"A{(demo_tick[0] // 5) % 4 + 1}",
            "sensors": sensors,
            "plant_id": "RFID-A1B2C3" if demo_tick[0] % 10 == 0 else "",
        })

    timer = QTimer()
    timer.timeout.connect(generate_dummy)
    timer.start(1000)
    return timer  # 참조 유지 필수


def main():
    parser = argparse.ArgumentParser(description="스마트팜 관제 앱")
    parser.add_argument("--server-ip", default="127.0.0.1",    # 2. 네트워크 및 포트 설정 옵션
    parser.add_argument("--udp-port", type=int, default=7070, help="UDP 포트")
    parser.add_argument("--tcp-port", type=int, default=8000, help="서버 TCP 포트")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="서버 IP")
    parser.add_argument("--demo", action="store_true", help="데모 모드")
    args = parser.parse_args()

    app = QApplication(sys.argv)

    # ── 네트워크 객체 생성 ──
    udp_receiver = None
    tcp_commander = None

    if not args.demo:
        udp_receiver = UdpReceiver(port=args.udp_port)
        tcp_commander = TcpCommander(
            server_ip=args.server_ip,
            server_port=args.tcp_port,
        )

    # ── 대시보드 생성 ──
    window = DashboardWindow(
        udp_receiver=udp_receiver,
        tcp_commander=tcp_commander,
    )
    window.show()

    # ── 네트워크 시작 ──
    if udp_receiver:
        udp_receiver.start()
        window._log(f"📡 UDP 수신 시작 (포트: {args.udp_port})")
        window._log(f"🔗 TCP 서버: {args.server_ip}:{args.tcp_port}")
    else:
        window._log("🎮 데모 모드로 실행 중 (서버 연결 없음)")
        demo_timer = run_demo(window)

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
