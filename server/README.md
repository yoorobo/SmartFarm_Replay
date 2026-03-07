# 스마트팜 운반차 제어 웹서버

SQLite 데이터베이스와 Flask 웹서버를 이용해 노트북에서 목적지를 입력하면 운반차(ESP32)가 수신해 이동하는 시스템입니다.

## 구조

```
[노트북]                                    [ESP32]
  웹 브라우저 ──→ Flask(5000) ──┬── TCP(8080) ←── 로봇
       │              │        │
       │         SQLite DB     └── UDP(7070) ←── ESP32-CAM (이미지)
       │              │
       └──────────────┴── 목적지 제어, 카메라 뷰
```

## 실행 방법

### 1. 가상환경 생성 및 의존성 설치

```bash
cd server
python3 -m venv venv
source venv/bin/activate   # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

### 2. 웹서버 실행

```bash
python app.py
```

- 웹 UI: http://127.0.0.1:5000 (또는 노트북 IP:5000)
- 로봇 TCP: 0.0.0.0:8080
- ESP32-CAM UDP: 0.0.0.0:7070

### 3. 로봇 펌웨어 설정

로봇이 **노트북 IP**로 연결해야 합니다. `robot-firmware/robot-firmware.ino`에서 `SERVER_IP`를 노트북 IP로 수정하세요.

```cpp
const char* SERVER_IP   = "192.168.0.xxx";  // 노트북 IP
const uint16_t SERVER_PORT = 8080;
```

### 4. ESP32-CAM 웹캠 설정 (선택)

ESP32-CAM으로 실시간 영상을 웹에서 보려면:

1. `esp32-cam/esp32-cam.ino`를 Arduino IDE에서 열기
2. `ssid`, `password`, `udpAddress` 수정:

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char* udpAddress = "192.168.0.xxx";  // 서버(노트북) IP - 로봇과 동일
const int udpPort = 7070;  // 서버 UDP 수신 포트
```

3. ESP32-CAM에 업로드 후 전원 연결
4. 웹 UI의 "ESP32-CAM 뷰" 섹션에서 실시간 영상 확인

### 5. 사용 순서

노트북 IP 확인:
- Linux: `ip addr` 또는 `hostname -I`
- Windows: `ipconfig`

1. 노트북에서 `python app.py` 실행
2. 로봇 전원 켜기 (같은 Wi-Fi에 연결)
3. 브라우저에서 http://노트북IP:5000 접속
4. 목적지(a01, s06 등) 선택 후 "목적지 전송" 클릭
5. 로봇이 해당 노드로 이동

## 데이터베이스 (SQLite)

- 파일: `server/robot_tasks.db`
- 테이블: `tasks` (목적지, 상태, 시간), `task_log` (이벤트 로그)

DB 직접 확인:

```bash
sqlite3 server/robot_tasks.db "SELECT * FROM tasks;"
```

## 목적지 노드

- a01~a04: 메인 라인
- s05~s07, s11~s13: 상부 노드
- r08~r10, r14~r16: 하부 노드
