# SFAM: Smart Farm Integrated Management System

> **"농업 현장의 불확실성을 기술적 신뢰성으로 극복하는 지능형 모종 관리 솔루션"**

유정학 (Junghak Yoo) · github.com/yoorobo · taekchun.utaek@gmail.com

---

## 핵심 강점 — 현장에서 검증된 설계 철학

### 현장 언어로 설계된 시스템

조경기능사(2024)와 버섯종균기능사(2024) 취득 과정에서 식물의 생육 임계값과 실제 농장 환경의 변수에 대한 이해가 제어 로직설계에 도움이 되었습니다.

- `farm_env_manager.py`의 목표 온도(20 ~ 28°C)·목표 습도(50 ~ 70%) 기본값은 육묘장 환경에서 검증된 생육 임계값에서 도출됐습니다.
- DB 스키마(`seedling_varieties`)에는 품종별 `opt_temp_day`, `opt_temp_night`, `opt_humidity`, `opt_ec`, `opt_ph`, `opt_light_dli`가 설계되어, RFID 스캔 시 해당 구역 노드의 제어 파라미터가 **품종에 맞게 자동 전환**됩니다.
- 밀폐 육묘장의 Wi-Fi 노이즈와 수분·전자기 간섭을 고려한 것이 Binary Protocol 선택의 실질적 배경입니다.

### 데이터 무결성 철학

JSON 기반 통신에서 Custom Binary Protocol로의 전환은 기술 유행이 아니라 현장 신뢰성 요구에서 나온 결정입니다.

![HW Architecture](./assets/HW_architecture.png)
| 비교 항목 | JSON 방식 | SFAM Binary v1.0 |
|---|---|---|
| 데이터 크기 | ~120 bytes | 최대 74 bytes (**38% 절감**) |
| 패킷 경계 감지 | 별도 구분자 로직 필요 | **0xAA SOF 즉시 탐지** |
| 무결성 검증 | 없음 (노이즈 취약) | **CRC16-CCITT 내장** |
| 패킷 누락 감지 | 불가능 | **SEQ 번호로 즉시 감지** |
| 에러 응답 | `"error": true`로 끝남 | **MSG_NAK + reason 6종** |

---

## 시스템 아키텍처
![SW Architecture](./assets/SW_architecture.png)


### 레이어별 구성

```
SmartFarm_Replay/
├── control-server/
│ ├── network/
│ │ ├── sfam_protocol.py ← SFAM v1.0 Python 파서 (5단계 상태머신)
│ │ ├── tcp_robot_server.py ← AGV TCP 연결 풀 관리
│ │ ├── task_dispatcher.py ← 자동 배차 루프 (3초 폴링)
│ │ └── message_router.py ← 패킷 → 도메인 라우팅
│ ├── domain/
│ │ ├── farm_env_manager.py ← 온습도 피드백 제어 (품종별 임계값)
│ │ ├── rfid_handler.py ← RFID 입출고 자동 파이프라인
│ │ └── transport_task.py ← 운송 작업 큐
│ ├── database/
│ │ ├── farm_repository.py ← 노드·환경·품종 데이터
│ │ └── agv_repository.py ← AGV 상태 및 텔레메트리
│ └── core/
│ └── system_controller.py ← 최상위 컨트롤러 (DI 패턴)
│
├── robot-firmware/src/
│ ├── comm/SFAM_Protocol.h ← C 프로토콜 헤더 (Python과 동일 로직)
│ ├── line/LineFollower.cpp ← 5센서 라인트레이싱 + 경로 추종
│ ├── motor/MotorController.cpp ← L298N PWM 차등 제어
│ ├── path/PathFinder.cpp ← BFS 경로탐색 (16노드 방향 그래프)
│ └── rfid/RFIDReader.cpp ← NFC UID 인식
│
├── farm-firmware/
│ └── nursery-bridge/sfam_packet.h ← 공용 패킷 헤더 (C·Python 공유)
│
├── PROTOCOL.md ← SFAM v1.0 공식 명세서
└── docs/DB_SCHEMA.md ← MySQL smart_farm_v2 전체 스키마
```

---

## 핵심 기술 구현 상세

### 1. Reliable Communication — SFAM v1.0 Binary Protocol

#### 패킷 구조

