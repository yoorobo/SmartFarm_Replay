"""
styles.py
=========
스마트팜 대시보드 다크 테마 스타일시트.
"""

DARK_THEME = """
/* ── 전역 기본 ── */
QMainWindow, QWidget {
    background-color: #1a1a2e;
    color: #e0e0e0;
    font-family: 'Segoe UI', 'Malgun Gothic', sans-serif;
    font-size: 13px;
}

/* ── 그룹박스 (패널) ── */
QGroupBox {
    background-color: #16213e;
    border: 1px solid #0f3460;
    border-radius: 10px;
    margin-top: 12px;
    padding: 15px 10px 10px 10px;
    font-weight: bold;
    font-size: 14px;
    color: #e94560;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 15px;
    padding: 0 8px;
}

/* ── 라벨 ── */
QLabel {
    color: #e0e0e0;
    font-size: 13px;
}
QLabel[class="value"] {
    color: #00d2ff;
    font-size: 18px;
    font-weight: bold;
}
QLabel[class="title"] {
    color: #e94560;
    font-size: 16px;
    font-weight: bold;
}
QLabel[class="status-online"] {
    color: #00e676;
    font-weight: bold;
}
QLabel[class="status-offline"] {
    color: #ff5252;
    font-weight: bold;
}

/* ── 버튼 ── */
QPushButton {
    background-color: #0f3460;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 6px;
    padding: 8px 18px;
    font-size: 13px;
    font-weight: bold;
    min-height: 30px;
}
QPushButton:hover {
    background-color: #533483;
    border-color: #e94560;
}
QPushButton:pressed {
    background-color: #e94560;
}
QPushButton:disabled {
    background-color: #2a2a3e;
    color: #555;
}
QPushButton[class="danger"] {
    background-color: #c62828;
    border-color: #e53935;
}
QPushButton[class="danger"]:hover {
    background-color: #e53935;
}
QPushButton[class="success"] {
    background-color: #2e7d32;
    border-color: #43a047;
}
QPushButton[class="success"]:hover {
    background-color: #43a047;
}

/* ── 텍스트 에디트 (로그) ── */
QTextEdit, QPlainTextEdit {
    background-color: #0a0a1a;
    color: #00e676;
    border: 1px solid #0f3460;
    border-radius: 6px;
    padding: 8px;
    font-family: 'Consolas', 'D2Coding', monospace;
    font-size: 12px;
}

/* ── 콤보박스 ── */
QComboBox {
    background-color: #0f3460;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 6px;
    padding: 6px 12px;
    min-height: 28px;
}
QComboBox::drop-down {
    border: none;
}
QComboBox QAbstractItemView {
    background-color: #16213e;
    color: #e0e0e0;
    selection-background-color: #533483;
}

/* ── 프로그레스 바 (배터리) ── */
QProgressBar {
    background-color: #0a0a1a;
    border: 1px solid #0f3460;
    border-radius: 6px;
    text-align: center;
    color: #e0e0e0;
    font-weight: bold;
    min-height: 22px;
}
QProgressBar::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
        stop:0 #e94560, stop:0.5 #ff6f00, stop:1 #00e676);
    border-radius: 5px;
}

/* ── 스핀박스 ── */
QSpinBox, QDoubleSpinBox {
    background-color: #0f3460;
    color: #e0e0e0;
    border: 1px solid #533483;
    border-radius: 6px;
    padding: 4px 8px;
}

/* ── 탭 위젯 ── */
QTabWidget::pane {
    border: 1px solid #0f3460;
    border-radius: 6px;
    background-color: #16213e;
}
QTabBar::tab {
    background-color: #0f3460;
    color: #e0e0e0;
    padding: 8px 20px;
    margin-right: 2px;
    border-top-left-radius: 6px;
    border-top-right-radius: 6px;
}
QTabBar::tab:selected {
    background-color: #e94560;
    color: white;
}
QTabBar::tab:hover:!selected {
    background-color: #533483;
}

/* ── 스크롤바 ── */
QScrollBar:vertical {
    background-color: #0a0a1a;
    width: 10px;
    border-radius: 5px;
}
QScrollBar::handle:vertical {
    background-color: #533483;
    border-radius: 5px;
    min-height: 30px;
}
"""
