# 🌱 스마트팜 통합 자동화 시스템 (Smart Farm Automation System)

> IoT 프로젝트 4조 저장소 – 스마트팜 통합 제어 시스템

## 📌 프로젝트 개요

육묘(모종 재배) 환경을 자동으로 모니터링 및 제어하고, 무인 이송 로봇(AGV)을 통해 작물을 운반하며, 관리자 대시보드(Web)를 통해 전체 시스템을 실시간으로 관제하는 **통합 스마트팜 자동화 및 물류 시스템**입니다.

이번 통합 과정(`system_integration`)을 통해 분산되어 있던 서버와 GUI 레거시 코드를 제거하고, **단일 통합 제어 서버(Control Server)** 와 모던 웹 대시보드로 시스템 구조를 근본적으로 개편하였습니다.

## 🌟 주요 특징 및 변경 사항

- **🚀 단일 통합 서버 아키텍처**: 기존의 분산된 웹 서버와 관제 프로그램을 하나의 Flask 기반 `control-server`로 통합.
- **🖥️ 모던 웹 대시보드 (SPA)**: HTML/CSS(Glassmorphism, Dark Theme)/JS 컴포넌트 기반 웹 대시보드 제공. (PyQt GUI 대체)
- **⚡ SFAM Binary Protocol**: 기기-서버 간 통신을 무거운 JSON에서 자체 정의한 가벼운 **바이너리 프로토콜(SFAM)** 로 전면 교체하여 속도와 안정성 확보.
- **📡 다중 통신 인터페이스 통합**: HTTP(웹 API), TCP(로봇 프로토콜), UDP(카메라 스트리밍) 통신을 한 서버에서 비동기적으로 처리.
- **📦 RFID 스마트 물류 시나리오 자동화**: RFID 태그를 통한 모종 입고부터 AGV(로봇) 배차 및 상태 업데이트까지 일련의 과정을 자동화.

## 📂 프로젝트 구조

```text
iot-repo-4/
├── control-server/          # 🖥️ Python 통합 중앙 제어 서버
│   ├── main_server.py       # 서버 진입점 (Flask 5001, TCP 8000, UDP 7070)
│   ├── requirements.txt     # Python 파이썬 의존성 패키지
│   ├── static/              # 웹 프론트엔드 에셋 (CSS, JS, 폰트, 이미지)
│   ├── templates/           # Flask HTML 템플릿 (index.html, login.html)
│   ├── core/                # 핵심 비즈니스 로직 (배차 로직, 센서 제어, DB 초기화 등)
│   ├── database/            # MySQL DB 연결 및 쿼리 관리 (pymysql 기반)
│   ├── domain/              # 분야별 API 라우터 (Blueprint: Auth, Env, RFID, Robot 등)
│   ├── network/             # 통신 계층 (TCP 로봇 서버, UDP 카메라 서버, SFAM 프로토콜 명세)
│   └── tests/               # 디바이스 가상 시뮬레이터 (RFID 테스트 등)
│
├── farm-firmware/           # 🌱 스마트 육묘장 ESP32/Arduino 펌웨어
│   ├── nursery-sensor/      # 온습도/조도 모니터링 노드
│   ├── nursery-bridge/      # Serial-TCP 브리지 (Arduino ↔ 와이파이통신)
│   └── nursery-cam/         # 육묘장 카메라 노드
│
├── robot-firmware/          # 🤖 무인 이송 로봇(AGV) 펌웨어 (C++)
│   └── (모터, 라인트레이싱, RFID 리더 및 SFAM 프로토콜 통신 로직)
│
├── esp32-cam/               # 📷 일반 ESP32-CAM 웹캠 펌웨어 (UDP 스트리밍)
│
├── INTEGRATION_GUIDE.md     # 📋 시스템 구조 및 기능 설명 문서
└── PROTOCOL.md              # 📡 SFAM 바이너리 통신 프로토콜 명세서
```

## 🛠️ 기술 스택

| 모듈 / 컴포넌트 | 언어 / 프레임워크 | 역할 |
|---|---|---|
| **Control Server** | Python, Flask, pymysql | 중앙 제어 서버, REST API 제공 및 백그라운드 태스크 |
| **Database** | MySQL (AWS/Cloud) | 센서 로그, 사용자, 시스템 상태 데이터 저장 |
| **Web Dashboard** | HTML5, Vanilla JS, CSS3 | 관리자 실시간 관제 대시보드 (SPA) |
| **Network Protocol** | SFAM (자체제작 Binary) | 기기간(로봇, 육묘장 ↔ 서버) 고속 및 저용량 통신 |
| **AGV Firmware** | C++ (Arduino/ESP32) | 무인 로봇 주행, 위치, RFID 스캔 제어 |
| **Farm Firmware** | C++ (Arduino/ESP32) | 육묘장 센서 측정 및 카메라 스트리밍 제어 |

### 1. 통합 제어 서버 접속 및 실행 (`control-server`)

현재 팀 공용 **AWS 클라우드 DB** 접속 정보가 코드(`db_config.py`)에 기본으로 세팅되어 있습니다. 따라서 별도의 DB 설치나 `.env` 환경변수 세팅 없이, 아래 명령어만으로 바로 서버를 실행할 수 있습니다.

```bash
# 1. 제어 서버 폴더로 이동
cd control-server

# 2. 필수 라이브러리 설치
pip install -r requirements.txt

# 3. 서버 실행
python main_server.py
```

서버가 실행되면 아래 포트로 서비스가 열립니다:
- **웹 대시보드 (HTTP)**: `http://localhost:5001` (또는 지정된 서버 IP)
- **AGV TCP 통신 포트**: `8000`
- **카메라 UDP 스트리밍 포트**: `7070`

### 2. 시뮬레이션 테스트
하드웨어 없이 소프트웨어 로직(RFID 입고 ~ 로봇 이동완료)을 테스트하려면 별도 터미널에서 다음 스크립트를 실행합니다.

```bash
cd control-server
python network/test_rfid_scenario.py
```

---

*본 프로젝트 저장소는 기존 레거시 코드(`server`, `main-gui`)를 제거하고, `system_integration` 브랜치를 기준으로 최적화 통합되었습니다.*