```
┌──────┬──────────┬────────┬────────┬─────┬─────┬──────────────┬────────┐
│ SOF │ MSG_TYPE │ SRC_ID │ DST_ID │ SEQ │ LEN │ PAYLOAD │ CRC16 │
│ 1B │ 1B │ 1B │ 1B │ 1B │ 1B │ 0 ~ 64B │ 2B │
│ 0xAA │ 0x01~FF │ 0x00=S │ 0xFF=B │ 롤오버│ │ │ Big-En │
└──────┴──────────┴────────┴────────┴─────┴─────┴──────────────┴────────┘
최대 74 bytes · Big-Endian · C와 Python 동일 알고리즘 필수
```

#### CRC16-CCITT (Python / C 동일 구현)

```python
# sfam_protocol.py
def calc_crc16(data: bytes) -> int:
"""Poly: 0x1021, Init: 0xFFFF, 반사 없음 — C와 동일 결과 필수"""
crc = 0xFFFF
for b in data:
crc ^= (b << 8)
for _ in range(8):
crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
return crc
```

```c
// sfam_packet.h (Arduino/ESP32) — 위 Python과 bit-exact 동일
static inline uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
uint16_t crc = 0xFFFF;
for (uint16_t i = 0; i < len; i++) {
crc ^= (uint16_t)data[i] << 8;
for (uint8_t j = 0; j < 8; j++)
crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
}
return crc;
}
```

#### 5단계 상태머신 스트림 파서 (SfamParser)

```
WAIT_SOF ──(0xAA)──► HEADER ──(6B)──► PAYLOAD ──(N bytes)──► CRC_HI ──► CRC_LO
▲ │ │
│ └──(payLen==0)──────────────────────────────────► │
└──────────────────────────────── reset() ◄──────────── CRC 검증 완료 ───┘
```

```python
# sfam_protocol.py — CRC 불일치든 정상이든 reset(), 시스템은 계속 동작
elif self.state == 'CRC_LO':
self.buf.append(b)
data_to_crc = self.buf[:PKT_HDR_SIZE + self.payLen]
calCrc = calc_crc16(data_to_crc)
rxCrc = (self.buf[-2] << 8) | self.buf[-1]

res = None
if rxCrc == calCrc: # 정상 패킷만 반환
res = {
'msg_type': self.buf[1], 'src_id': self.buf[2],
'dst_id': self.buf[3], 'seq': self.buf[4],
'len': self.buf[5], 'payload': bytes(self.buf[6:6+self.payLen])
}
self.reset() # 깨진 데이터로 동작하는 것보다 안 읽는 것이 안전하다
return res
```

#### MSG_TYPE 코드표

| 코드 | 메시지 | 방향 | 크기 |
|---|---|---|---|
| `0x01` | HEARTBEAT_REQ | 양방향 | 없음 |
| `0x10` | AGV_TELEMETRY | AGV→Server | 8B |
| `0x11` | AGV_TASK_CMD | Server→AGV | 10B |
| `0x13` | AGV_STATUS_RPT | AGV→Server | 5B |
| `0x14` | AGV_EMERGENCY | 양방향 | 2B |
| `0x20` | SENSOR_BATCH | 육묘→Server | 가변 (최대 10센서, int24 BE) |
| `0x21` | ACTUATOR_CMD | Server→육묘 | 4B |
| `0x24` | RFID_EVENT | 장치→Server | UID |
| `0xFE` | ACK | 양방향 | 2B |
| `0xFF` | NAK | 양방향 | 3B (reason 6종) |

---

### 2. Fail-safe Autonomous Navigation — 라인트레이싱 & 경로 추종

#### 5센서 차등 PWM 제어 (LineFollower.cpp)

```
센서 배열: [S1] [S2] [S3] [S4] [S5] (왼쪽 → 오른쪽)

S3만 감지 → goForward() 직진
S2 감지 (S1 없음) → turnLeftSoft() 소프트 좌보정 (우측만 구동)
S1 감지 → turnLeftHard() 급격 좌보정 (제자리 회전)
S1 + S5 동시 → detectCrossroad() 교차로 진입 판정
모두 미감지 → stop() 안전 정지 (OUT_OF_LINE)
```

```cpp
// LineFollower.cpp
bool LineFollower::detectCrossroad(int s1, int s2, int s3, int s4, int s5) {
return (s1 == 1 && s5 == 1) || (s2 == 1 && s4 == 1 && s3 == 0);
}

void LineFollower::followLine(int s1, int s2, int s3, int s4, int s5) {
if (s3==1 && s1==0 && s2==0 && s4==0 && s5==0) _motor.goForward();
else if ((s2==1 && s1==0) || (s2==1 && s3==1)) _motor.turnLeftSoft();
else if ((s4==1 && s5==0) || (s4==1 && s3==1)) _motor.turnRightSoft();
else if (s1==1) _motor.turnLeftHard();
else if (s5==1) _motor.turnRightHard();
else _motor.stop(); // 이탈 → 즉시 정지
}
```

