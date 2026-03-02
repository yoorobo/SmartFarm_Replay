"""
main_server.py
==============
통합 스마트팜 자동화 시스템 – 중앙 제어 서버 진입점(Entry Point).

실행:
    python main_server.py

기능:
    1. SystemController를 통해 모든 컴포넌트 초기화
    2. DB 연결 + 초기 데이터 로드
    3. TCP 소켓 서버 시작 (ESP32/GUI 연결 대기)
    4. UDP 소켓 서버 시작 (센서 데이터 수신)
    5. Ctrl+C로 안전 종료
"""

import signal
from core.system_controller import SystemController


def main():
    """
    메인 함수:
    SystemController를 시작하고, Ctrl+C 시그널이 올 때까지 대기한다.
    """

    # ── SystemController 생성 ──
    controller = SystemController(
        tcp_port=8080,  # ESP32/GUI가 TCP로 연결할 포트
        udp_port=9000,  # 육묘장 센서 UDP 수신 포트
    )

    # ── 시스템 시작 ──
    if not controller.start():
        print("🚫 시스템 시작 실패. 종료합니다.")
        return

    # ── Ctrl+C 시그널 핸들러 등록 ──
    def signal_handler(sig, frame):
        print("\n\n🛑 Ctrl+C 감지 – 서버를 종료합니다...")
        controller.stop()
        exit(0)

    signal.signal(signal.SIGINT, signal_handler)

    # ── 서버 대기 (메인 스레드 유지) ──
    print("\n💡 서버가 실행 중입니다. 종료하려면 Ctrl+C를 누르세요.\n")

    try:
        signal.pause()  # Unix: 시그널 대기
    except AttributeError:
        # Windows에서는 signal.pause()가 없으므로 대체
        import time
        while True:
            time.sleep(1)


if __name__ == "__main__":
    main()
