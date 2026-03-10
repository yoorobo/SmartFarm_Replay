# 🌱 통합 스마트팜 자동화 시스템

> IoT 프로젝트 4조 저장소 – 스마트팜

## 프로젝트 개요

육묘(모종 재배) 환경을 자동으로 제어하고, 무인 이송 로봇으로 작물을 운반하며, 관리자 대시보드를 통해 전체 시스템을 실시간으로 관제하는 **통합 스마트팜 자동화 시스템**입니다.

## 프로젝트 구조

```
iot-repo-4/
├── .env.example             # 환경 설정 템플릿 (Wi-Fi, DB, 포트)
│
├── control-server/          # 🖥️ Python 중앙 제어 서버 (Flask + TCP)
│   ├── main_server.py       # 서버 진입점 (Flask 5001, TCP 8000)
│   ├── requirements.txt     # Python 의존성
│   ├── templates/
│   │   └── index.html       # 웹 대시보드 UI
│   ├── database/            # DB 연결 및 ORM 모델
│   │   ├── db_config.py     # pymysql 연결 설정 (.env 연동)
│   │   ├── database.py      # SQLAlchemy 엔진/세션
│   │   ├── models.py        # ORM 모델 정의
│   │   └── schemas.py       # Pydantic 스키마
│   ├── core/                # 핵심 비즈니스 로직
│   │   ├── node_identifier.py   # ESP32 ID → 노드 매핑
│   │   ├── sensor_controller.py # 센서 데이터 처리 + 제어
│   │   └── init_db.py       # DB 마스터 데이터 초기화
│   ├── network/             # 네트워크 통신
│   │   ├── tcp_robot_server.py  # 로봇 TCP 서버
│   │   └── task_dispatcher.py   # 자동 배차 루프
│   └── domain/              # API 라우트 (Flask Blueprint)
│       ├── rfid_handler.py  # RFID 인식 처리
│       ├── robot_api.py     # 로봇 제어 API
│       └── environment_api.py   # 환경 설정 API
│
├── main-gui/                # 🖥️ PyQt6 관리자 관제 대시보드
│   └── monitor_app.py       # 실시간 센서 모니터링 GUI
│
├── server/                  # 🌐 Flask 웹서버 (운반차 제어, 레거시)
│   ├── app.py
│   ├── db.py
│   ├── run.sh
│   └── templates/
│
├── robot-firmware/          # 🤖 무인 이송 로봇 ESP32 펌웨어 (C++)
│   ├── robot-firmware.ino
│   └── src/
│       ├── comm/            # 네트워크 매니저
│       ├── config/          # 핀 설정
│       ├── line/            # 라인 팔로워
│       ├── motor/           # 모터 제어
│       ├── path/            # 경로 탐색
│       └── rfid/            # RFID 리더
│
├── esp32-cam/               # 📷 ESP32-CAM 웹캠 펌웨어
│   └── esp32-cam.ino
│
├── farm-firmware/           # 🌱 육묘 시스템 ESP32 펌웨어
│   ├── nursery-sensor/      # 센서 노드 (온습도, 조도)
│   │   └── nursery-sensor.ino
│   ├── nursery-bridge/      # Serial-TCP 브리지 (Arduino ↔ WiFi)
│   │   └── nursery-bridge.ino
│   └── nursery-cam/         # ESP32-CAM UDP 스트리밍
│       └── nursery-cam.ino
│
├── INTEGRATION_GUIDE.md     # 📋 시스템 통합 변경사항 가이드
├── PROTOCOL.md              # 📡 SFAM 장치간 Serial 패킷 프로토콜 명세서
└── README.md
```

## 기술 스택

| 모듈 | 언어 / 프레임워크 | 역할 |
|------|-------------------|------|
| control-server | Python, Flask, pymysql, SQLAlchemy | 중앙 제어 서버, AWS MySQL DB 연동 |
| main-gui | Python, PyQt6, pyqtgraph | 관리자 관제 대시보드 |
| robot-firmware | C++ (ESP32) | 무인 이송 로봇 펌웨어 |
| farm-firmware | C++ (ESP32, ESP32-CAM) | 육묘 환경 제어 + 영상 펌웨어 |
| server | Python, Flask | 운반차 웹 제어 (레거시) |

## 환경 설정

```bash
# 최초: .env.example을 .env로 복사 후 편집
cp .env.example .env

# .env 수정 예시:
WIFI_SSID=YOUR_WIFI_SSID
WIFI_PASSWORD=YOUR_WIFI_PASSWORD
SERVER_IP=192.168.0.xxx
DB_HOST=3.35.24.94
DB_USER=root
DB_PASSWORD=YOUR_DB_PASSWORD
DB_NAME=sfam_db
FLASK_PORT=5001
TCP_PORT=8000
```

## 서버 실행 방법

### 중앙 제어 서버 (`control-server/`)

```bash
cd control-server
pip install -r requirements.txt
python main_server.py
```

- **웹 대시보드**: http://서버IP:5001
- **로봇 TCP**: 0.0.0.0:8000
- **자동 배차**: 3초 주기 polling

### 모니터링 앱 (`main-gui/`)

```bash
cd main-gui
pip install PyQt6 pyqtgraph requests python-dotenv
python monitor_app.py         # 기본: s11
python monitor_app.py s12     # s12 노드 모니터링
```

### 운반차 웹서버 (`server/`, 레거시)

```bash
cd server
./run.sh
```