#### 경로 명령 시퀀스 (BFS PathFinder → LineFollower)

```
PathFinder (BFS, 16노드 방향 그래프)
↓ "SLRUE" 등 문자열 생성
LineFollower (교차로마다 1문자씩 소비)
L=좌회전 R=우회전 U=U턴 S=직진통과 B=후진 E=도착
```

#### 15초 안전 타임아웃 — 하드웨어 정지 사고 방지

```cpp
// LineFollower.cpp — 후진 무한루프 원천 차단
const unsigned long SAFETY_MS = 15000;

if (detectCrossroad(s1,s2,s3,s4,s5) || (millis() - _backwardStartTime >= SAFETY_MS)) {
_motor.stop(); // PWM ENA/ENB = 0, 모든 IN 핀 LOW
_isBackwardUntilCrossroad = false; // 상태 초기화
...
}
```

후진 중 교차로를 찾지 못하더라도 **15초 후 강제 정지**하여 벽 충돌·탈선을 방지합니다.

---

### 3. Automated Pipeline — RFID 입고부터 배차까지 단일 워크플로우

NFC 스캔 1회로 아래 5단계가 자동 연속 실행됩니다.

```
NFC 스캔 (uid)
│
▼
① 트레이 품종 확인 (trays JOIN seedling_varieties JOIN crops WHERE nfc_uid)
│
▼
② 빈 구역 탐색 — 진행 중 예약분까지 차감한 실제 가용 용량 계산
│ SELECT available_spots = max_capacity - current_quantity - reserved
│ WHERE reserved = COUNT(transport_tasks WHERE task_status IN (0,1))
▼
③ transport_tasks INSERT (status=0, PENDING)
│
▼
④ trays.tray_status → IN_TRANSIT(2)
│
▼
⑤ farm_nodes.current_variety_id 선제 업데이트
→ FarmEnvManager가 해당 구역 온습도 임계값 즉시 전환
```

```python
# rfid_handler.py — 핵심 쿼리: 예약분 포함 가용 용량 계산
cursor.execute("""
SELECT fn.node_id,
(CAST(fn.max_capacity AS SIGNED)
- CAST(fn.current_quantity AS SIGNED)
- CAST(IFNULL(tt.reserved, 0) AS SIGNED)) AS available_spots
FROM farm_nodes fn
LEFT JOIN (
SELECT destination_node, COUNT(*) AS reserved
FROM transport_tasks
WHERE task_status IN (0, 1) -- PENDING + IN_PROGRESS 모두 예약 처리
GROUP BY destination_node
) tt ON fn.node_id = tt.destination_node
WHERE fn.node_type_id = 1
AND fn.current_variety_id = %s
AND fn.is_active = TRUE
AND available_spots > 0
ORDER BY available_spots DESC
LIMIT 1
""", (variety_id,))
```

동시 다중 입고 시 **오버플로우가 발생하지 않는 이유**: 이미 배차됐지만 아직 완료되지 않은 작업의 목적지도 `reserved`로 카운트하기 때문입니다.

---

### 4. 육묘장 환경 피드백 제어 (FarmEnvManager)

```python
# farm_env_manager.py
DEFAULT_TARGET_TEMP_MIN = 20.0 # °C — 육묘 환경 하한
DEFAULT_TARGET_TEMP_MAX = 28.0 # °C — 육묘 환경 상한
DEFAULT_TARGET_HUM_MIN = 50.0 # % — 건조 임계
DEFAULT_TARGET_HUM_MAX = 70.0 # % — 과습 임계

def _check_and_control(self, node_id, temperature, humidity):
if temperature > self.target_temp_max: self._activate_cooling_fan(node_id)
elif temperature < self.target_temp_min: self._activate_heater(node_id)

if humidity > self.target_hum_max: self._activate_ventilation(node_id)
elif humidity < self.target_hum_min: self._activate_humidifier(node_id)
```

RFID 입고 시 `farm_nodes.current_variety_id`가 갱신되면, 이 매니저는 해당 구역의 임계값을 품종 테이블(`seedling_varieties`)에서 읽어 동적으로 전환합니다.

---

## 신뢰성 설계 매트릭스

