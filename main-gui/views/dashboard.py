"""
dashboard.py
============
스마트팜 관제 대시보드 메인 화면.

구성:
    ┌────────────────────────────────────────────────────────┐
    │  🌱 통합 스마트팜 관제 시스템          [연결 상태]      │
    ├──────────────┬─────────────────┬───────────────────────┤
    │              │                 │                       │
    │  🌡️ 센서     │  🤖 AGV 상태    │  📋 시스템 로그       │
    │  모니터링    │                 │                       │
    │              │  배터리 [====]  │  [로그 메시지들...]    │
    │  섹션A: 24°C │  상태: IDLE     │                       │
    │  섹션B: 23°C │  위치: (0, 0)   │                       │
    │              │  작업: 없음     │                       │
    ├──────────────┴─────────────────┤                       │
    │  🎮 수동 제어                   │                       │
    │  [이동] [팬ON] [팬OFF] [비상정지]│                       │
    └────────────────────────────────┴───────────────────────┘
"""

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
    QGroupBox, QLabel, QPushButton, QTextEdit, QProgressBar,
    QComboBox, QTabWidget, QLineEdit, QFrame, QSizePolicy,
)
from PyQt6.QtCore import Qt, QTimer, pyqtSlot
from PyQt6.QtGui import QFont

from views.styles import DARK_THEME


