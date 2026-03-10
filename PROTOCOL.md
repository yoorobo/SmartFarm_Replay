# SFAM 장치간 Serial 패킷 프로토콜 명세 (v1.0)

본 문서는 스마트팜(SFAM) 시스템 내의 중앙 제어 서버, 스마트팜 컨트롤러, 무인 이송 로봇(AGV), 팜 노드 장치 간의 Serial/TCP 패킷 구조 및 통신 방식을 정의합니다.

## 1. 프레임 구조 (공통)

모든 데이터는 바이너리 형식으로 전송되며, 기본 프레임 구조는 총 8 Bytes의 고정 크기 헤더와 가변 길이의 페이로드로 구성됩니다. 멀티바이트 필드는 **Big-Endian (MSB First)** 로 전송됩니다.

| 오프셋 (Byte) | 필드 | 크기 | 설명 |
|---|---|---|---|
| `0x00` | `SOF` | 1B | 프레임 시작점 (**0xAA**) |
| `0x01` | `MSG_TYPE` | 1B | 메시지 타입 코드 |
| `0x02` | `SRC_ID` | 1B | 송신측 장치 ID |
| `0x03` | `DST_ID` | 1B | 수신측 장치 ID |
| `0x04` | `SEQ` | 1B | 패킷 순차 번호 (Sequence Number) |
| `0x05` | `LEN` | 1B | 뒤따르는 `PAYLOAD`의 바이트 수 (N) |
| `0x06` | `PAYLOAD` | N | 가변 길이 실제 전송 데이터 (최대 64B) |
| `0x06 + N` | `CRC16` | 2B | 오류 검출 코드 (CRC16-CCITT) |

* **최대 패킷 크기:** 헤더 8B + 페이로드 최대 64B = 최대 72 Bytes (문서상 74 Bytes max)
* **통신 속도 (UART):** `115,200 bps` (TCP 래핑 시 동일한 바이너리 배열 유지)

### 1-1. CRC-16 계산 방식
* **표준:** CRC16-CCITT
* **다항식 (Poly):** `0x1021`
* **초기값 (Init):** `0xFFFF`
* **계산 대상:** `SOF`부터 `PAYLOAD`의 끝까지 (총 `6 + N` Bytes)
* **입출력 반전 (Reflect In/Out):** `False`

---

## 2. 장치 ID 체계 (SRC_ID / DST_ID)

| ID 값 범위 (Hex) | 분류 | 설명 |
|---|---|---|
| `0x00` | Server | 메인 서버 (단일 마스터) |
| `0x01 ~ 0x08` | AGV Robot | 무인 이송 로봇 (AGV-01 ~ AGV-08) |
| `0x10 ~ 0x1F` | Nursery Controller | 육묘장 컨트롤러 |
| `0x20 ~ 0x5F` | Farm Node | 팜 노드 (도킹 경유지 식별 전용) |
| `0xFF` | Broadcast | 전체 장치 동시 수신 브로드캐스트 |

---

## 3. 메시지 유형 코드 (MSG_TYPE)

| 종류 | Type Code | Payload | 설명 |
|---|---|---|---|
| 상태 | `0x01` | 0 Bytes | **HEARTBEAT_REQ** (하트비트 요청) |
| 상태 | `0x02` | 2 Bytes | **HEARTBEAT_ACK** (하트비트 응답) |
| AGV | `0x10` | 8 Bytes | **AGV_TELEMETRY** (텔레메트리 보고) |
| AGV | `0x11` | 10 Bytes | **AGV_TASK_CMD** (작업 수행 명령) |
| AGV | `0x12` | 3 Bytes | **AGV_TASK_ACK** (작업 수락/거절 응답) |
| AGV | `0x13` | 5 Bytes | **AGV_STATUS_RPT** (단위 작업 상태 보고) |
| AGV | `0x14` | 2 Bytes | **AGV_EMERGENCY** (긴급 정지) |
| 센서 | `0x20` | Max 40B | **SENSOR_BATCH** (센서 데이터 일괄 전송) |
| 제어 | `0x21` | 4 Bytes | **ACTUATOR_CMD** (액추에이터 구동 제어) |
| 제어 | `0x22` | 3 Bytes | **ACTUATOR_ACK** (액추에이터 구동 결과) |
| 제어 | `0x23` | 4 Bytes | **CTRL_STATUS_RPT** (컨트롤러 상태 보고) |
| 도킹 | `0x30` | 3 Bytes | **DOCK_REQ** (도킹 요청) |
| 도킹 | `0x31` | 3 Bytes | **DOCK_ACK** (도킹 허가) |
| 공통 | `0xF0` | 4 Bytes | **ERROR_REPORT** (시스템 오류 전송) |
| 공통 | `0xFE` | 2 Bytes | **ACK** (일반 긍정 응답) |
| 공통 | `0xFF` | 3 Bytes | **NAK** (일반 부정 응답) |