| 계층 | 실패 시나리오 | 감지 방법 | 대응 코드 |
|---|---|---|---|
| 통신 | Wi-Fi 노이즈·비트 반전 | CRC16-CCITT 검증 실패 | `SfamParser.reset()` — 파서 재초기화, 서버 무중단 |
| 통신 | 패킷 순서 뒤바뀜·누락 | SEQ 번호 불연속 감지 | `MSG_NAK(reason=4 타임아웃)` 재전송 요청 |
| 통신 | 페이로드 길이 초과 | LEN 필드 > 64 | `SfamParser.reset()` — 해당 패킷 폐기 |
| AGV 펌웨어 | 라인 이탈 | S1~S5 전부 미감지 | `RobotState::OUT_OF_LINE` → `motor.stop()` 즉시 |
| AGV 펌웨어 | 후진 중 교차로 미탐지 | `millis()` 경과 감시 | 15초 `SAFETY_MS` 타임아웃 → 강제 정지 |
| AGV 펌웨어 | 교차로 명령 없음 | `_currentStep >= pathString.length()` | `RobotState::ARRIVED` 처리, 주행 루프 종료 |
| 배차 | 목적지 용량 초과 | 예약분 포함 가용 계산 | `available_spots ≤ 0` → 다른 구역 탐색 |
| 배차 | TCP 소켓 단절 | `sock.sendall()` 예외 | `active_tcp_connections` 즉시 제거, 다음 폴링 재시도 |
| DB | 서버 재시작 후 미완료 작업 | `task_status ENUM` | `status=1(IN_PROGRESS)` 잔여 작업 재처리 가능 |
| 환경 제어 | 센서 오작동 (이상값) | 범위 이탈 즉시 감지 | 액추에이터 명령 전송 + `nursery_actuator_logs` 기록 |

---

## 자동 배차 루프

```python
# task_dispatcher.py — 3초 폴링 배차
def task_dispatcher_loop():
while True:
time.sleep(3)
# SELECT transport_tasks WHERE task_status = 0 (PENDING) ORDER BY ordered_at
# → active_tcp_connections에서 해당 AGV 소켓 확인
# → sendall({"cmd": "MOVE", "target_node": dest_node})
# → 성공: task_status = 1, started_at = NOW()
# → 실패: 소켓 제거, 다음 주기에서 재시도
```

---

## DB 스키마 — 전 생애주기 이력

```
crops ──► seedling_varieties ──► farm_nodes ◄── nursery_controller
│ │ │
│ transport_tasks nursery_sensor
│ (입고→운송→출하) │
│ │ nursery_sensor_logs
└────────────────────┘ nursery_actuator_logs
│
agv_robots ──► agv_telemetry_logs
│
users ──► user_action_logs
```

모든 이벤트에 `ordered_at / started_at / completed_at` 타임스탬프가 기록됩니다.

---

## 빠른 시작

```bash
# 1. 의존성 설치
cd control-server
pip install -r requirements.txt

# 2. 서버 실행
python main_server.py
# Flask :5001 / AGV TCP :8000 / 카메라 UDP :7070

# 3. 시뮬레이터 (하드웨어 없이 테스트)
python tests/fake_robot.py # AGV 시뮬레이터
python tests/fake_sensor.py # 육묘장 센서 시뮬레이터
python network/test_rfid_scenario.py # RFID 입고 전체 시나리오
```

ESP32 펌웨어: Arduino IDE 또는 PlatformIO에서 `robot-firmware/` · `farm-firmware/` 오픈 후 빌드

---

## 기술 스택

| 레이어 | 기술 |
|---|---|
| 제어 서버 | Python 3.11, Flask, pymysql, threading |
| 통신 프로토콜 | TCP — SFAM Binary v1.0 (CRC16-CCITT), UDP — 카메라 스트림 |
| AGV 펌웨어 | C++, Arduino Framework, ESP32-WROOM, L298N 모터 드라이버 |
| 육묘장 펌웨어 | C++, ESP32, Arduino Uno, DHT22, RC522 RFID |
| 데이터베이스 | MySQL 8.0 (AWS EC2), smart_farm_v2 |
| 관제 GUI | Python, PyQt5 (대시보드) |
| 협업 | Jira, Confluence, Git |

---

## 팀 구성 및 역할 (6인)

- **유정학**: SFAM v1.0 Binary Protocol 공동 설계 (패킷 구조·CRC·NAK 사유 코드 정의) · Jira/Confluence 협업 환경 구축 · 기술 문서화 · 발표 전략 총괄

---

유정학 (Junghak Yoo) · 010-2182-7655 · taekchun.utaek@gmail.com · github.com/yoorobo