class DashboardWindow(QMainWindow):
    """스마트팜 관제 대시보드 메인 윈도우."""

    def __init__(self, udp_receiver=None, tcp_commander=None):
        """
        Args:
            udp_receiver  : UdpReceiver 인스턴스 (백그라운드 데이터 수신, optional)
            tcp_commander : TcpCommander 인스턴스 (명령 전송, optional)
        """
        super().__init__()
        self.udp_receiver = udp_receiver
        self.tcp_commander = tcp_commander

        self.setWindowTitle("🌱 통합 스마트팜 관제 시스템")
        self.setMinimumSize(1200, 750)
        self.setStyleSheet(DARK_THEME)

        # ── 메인 위젯 ──
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setContentsMargins(15, 15, 15, 15)
        main_layout.setSpacing(10)

        # ── 헤더 ──
        main_layout.addWidget(self._create_header())

        # ── 본문 (3열 레이아웃) ──
        body_layout = QHBoxLayout()
        body_layout.setSpacing(10)

        # 좌측: 센서 모니터링
        body_layout.addWidget(self._create_sensor_panel(), stretch=3)

        # 중앙: AGV 상태 + 수동 제어
        center_layout = QVBoxLayout()
        center_layout.setSpacing(10)
        center_layout.addWidget(self._create_agv_panel(), stretch=3)
        center_layout.addWidget(self._create_control_panel(), stretch=2)
        body_layout.addLayout(center_layout, stretch=3)

        # 우측: 시스템 로그
        body_layout.addWidget(self._create_log_panel(), stretch=3)

        main_layout.addLayout(body_layout)

        # ── 시그널 연결 ──
        if self.udp_receiver:
            self.udp_receiver.sensor_received.connect(self._on_sensor_data)
            self.udp_receiver.robot_state_received.connect(self._on_agv_state)

        # ── 시계 업데이트 타이머 ──
        self._clock_timer = QTimer(self)
        self._clock_timer.timeout.connect(self._update_clock)
        self._clock_timer.start(1000)

        self._log("시스템 초기화 완료")

    # ============================================================
    #  헤더
    # ============================================================

    def _create_header(self) -> QWidget:
        """상단 헤더 바를 생성한다."""
        header = QFrame()
        header.setFixedHeight(60)
        header.setStyleSheet("""
            QFrame {
                background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                    stop:0 #0f3460, stop:0.5 #533483, stop:1 #e94560);
                border-radius: 10px;
            }
        """)

        layout = QHBoxLayout(header)
        layout.setContentsMargins(20, 0, 20, 0)

        # 타이틀
        title = QLabel("🌱 통합 스마트팜 관제 시스템")
        title.setFont(QFont("Malgun Gothic", 18, QFont.Weight.Bold))
        title.setStyleSheet("color: white; background: transparent;")
        layout.addWidget(title)

        layout.addStretch()

        # 시계
        self._clock_label = QLabel("--:--:--")
        self._clock_label.setFont(QFont("Consolas", 14))
        self._clock_label.setStyleSheet("color: #00d2ff; background: transparent;")
        layout.addWidget(self._clock_label)

        # 연결 상태
        self._conn_label = QLabel("● 연결 대기")
        self._conn_label.setStyleSheet("color: #ffab00; background: transparent; font-weight: bold;")
        layout.addWidget(self._conn_label)

        return header

    # ============================================================
    #  센서 모니터링 패널
    # ============================================================

    def _create_sensor_panel(self) -> QGroupBox:
        """육묘장 센서 모니터링 패널을 생성한다."""
        group = QGroupBox("🌡️ 육묘장 환경 모니터링")
        layout = QVBoxLayout(group)

        # 섹션별 센서 카드
        self._sensor_cards = {}
        sections = [
            ("섹션 A", "NODE-A1-001"),
            ("섹션 B", "NODE-A1-002"),
        ]

        for name, node_id in sections:
            card = self._create_sensor_card(name, node_id)
            layout.addWidget(card)

        layout.addStretch()
        return group

    def _create_sensor_card(self, name: str, node_id: str) -> QFrame:
        """센서 데이터 카드를 생성한다."""
        card = QFrame()
        card.setStyleSheet("""
            QFrame {
                background-color: #0a0a1a;
                border: 1px solid #0f3460;
                border-radius: 8px;
                padding: 10px;
            }
        """)
        card_layout = QGridLayout(card)
        card_layout.setSpacing(8)

        # 섹션 이름
        title = QLabel(f"📍 {name}")
        title.setFont(QFont("Malgun Gothic", 13, QFont.Weight.Bold))
        title.setStyleSheet("color: #e94560; border: none;")
        card_layout.addWidget(title, 0, 0, 1, 2)

        # 노드 ID
        node_label = QLabel(f"({node_id})")
        node_label.setStyleSheet("color: #666; font-size: 11px; border: none;")
        card_layout.addWidget(node_label, 0, 2)

        # 온도
        card_layout.addWidget(self._make_label("🌡️ 온도:", "border: none;"), 1, 0)
        temp_val = QLabel("--.- °C")
        temp_val.setStyleSheet("color: #ff6f00; font-size: 18px; font-weight: bold; border: none;")
        card_layout.addWidget(temp_val, 1, 1)

        # 습도
        card_layout.addWidget(self._make_label("💧 습도:", "border: none;"), 2, 0)
        hum_val = QLabel("--.- %")
        hum_val.setStyleSheet("color: #00b0ff; font-size: 18px; font-weight: bold; border: none;")
        card_layout.addWidget(hum_val, 2, 1)

        # 조도
        card_layout.addWidget(self._make_label("☀️ 조도:", "border: none;"), 3, 0)
        light_val = QLabel("-- lux")
        light_val.setStyleSheet("color: #ffeb3b; font-size: 18px; font-weight: bold; border: none;")
        card_layout.addWidget(light_val, 3, 1)

        # 상태 표시
        status = QLabel("● ONLINE")
        status.setStyleSheet("color: #00e676; font-weight: bold; border: none;")
        card_layout.addWidget(status, 1, 2)

        self._sensor_cards[node_id] = {
            "temp": temp_val,
            "hum": hum_val,
            "light": light_val,
            "status": status,
        }

        return card

    # ============================================================
    #  AGV 상태 패널
    # ============================================================

    def _create_agv_panel(self) -> QGroupBox:
        """AGV 상태 모니터링 패널을 생성한다."""
        group = QGroupBox("🤖 AGV 무인 운반차 상태")
        layout = QGridLayout(group)
        layout.setSpacing(10)

        # AGV ID
        layout.addWidget(self._make_label("🏷️ AGV ID:"), 0, 0)
        self._agv_id_label = QLabel("R01")
        self._agv_id_label.setStyleSheet("color: #00d2ff; font-size: 16px; font-weight: bold;")
        layout.addWidget(self._agv_id_label, 0, 1)

        # 상태
        layout.addWidget(self._make_label("📊 상태:"), 1, 0)
        self._agv_status_label = QLabel("IDLE")
        self._agv_status_label.setStyleSheet("color: #00e676; font-size: 16px; font-weight: bold;")
        layout.addWidget(self._agv_status_label, 1, 1)

        # 위치
        layout.addWidget(self._make_label("📍 위치:"), 2, 0)
        self._agv_pos_label = QLabel("(0, 0)")
        self._agv_pos_label.setStyleSheet("color: #e0e0e0; font-size: 14px;")
        layout.addWidget(self._agv_pos_label, 2, 1)

        # 현재 노드
        layout.addWidget(self._make_label("🗺️ 노드:"), 3, 0)
        self._agv_node_label = QLabel("--")
        self._agv_node_label.setStyleSheet("color: #e0e0e0; font-size: 14px;")
        layout.addWidget(self._agv_node_label, 3, 1)

        # 배터리
        layout.addWidget(self._make_label("🔋 배터리:"), 4, 0)
        self._battery_bar = QProgressBar()
        self._battery_bar.setValue(100)
        self._battery_bar.setFormat("%v%")
        layout.addWidget(self._battery_bar, 4, 1)

        # IR 센서
        layout.addWidget(self._make_label("📡 IR 센서:"), 5, 0)
        self._sensor_display = QLabel("⬜⬜⬜⬜⬜")
        self._sensor_display.setStyleSheet("font-size: 20px;")
        layout.addWidget(self._sensor_display, 5, 1)

        # RFID
        layout.addWidget(self._make_label("🏷️ RFID:"), 6, 0)
        self._rfid_label = QLabel("--")
        self._rfid_label.setStyleSheet("color: #ce93d8; font-size: 14px;")
        layout.addWidget(self._rfid_label, 6, 1)

        # 현재 작업
        layout.addWidget(self._make_label("📋 작업:"), 7, 0)
        self._task_label = QLabel("대기 중")
        self._task_label.setStyleSheet("color: #e0e0e0; font-size: 13px;")
        layout.addWidget(self._task_label, 7, 1)

        return group

    # ============================================================
    #  수동 제어 패널
    # ============================================================

    def _create_control_panel(self) -> QGroupBox:
        """수동 제어 패널을 생성한다."""
        group = QGroupBox("🎮 수동 제어")
        layout = QVBoxLayout(group)

        # 이동 명령
        move_layout = QHBoxLayout()
        move_layout.addWidget(QLabel("목적지:"))
        self._target_combo = QComboBox()
        self._target_combo.addItems([
            "NODE-IN-001 (입고장)",
            "NODE-OUT-001 (출고장)",
            "NODE-A1-001 (섹션A-1)",
            "NODE-A1-002 (섹션A-2)",
        ])
        move_layout.addWidget(self._target_combo, stretch=1)
        btn_move = QPushButton("🚗 이동")
        btn_move.setProperty("class", "success")
        btn_move.clicked.connect(self._on_move_clicked)
        move_layout.addWidget(btn_move)
        layout.addLayout(move_layout)

        # 장치 제어 버튼들
        device_layout = QHBoxLayout()

        btn_fan_on = QPushButton("🌀 팬 ON")
        btn_fan_on.clicked.connect(lambda: self._send_manual("FAN", "ON"))
        device_layout.addWidget(btn_fan_on)

        btn_fan_off = QPushButton("🌀 팬 OFF")
        btn_fan_off.clicked.connect(lambda: self._send_manual("FAN", "OFF"))
        device_layout.addWidget(btn_fan_off)

        btn_light_on = QPushButton("💡 조명 ON")
        btn_light_on.clicked.connect(lambda: self._send_manual("LED_GrowLight", "ON"))
        device_layout.addWidget(btn_light_on)

        btn_pump = QPushButton("💧 급수")
        btn_pump.clicked.connect(lambda: self._send_manual("Water_Pump", "ON"))
        device_layout.addWidget(btn_pump)

        layout.addLayout(device_layout)

        # 비상 정지
        btn_stop = QPushButton("🚨 비상 정지")
        btn_stop.setProperty("class", "danger")
        btn_stop.setMinimumHeight(40)
        btn_stop.clicked.connect(self._on_emergency_stop)
        layout.addWidget(btn_stop)

        return group

    # ============================================================
    #  시스템 로그 패널
    # ============================================================

    def _create_log_panel(self) -> QGroupBox:
        """시스템 로그 패널을 생성한다."""
        group = QGroupBox("📋 시스템 로그")
        layout = QVBoxLayout(group)

        self._log_text = QTextEdit()
        self._log_text.setReadOnly(True)
        layout.addWidget(self._log_text)

        # 로그 클리어 버튼
        btn_clear = QPushButton("🗑️ 로그 초기화")
        btn_clear.clicked.connect(self._log_text.clear)
        layout.addWidget(btn_clear)

        return group

    # ============================================================
    #  시그널 슬롯: 데이터 수신
    # ============================================================

    @pyqtSlot(dict)
    def _on_sensor_data(self, data: dict):
        """센서 데이터 수신 시 화면 업데이트."""
        node_id = data.get("node_id", "")
        card = self._sensor_cards.get(node_id)
        if not card:
            return

        temp = data.get("temp")
        hum = data.get("hum")
        light = data.get("light")

        if temp is not None:
            card["temp"].setText(f"{temp:.1f} °C")
        if hum is not None:
            card["hum"].setText(f"{hum:.1f} %")
        if light is not None:
            card["light"].setText(f"{light:.0f} lux")

        card["status"].setText("● ONLINE")
        card["status"].setStyleSheet("color: #00e676; font-weight: bold; border: none;")

    @pyqtSlot(dict)
    def _on_agv_state(self, data: dict):
        """AGV 상태 수신 시 화면 업데이트."""
        agv_id = data.get("robot_id", data.get("agv_id", ""))
        if agv_id:
            self._agv_id_label.setText(agv_id)

        # 상태
        state = data.get("state", data.get("status", ""))
        if isinstance(state, int):
            state_map = {0: "IDLE", 1: "FORWARD", 2: "SOFT_LEFT", 3: "SOFT_RIGHT",
                         6: "CROSS_DETECTED", 11: "ARRIVED", 12: "OUT_OF_LINE"}
            state = state_map.get(state, f"STATE_{state}")
        self._agv_status_label.setText(str(state))

        # 상태에 따른 색상
        color_map = {"IDLE": "#00e676", "MOVING": "#00b0ff", "FORWARD": "#00b0ff",
                      "WORKING": "#ff6f00", "ERROR": "#ff5252", "ARRIVED": "#00e676"}
        color = color_map.get(str(state), "#e0e0e0")
        self._agv_status_label.setStyleSheet(f"color: {color}; font-size: 16px; font-weight: bold;")

        # 위치
        pos_x = data.get("pos_x", 0)
        pos_y = data.get("pos_y", 0)
        self._agv_pos_label.setText(f"({pos_x}, {pos_y})")

        # 노드
        node = data.get("node", "")
        if node:
            self._agv_node_label.setText(node)

        # 배터리
        battery = data.get("battery", 0)
        self._battery_bar.setValue(battery)

        # IR 센서
        sensors = data.get("sensors", [])
        if sensors and len(sensors) >= 5:
            display = ""
            for s in sensors:
                display += "⬛" if s else "⬜"
            self._sensor_display.setText(display)

        # RFID
        plant_id = data.get("plant_id", "")
        if plant_id:
            self._rfid_label.setText(plant_id)

        # 연결 상태 업데이트
        self._conn_label.setText("● 연결됨")
        self._conn_label.setStyleSheet("color: #00e676; background: transparent; font-weight: bold;")

    # ============================================================
    #  버튼 이벤트
    # ============================================================

    def _on_move_clicked(self):
        """이동 버튼 클릭."""
        target = self._target_combo.currentText().split(" ")[0]
        self._log(f"🚗 이동 명령 전송: {target}")

        if self.tcp_commander:
            response = self.tcp_commander.send_move_command(target)
            if response:
                self._log(f"   응답: {response.get('msg', '')}")
            else:
                self._log("   ❌ 서버 응답 없음")

    def _send_manual(self, device: str, state: str):
        """수동 제어 명령 전송."""
        self._log(f"🔧 수동 제어: {device} → {state}")

        if self.tcp_commander:
            response = self.tcp_commander.send_manual_command(device, state)
            if response:
                self._log(f"   응답: {response.get('msg', '')}")

    def _on_emergency_stop(self):
        """비상 정지."""
        self._log("🚨 비상 정지 명령 전송!")
        if self.tcp_commander:
            self.tcp_commander.send_move_command("EMERGENCY_STOP")

    # ============================================================
    #  유틸리티
    # ============================================================

    def _make_label(self, text: str, style: str = "") -> QLabel:
        """스타일이 적용된 라벨을 생성한다."""
        label = QLabel(text)
        if style:
            label.setStyleSheet(style)
        return label

    def _log(self, message: str):
        """로그 메시지를 추가한다."""
        from datetime import datetime
        timestamp = datetime.now().strftime("%H:%M:%S")
        self._log_text.append(f"[{timestamp}] {message}")

    def _update_clock(self):
        """헤더 시계를 업데이트한다."""
        from datetime import datetime
        self._clock_label.setText(datetime.now().strftime("%H:%M:%S"))