---

## 4. Payload 상세 명세

모든 데이터 타입 오프셋 기준은 `PAYLOAD` 버퍼의 0번 인덱스를 시작점(`0x00`)으로 합니다.

### `0x02` HEARTBEAT_ACK
* `[0x00]` uint8: `status_id` (현재 기기 상태)
* `[0x01]` uint8: `aux_value` (보조 전압 등)

### `0x10` AGV_TELEMETRY (AGV 상태 및 좌표 주기 보고)
* `[0x00]` uint8: `status_id`
* `[0x01]` uint8: `battery_level` (0~100%)
* `[0x02]` uint8: `current_node_idx` (현재 위치)
* `[0x03]` uint8: `task_id_hi` (작업 ID 상위 비트)
* `[0x04]` uint8: `task_id_lo` (작업 ID 하위 비트)
* `[0x05]` uint8: `line_sensor_state` (라인 센서 값)
* `[0x06]` uint8: `motor_pwm` (모터 출력)
* `[0x07]` uint8: `error_id`

### `0x11` AGV_TASK_CMD (서버 → AGV 작업 명령)
* `[0x00~0x01]` uint16: `task_id`
* `[0x02~0x03]` uint16: `variety_id` (품종 ID)
* `[0x04]` uint8: `src_node_idx` (출발/시작 노드)
* `[0x05]` uint8: `dst_node_idx` (목적지 노드)
* `[0x06~0x07]` uint16: `quantity` (운반 수량/카운트)
* `[0x08]` uint8: `priority` (우선순위)
* `[0x09]` uint8: `reserved` (예약 공간)

### `0x12` AGV_TASK_ACK
* `[0x00~0x01]` uint16: `task_id`
* `[0x02]` uint8: `ack_code` (0: 거부, 1: 수락)

### `0x13` AGV_STATUS_RPT (노드 통과/작업 진행도 보고)
* `[0x00~0x01]` uint16: `task_id`
* `[0x02]` uint8: `task_status`
* `[0x03]` uint8: `node_idx`
* `[0x04]` uint8: `error_id`

### `0x14` AGV_EMERGENCY
* `[0x00]` uint8: `action` (1: Halt, 0: Resume)
* `[0x01]` uint8: `reason` (정지 사유 코드)

### `0x20` SENSOR_BATCH
* `[0x00]` uint8: `sensor_count` (N개)
* 반복 블록: 
  * `sensor_id` (1Byte)
  * `value` (3Bytes, int24 타입)

### `0x21` ACTUATOR_CMD
* `[0x00]` uint8: `actuator_id`
* `[0x01]` uint8: `state_value` (목표 상태)
* `[0x02]` uint8: `trigger_id`
* `[0x03]` uint8: `duration_sec` (유지 시간)

### `0x22` ACTUATOR_ACK
* `[0x00]` uint8: `actuator_id`
* `[0x01]` uint8: `state_value`
* `[0x02]` uint8: `result` (0: 실패, 1: 성공)

### `0x23` CTRL_STATUS_RPT
* `[0x00]` uint8: `control_mode` (수동/자동 등)
* `[0x01]` uint8: `device_status`
* `[0x02]` uint8: `actuator_bitmask` (액추에이터 현재 점등 상태)
* `[0x03]` uint8: `error_flags`

### `0x30` DOCK_REQ / `0x31` DOCK_ACK
* `[0x00]` uint8: `agv_idx`
* `[0x01]` uint8: `node_idx`
* `[0x02]` uint8: DOCK_REQ의 경우 `dock_action` / DOCK_ACK의 경우 `result`

### 공통 오류 및 ACK (0xF0, 0xFE, 0xFF)
* **0xF0 ERROR_REPORT:** `[0x00-0x01] error_id`, `[0x02] severity`, `[0x03] context`
* **0xFE ACK / 0xFF NAK:** 
  * `[0x00] acked_type / nacked_type` (ACK 대상 CMD MSG_TYPE)
  * `[0x01] seq` (ACK 대상 SEQ)
  * `[0x02]` (NAK 한정): `reason` 거절/실패 사유.

---

## 5. 통신 타이밍 스펙

| 종별 | 조건/주기 | 타임아웃/Retry 규격 |
|---|---|---|
| **HEARTBEAT** | `5초` (5000ms) 주기 전송 | 응답(`HEARTBEAT_ACK`)은 **200ms** 내 반환 필수. <br> 3회 연속 무응답 시 해당 장치 **OFFLINE** 처리. |
| **AGV_TELEMETRY** | AGV 주행/동작 중 `500ms` 주기 전송 | 단방향 보고용 (ACK 강제 안함). |
| **ACTUATOR_CMD/TASK_CMD** | 일반적인 제어 명령 전송 시 | ACK 미수신 시 **200ms** 후 재전송. <br> 최대 **3회** 재전송 후 실패 시 Error 로깅. |
