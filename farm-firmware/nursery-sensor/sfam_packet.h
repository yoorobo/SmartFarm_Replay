/*
 * ============================================================
 *  sfam_packet.h  —  SFAM 장치간 Serial 통신 공용 헤더
 * ============================================================
 *  사용 대상 : Arduino Uno, ESP32  (동일 파일을 양쪽 스케치 폴더에 복사)
 *
 *  프로토콜 요약:
 *    프레임  : SOF(1) + MSG_TYPE(1) + SRC_ID(1) + DST_ID(1)
 *              + SEQ(1) + LEN(1) + PAYLOAD(0~64) + CRC16(2)
 *    CRC16   : CRC16-CCITT  (Poly=0x1021, Init=0xFFFF, 반사 없음)
 *    CRC 범위: SOF ~ PAYLOAD 전체 (헤더 6바이트 + 페이로드 N바이트)
 *    엔디안  : 멀티바이트 필드 Big-Endian (MSB First)
 *    최대 패킷: 74 bytes (헤더6 + 페이로드64 + CRC2)
 * ============================================================
 */

#pragma once
#include <stdint.h>   // uint8_t, uint16_t, int32_t 등 정수 타입 정의
#include <string.h>   // memcpy, memset

// ============================================================
//  ─── 프로토콜 상수 ───────────────────────────────────────
// ============================================================
#define PKT_SOF             0xAA   // Start Of Frame 고정 마커
#define PKT_HDR_LEN         6      // 헤더 고정 크기 (SOF~LEN 필드까지)
#define PKT_CRC_LEN         2      // CRC16 크기
#define PKT_MAX_PAYLOAD     64     // 페이로드 최대 바이트 수
#define PKT_MAX_TOTAL       74     // 전체 패킷 최대 크기 (6+64+2+2)
#define PKT_TIMEOUT_MS      200    // ACK 대기 타임아웃 (ms)
#define PKT_MAX_RETRY       3      // 최대 재전송 횟수

// ============================================================
//  ─── MSG_TYPE 코드표 ─────────────────────────────────────
// ============================================================
#define MSG_HEARTBEAT_REQ   0x01   // 하트비트 요청 (Any → Any, Payload 없음)
#define MSG_HEARTBEAT_ACK   0x02   // 하트비트 응답 (Device → Server, 2B)
#define MSG_AGV_TELEMETRY   0x10   // AGV 텔레메트리 (AGV → Server, 8B)
#define MSG_AGV_TASK_CMD    0x11   // AGV 작업 명령 (Server → AGV, 10B)
#define MSG_AGV_TASK_ACK    0x12   // AGV 작업 수락/거부 (AGV → Server, 3B)
#define MSG_AGV_STATUS_RPT  0x13   // AGV 작업 상태 보고 (AGV → Server, 5B)
#define MSG_AGV_EMERGENCY   0x14   // AGV 긴급 정지 (양방향, 2B)
#define MSG_SENSOR_BATCH    0x20   // 센서 일괄 데이터 (Controller → Server, 가변)
#define MSG_ACTUATOR_CMD    0x21   // 액추에이터 명령 (Server → Controller, 4B)
#define MSG_ACTUATOR_ACK    0x22   // 액추에이터 실행 결과 (Controller → Server, 3B)
#define MSG_CTRL_STATUS_RPT 0x23   // 컨트롤러 상태 보고 (Controller → Server, 4B)
#define MSG_DOCK_REQ        0x30   // AGV 도킹 요청 (AGV → Controller, 3B)
#define MSG_DOCK_ACK        0x31   // 도킹 허가 응답 (Controller → AGV, 3B)
#define MSG_ERROR_REPORT    0xF0   // 오류 보고 (Any → Server, 4B)
#define MSG_ACK             0xFE   // 일반 긍정 응답 (Any → Any, 2B)
#define MSG_NAK             0xFF   // 일반 부정 응답 (Any → Any, 3B)

// ============================================================
//  ─── 장치 ID 체계 ────────────────────────────────────────
// ============================================================
#define DEV_SERVER          0x00   // 중앙 서버 (Master)
#define DEV_AGV_01          0x01   // AGV-01 (0x01~0x08)
#define DEV_AGV_02          0x02   // AGV-02 (0x01~0x08)
#define DEV_AGV_03          0x03   // AGV-03 (0x01~0x08)
#define DEV_AGV_04          0x04   // AGV-04 (0x01~0x08)
#define DEV_AGV_05          0x05   // AGV-05 (0x01~0x08)
#define DEV_AGV_06          0x06   // AGV-06 (0x01~0x08)
#define DEV_AGV_07          0x07   // AGV-07 (0x01~0x08)
#define DEV_AGV_08          0x08   // AGV-08 (0x01~0x08)
#define DEV_AGV_09          0x09   // AGV-09 (0x01~0x08)
#define DEV_CTRL_01         0x11   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_02         0x12   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_03         0x13   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_04         0x14   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_05         0x15   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_06         0x16   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_07         0x17   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_08         0x18   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_CTRL_09         0x19   // 육묘장 컨트롤러-01 (0x10~0x1F)
#define DEV_BROADCAST       0xFF   // 브로드캐스트 (전체 장치 수신, 응답 없음)

// ============================================================
//  ─── 패킷 헤더 구조체 (6 bytes, 고정) ────────────────────
// ============================================================
typedef struct __attribute__((packed)) {
    uint8_t sof;        // [0x00] 0xAA 고정 — 패킷 시작 마커
    uint8_t msg_type;   // [0x01] 메시지 유형 코드 (MSG_* 상수)
    uint8_t src_id;     // [0x02] 송신 장치 ID (DEV_* 상수)
    uint8_t dst_id;     // [0x03] 수신 장치 ID (DEV_BROADCAST=0xFF)
    uint8_t seq;        // [0x04] 시퀀스 번호 8-bit 롤오버 (ACK 매핑용)
    uint8_t len;        // [0x05] 페이로드 바이트 수 (0~64, CRC 미포함)
} PktHeader;

// ============================================================
//  ─── 페이로드 구조체 모음 ────────────────────────────────
// ============================================================

/* HEARTBEAT_ACK (0x02) — 2 bytes
   status_id : agv_status_codes.status_id 또는 device_status
   aux_value : AGV=배터리(%) / Controller=제어모드(0=MANUAL,1=AUTO)  */
typedef struct __attribute__((packed)) {
    uint8_t status_id;    // 장치 현재 상태 코드
    uint8_t aux_value;    // 보조 값 (AGV: 배터리%, Controller: 모드)
} PayloadHeartbeatAck;

/* AGV_TELEMETRY (0x10) — 8 bytes
   AGV → Server 주기 보고 (권장 500ms)                             */
typedef struct __attribute__((packed)) {
    uint8_t status_id;           // AGV 상태 (1=MOVING, 2=IDLE, 3=CHARGING, 4=FAULT)
    uint8_t battery_level;       // 배터리 잔량 (0~100 %)
    uint8_t current_node_idx;    // 현재 노드 인덱스 (0xFF=이동 중)
    uint8_t task_id_hi;          // 수행 중인 task_id 상위 바이트
    uint8_t task_id_lo;          // 수행 중인 task_id 하위 바이트 (없으면 0x0000)
    uint8_t line_sensor_state;   // 라인 센서 비트마스크
    uint8_t motor_pwm;           // 좌우 평균 PWM (상위4비트=좌, 하위4비트=우, ×17=실제값)
    uint8_t error_id;            // 오류 코드 인덱스 (0=정상)
} PayloadAgvTelemetry;

/* AGV_TASK_CMD (0x11) — 10 bytes
   Server → AGV 운송 작업 할당 (Big-Endian uint16 필드 주의)       */
typedef struct __attribute__((packed)) {
    uint8_t  task_id_hi;      // task_id 상위 바이트 (Big-Endian)
    uint8_t  task_id_lo;      // task_id 하위 바이트
    uint8_t  variety_id_hi;   // 품종 ID 상위 바이트 (Big-Endian)
    uint8_t  variety_id_lo;   // 품종 ID 하위 바이트
    uint8_t  src_node_idx;    // 출발지 노드 인덱스
    uint8_t  dst_node_idx;    // 목적지 노드 인덱스
    uint8_t  quantity_hi;     // 운반 수량 상위 바이트 (Big-Endian)
    uint8_t  quantity_lo;     // 운반 수량 하위 바이트
    uint8_t  priority;        // 우선순위 (0=일반, 1=높음, 2=긴급)
    uint8_t  reserved;        // 예약 (항상 0x00)
} PayloadAgvTaskCmd;

/* AGV_TASK_ACK (0x12) — 3 bytes                                   */
typedef struct __attribute__((packed)) {
    uint8_t task_id_hi;   // ACK 대상 task_id 상위 바이트 (Big-Endian)
    uint8_t task_id_lo;   // ACK 대상 task_id 하위 바이트
    uint8_t ack_code;     // 0=ACCEPT, 1=REJECT, 2=BUSY
} PayloadAgvTaskAck;

/* AGV_STATUS_RPT (0x13) — 5 bytes
   작업 상태 변화 시 이벤트 기반 전송                              */
typedef struct __attribute__((packed)) {
    uint8_t task_id_hi;    // 상태 변경 task_id 상위 바이트 (Big-Endian)
    uint8_t task_id_lo;    // 상태 변경 task_id 하위 바이트
    uint8_t task_status;   // 0=PENDING, 1=IN_PROGRESS, 2=DONE, 3=FAIL
    uint8_t node_idx;      // 현재 위치 노드 인덱스
    uint8_t error_id;      // 오류 발생 시 error_codes.error_id
} PayloadAgvStatusRpt;

/* AGV_EMERGENCY (0x14) — 2 bytes
   양방향: Server→AGV(명령) / AGV→Server(자체 비상 감지 알림)     */
typedef struct __attribute__((packed)) {
    uint8_t action;   // 0=E-STOP(즉시 정지), 1=RESUME(해제 및 재개)
    uint8_t reason;   // 0=서버명령, 1=장애물, 2=배터리부족, 3=라인이탈, 4=충돌, 9=기타
} PayloadAgvEmergency;

/* SENSOR_BATCH (0x20) — 가변 (최대 41 bytes)
   개별 센서 항목 구조체 (sensor_id + value 세트)                  */
typedef struct __attribute__((packed)) {
    uint8_t sensor_id;   // 센서 고유 ID (nursery_sensors.sensor_id)
    uint8_t value[3];    // int24 Big-Endian, 실측값×100 (예: 25.50℃ → 2450 = 0x000992)
} SensorEntry;

#define SENSOR_BATCH_MAX   10   // 패킷당 최대 센서 수

typedef struct __attribute__((packed)) {
    uint8_t    sensor_count;                    // 포함된 센서 개수 (1~10)
    SensorEntry entries[SENSOR_BATCH_MAX];      // 센서 데이터 배열
} PayloadSensorBatch;

/* ACTUATOR_CMD (0x21) — 4 bytes
   Server → Controller 액추에이터 제어 명령                        */
typedef struct __attribute__((packed)) {
    uint8_t actuator_id;    // 제어 대상 액추에이터 ID (1~255)
    uint8_t state_value;    // 0=OFF, 1=ON, 2~255=PWM 듀티(%)
    uint8_t trigger_id;     // 트리거 원인 (1=AUTO, 2=MANUAL, 3=SCHEDULE)
    uint8_t duration_sec;   // 지속시간(초). 0=무기한, 1~255=지정 후 자동 OFF
} PayloadActuatorCmd;

/* ACTUATOR_ACK (0x22) — 3 bytes
   Controller → Server 액추에이터 명령 실행 결과                   */
typedef struct __attribute__((packed)) {
    uint8_t actuator_id;   // 실행된 액추에이터 ID
    uint8_t state_value;   // 실제 적용된 상태 값
    uint8_t result;        // 0=SUCCESS, 1=FAIL, 2=PARTIAL
} PayloadActuatorAck;

/* CTRL_STATUS_RPT (0x23) — 4 bytes
   Controller → Server 전체 상태 주기 보고 (60초)                  */
typedef struct __attribute__((packed)) {
    uint8_t control_mode;       // 0=MANUAL, 1=AUTO
    uint8_t device_status;      // 0=OFFLINE, 1=ONLINE
    uint8_t actuator_bitmask;   // 현재 ON 상태 액추에이터 (bit0~bit7 = ID 1~8)
    uint8_t error_flags;        // bit0=센서이상, bit1=액추에이터이상, bit2=통신이상
} PayloadCtrlStatusRpt;

/* DOCK_REQ (0x30) — 3 bytes
   AGV → Controller, NFC 노드 도착 시 전송                         */
typedef struct __attribute__((packed)) {
    uint8_t agv_idx;       // 요청 AGV 인덱스 (0x01~0x08)
    uint8_t node_idx;      // 도착한 노드 인덱스 (0x20~0x5F)
    uint8_t dock_action;   // 0=ARRIVE, 1=DEPART, 2=LOAD, 3=UNLOAD
} PayloadDockReq;

/* DOCK_ACK (0x31) — 3 bytes
   Controller → AGV, 도킹 허가/대기/거부 응답                      */
typedef struct __attribute__((packed)) {
    uint8_t agv_idx;    // 수신 AGV 인덱스
    uint8_t node_idx;   // 대상 노드 인덱스
    uint8_t result;     // 0=OK(허가), 1=WAIT(대기), 2=ERROR(거부)
} PayloadDockAck;

/* ERROR_REPORT (0xF0) — 4 bytes
   Any → Server, 오류 발생 즉시 전송                               */
typedef struct __attribute__((packed)) {
    uint8_t error_id_hi;   // error_codes.error_id 상위 바이트 (Big-Endian)
    uint8_t error_id_lo;   // error_codes.error_id 하위 바이트
    uint8_t severity;      // 1=INFO, 2=WARN, 3=ERROR
    uint8_t context;       // bit0=작업중, bit1=이동중, bit2=도킹중, bit3=수동모드
} PayloadErrorReport;

/* ACK (0xFE) — 2 bytes
   명령 수신 성공 확인, 200ms 내 전송 필수                         */
typedef struct __attribute__((packed)) {
    uint8_t acked_type;   // ACK 대상 MSG_TYPE
    uint8_t seq;          // ACK 대상 SEQ 번호
} PayloadAck;

/* NAK (0xFF) — 3 bytes
   명령 수신 실패 또는 처리 거부                                    */
typedef struct __attribute__((packed)) {
    uint8_t nacked_type;   // NAK 대상 MSG_TYPE
    uint8_t seq;           // NAK 대상 SEQ 번호
    uint8_t reason;        // 0=CRC오류, 1=LEN오류, 2=상태불가, 3=권한없음, 4=타임아웃, 9=알수없음
} PayloadNak;

// ============================================================
//  ─── CRC16-CCITT 계산 함수 ───────────────────────────────
// ============================================================

/*
 * crc16_ccitt(data, len)
 * 역할 : SOF ~ PAYLOAD 범위에 대해 CRC16-CCITT 계산
 * 방식 : Poly=0x1021, Init=0xFFFF, 반사(reflection) 없음
 * 반환 : 16-bit 체크섬 (Big-Endian 으로 패킷 맨 끝에 붙임)
 */
static inline uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0xFFFF;           // 초기값 0xFFFF
    for (uint16_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;   // 현재 바이트를 CRC 상위 8비트에 XOR
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000)             // 최상위 비트가 1이면
                crc = (crc << 1) ^ 0x1021; // 좌시프트 후 폴리노미얼 XOR
            else
                crc <<= 1;                // 좌시프트만
        }
    }
    return crc;
}

// ============================================================
//  ─── int24 인코딩/디코딩 헬퍼 ────────────────────────────
// ============================================================

/*
 * encodeInt24(buf, val)
 * 역할 : 32-bit 정수의 하위 24비트를 Big-Endian 3바이트로 인코딩
 * 용도 : SENSOR_BATCH 의 value 필드 (실측값×100 → int24 BE)
 */
static inline void encodeInt24(uint8_t *buf, int32_t val) {
    buf[0] = (uint8_t)((val >> 16) & 0xFF);   // 상위 바이트
    buf[1] = (uint8_t)((val >>  8) & 0xFF);   // 중간 바이트
    buf[2] = (uint8_t)((val      ) & 0xFF);   // 하위 바이트
}

/*
 * decodeInt24(buf)
 * 역할 : Big-Endian 3바이트를 부호 있는 32-bit 정수로 복원
 * 주의 : 부호 확장 처리 포함 (MSB=1 이면 음수)
 */
static inline int32_t decodeInt24(const uint8_t *buf) {
    int32_t val = ((uint32_t)buf[0] << 16)
                | ((uint32_t)buf[1] <<  8)
                |  (uint32_t)buf[2];
    if (val & 0x800000) val |= (int32_t)0xFF000000;   // 부호 확장
    return val;
}

// ============================================================
//  ─── 공통 패킷 빌더 ─────────────────────────────────────
// ============================================================

/*
 * buildPacket(buf, msg_type, src_id, dst_id, seq, payload, payload_len)
 * 역할 : 헤더 + 페이로드 + CRC16 을 buf 에 조립
 * 인자 :
 *   buf         - 출력 버퍼 (PKT_MAX_TOTAL 이상 크기 필요)
 *   msg_type    - 메시지 유형 (MSG_* 상수)
 *   src_id      - 송신 장치 ID
 *   dst_id      - 수신 장치 ID (DEV_BROADCAST 가능)
 *   seq         - 시퀀스 번호 (호출측에서 관리)
 *   payload     - 페이로드 데이터 포인터 (NULL 허용, len=0 이면 무시)
 *   payload_len - 페이로드 바이트 수 (0~64)
 * 반환 : 완성된 전체 패킷 바이트 수 (헤더6 + 페이로드N + CRC2)
 */
static inline uint8_t buildPacket(uint8_t       *buf,
                                   uint8_t        msg_type,
                                   uint8_t        src_id,
                                   uint8_t        dst_id,
                                   uint8_t        seq,
                                   const uint8_t *payload,
                                   uint8_t        payload_len)
{
    buf[0] = PKT_SOF;          // SOF 고정 마커
    buf[1] = msg_type;         // 메시지 유형
    buf[2] = src_id;           // 송신 장치 ID
    buf[3] = dst_id;           // 수신 장치 ID
    buf[4] = seq;              // 시퀀스 번호
    buf[5] = payload_len;      // 페이로드 길이

    // 페이로드 복사 (payload_len=0 이면 건너뜀)
    if (payload && payload_len > 0)
        memcpy(buf + PKT_HDR_LEN, payload, payload_len);

    // CRC16 계산: SOF(buf[0]) ~ PAYLOAD 마지막 바이트까지
    uint16_t crc = crc16_ccitt(buf, PKT_HDR_LEN + payload_len);

    // CRC16 Big-Endian 으로 패킷 끝에 추가
    buf[PKT_HDR_LEN + payload_len]     = (uint8_t)(crc >> 8);    // CRC 상위 바이트
    buf[PKT_HDR_LEN + payload_len + 1] = (uint8_t)(crc & 0xFF);  // CRC 하위 바이트

    return PKT_HDR_LEN + payload_len + PKT_CRC_LEN;   // 전체 패킷 길이 반환
}

/*
 * validatePacket(buf, total_len, hdr_out)
 * 역할 : 수신 버퍼의 패킷 유효성 검사 (SOF + LEN 범위 + CRC16)
 * 인자 :
 *   buf       - 수신 버퍼 (SOF 부터 CRC 끝까지)
 *   total_len - 버퍼 내 수신된 전체 바이트 수
 *   hdr_out   - 파싱된 헤더를 저장할 포인터 (NULL 허용)
 * 반환 : true=유효 패킷, false=오류
 */
static inline bool validatePacket(const uint8_t *buf,
                                   uint8_t        total_len,
                                   PktHeader     *hdr_out)
{
    // ── SOF 확인 ──────────────────────────────────────────
    if (buf[0] != PKT_SOF) return false;

    // ── 최소 크기 확인 (헤더 + CRC 최소) ──────────────────
    if (total_len < PKT_HDR_LEN + PKT_CRC_LEN) return false;

    uint8_t payload_len = buf[5];   // LEN 필드

    // ── LEN 범위 확인 ─────────────────────────────────────
    if (payload_len > PKT_MAX_PAYLOAD) return false;

    // ── 전체 길이 확인 ────────────────────────────────────
    uint8_t expected_len = PKT_HDR_LEN + payload_len + PKT_CRC_LEN;
    if (total_len != expected_len) return false;

    // ── CRC16 검증 ────────────────────────────────────────
    uint16_t calc_crc    = crc16_ccitt(buf, PKT_HDR_LEN + payload_len);
    uint16_t recv_crc    = ((uint16_t)buf[PKT_HDR_LEN + payload_len] << 8)
                           | buf[PKT_HDR_LEN + payload_len + 1];
    if (calc_crc != recv_crc) return false;

    // ── 헤더 파싱 결과 저장 ───────────────────────────────
    if (hdr_out) {
        hdr_out->sof      = buf[0];
        hdr_out->msg_type = buf[1];
        hdr_out->src_id   = buf[2];
        hdr_out->dst_id   = buf[3];
        hdr_out->seq      = buf[4];
        hdr_out->len      = buf[5];
    }
    return true;
}

// ============================================================
//  ─── 메시지별 전용 빌더 함수 ─────────────────────────────
//  공통 항목(SOF, SRC, DST, SEQ)은 내부에서 처리하고
//  페이로드 값만 인자로 받아 완성 패킷을 반환합니다.
// ============================================================

// ── HEARTBEAT_REQ (0x01) — Payload 없음 ──────────────────
static inline uint8_t buildHeartbeatReq(uint8_t *buf,
                                          uint8_t  src_id,
                                          uint8_t  dst_id,
                                          uint8_t  seq)
{
    return buildPacket(buf, MSG_HEARTBEAT_REQ, src_id, dst_id, seq, NULL, 0);
}

// ── HEARTBEAT_ACK (0x02) ─────────────────────────────────
static inline uint8_t buildHeartbeatAck(uint8_t *buf,
                                          uint8_t  src_id,
                                          uint8_t  dst_id,
                                          uint8_t  seq,
                                          uint8_t  status_id,
                                          uint8_t  aux_value)
{
    PayloadHeartbeatAck p;
    p.status_id = status_id;
    p.aux_value = aux_value;
    return buildPacket(buf, MSG_HEARTBEAT_ACK, src_id, dst_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── AGV_TELEMETRY (0x10) ─────────────────────────────────
static inline uint8_t buildAgvTelemetry(uint8_t *buf,
                                          uint8_t  src_id,
                                          uint8_t  seq,
                                          uint8_t  status_id,
                                          uint8_t  battery_pct,
                                          uint8_t  node_idx,
                                          uint16_t task_id,
                                          uint8_t  line_sensor,
                                          uint8_t  motor_pwm,
                                          uint8_t  error_id)
{
    PayloadAgvTelemetry p;
    p.status_id          = status_id;
    p.battery_level      = battery_pct;
    p.current_node_idx   = node_idx;
    p.task_id_hi         = (uint8_t)(task_id >> 8);   // Big-Endian 분리
    p.task_id_lo         = (uint8_t)(task_id & 0xFF);
    p.line_sensor_state  = line_sensor;
    p.motor_pwm          = motor_pwm;
    p.error_id           = error_id;
    return buildPacket(buf, MSG_AGV_TELEMETRY, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── AGV_TASK_CMD (0x11) ──────────────────────────────────
static inline uint8_t buildAgvTaskCmd(uint8_t  *buf,
                                        uint8_t   seq,
                                        uint8_t   dst_agv_id,
                                        uint16_t  task_id,
                                        uint16_t  variety_id,
                                        uint8_t   src_node,
                                        uint8_t   dst_node,
                                        uint16_t  quantity,
                                        uint8_t   priority)
{
    PayloadAgvTaskCmd p;
    p.task_id_hi    = (uint8_t)(task_id    >> 8);
    p.task_id_lo    = (uint8_t)(task_id    & 0xFF);
    p.variety_id_hi = (uint8_t)(variety_id >> 8);
    p.variety_id_lo = (uint8_t)(variety_id & 0xFF);
    p.src_node_idx  = src_node;
    p.dst_node_idx  = dst_node;
    p.quantity_hi   = (uint8_t)(quantity   >> 8);
    p.quantity_lo   = (uint8_t)(quantity   & 0xFF);
    p.priority      = priority;
    p.reserved      = 0x00;
    return buildPacket(buf, MSG_AGV_TASK_CMD, DEV_SERVER, dst_agv_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── AGV_TASK_ACK (0x12) ──────────────────────────────────
static inline uint8_t buildAgvTaskAck(uint8_t *buf,
                                        uint8_t  src_id,
                                        uint8_t  seq,
                                        uint16_t task_id,
                                        uint8_t  ack_code)
{
    PayloadAgvTaskAck p;
    p.task_id_hi = (uint8_t)(task_id >> 8);
    p.task_id_lo = (uint8_t)(task_id & 0xFF);
    p.ack_code   = ack_code;
    return buildPacket(buf, MSG_AGV_TASK_ACK, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── AGV_STATUS_RPT (0x13) ────────────────────────────────
static inline uint8_t buildAgvStatusRpt(uint8_t *buf,
                                          uint8_t  src_id,
                                          uint8_t  seq,
                                          uint16_t task_id,
                                          uint8_t  task_status,
                                          uint8_t  node_idx,
                                          uint8_t  error_id)
{
    PayloadAgvStatusRpt p;
    p.task_id_hi  = (uint8_t)(task_id >> 8);
    p.task_id_lo  = (uint8_t)(task_id & 0xFF);
    p.task_status = task_status;
    p.node_idx    = node_idx;
    p.error_id    = error_id;
    return buildPacket(buf, MSG_AGV_STATUS_RPT, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── AGV_EMERGENCY (0x14) ─────────────────────────────────
static inline uint8_t buildAgvEmergency(uint8_t *buf,
                                          uint8_t  src_id,
                                          uint8_t  dst_id,
                                          uint8_t  seq,
                                          uint8_t  action,
                                          uint8_t  reason)
{
    PayloadAgvEmergency p;
    p.action = action;
    p.reason = reason;
    return buildPacket(buf, MSG_AGV_EMERGENCY, src_id, dst_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── SENSOR_BATCH (0x20) — 가변 페이로드 ─────────────────
/*
 * buildSensorBatch(buf, src_id, seq, count, sensor_ids, values_x100)
 * 인자 :
 *   count        - 센서 개수 (1~SENSOR_BATCH_MAX)
 *   sensor_ids[] - 각 센서의 ID 배열
 *   values_x100[]- 각 센서 측정값 × 100 배열 (int32_t, int24 범위)
 *                  예: 온도 25.50℃ → 2550
 */
static inline uint8_t buildSensorBatch(uint8_t       *buf,
                                         uint8_t        src_id,
                                         uint8_t        seq,
                                         uint8_t        count,
                                         const uint8_t *sensor_ids,
                                         const int32_t *values_x100)
{
    uint8_t payload[1 + SENSOR_BATCH_MAX * 4];   // sensor_count(1) + entries(4×N)
    uint8_t idx = 0;

    payload[idx++] = count;   // 센서 개수 첫 바이트

    // 각 센서 항목: sensor_id(1B) + value(3B int24 BE)
    for (uint8_t i = 0; i < count && i < SENSOR_BATCH_MAX; i++) {
        payload[idx++] = sensor_ids[i];               // 센서 ID
        encodeInt24(&payload[idx], values_x100[i]);   // 측정값 × 100 → int24 BE
        idx += 3;
    }
    return buildPacket(buf, MSG_SENSOR_BATCH, src_id, DEV_SERVER, seq,
                       payload, idx);
}

// ── ACTUATOR_CMD (0x21) ──────────────────────────────────
static inline uint8_t buildActuatorCmd(uint8_t *buf,
                                         uint8_t  dst_ctrl_id,
                                         uint8_t  seq,
                                         uint8_t  actuator_id,
                                         uint8_t  state_value,
                                         uint8_t  trigger_id,
                                         uint8_t  duration_sec)
{
    PayloadActuatorCmd p;
    p.actuator_id  = actuator_id;
    p.state_value  = state_value;
    p.trigger_id   = trigger_id;
    p.duration_sec = duration_sec;
    return buildPacket(buf, MSG_ACTUATOR_CMD, DEV_SERVER, dst_ctrl_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── ACTUATOR_ACK (0x22) ──────────────────────────────────
static inline uint8_t buildActuatorAck(uint8_t *buf,
                                         uint8_t  src_id,
                                         uint8_t  seq,
                                         uint8_t  actuator_id,
                                         uint8_t  state_value,
                                         uint8_t  result)
{
    PayloadActuatorAck p;
    p.actuator_id = actuator_id;
    p.state_value = state_value;
    p.result      = result;
    return buildPacket(buf, MSG_ACTUATOR_ACK, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── CTRL_STATUS_RPT (0x23) ───────────────────────────────
static inline uint8_t buildCtrlStatusRpt(uint8_t *buf,
                                           uint8_t  src_id,
                                           uint8_t  seq,
                                           uint8_t  control_mode,
                                           uint8_t  device_status,
                                           uint8_t  actuator_bitmask,
                                           uint8_t  error_flags)
{
    PayloadCtrlStatusRpt p;
    p.control_mode      = control_mode;
    p.device_status     = device_status;
    p.actuator_bitmask  = actuator_bitmask;
    p.error_flags       = error_flags;
    return buildPacket(buf, MSG_CTRL_STATUS_RPT, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── DOCK_REQ (0x30) ──────────────────────────────────────
static inline uint8_t buildDockReq(uint8_t *buf,
                                     uint8_t  agv_id,
                                     uint8_t  dst_ctrl_id,
                                     uint8_t  seq,
                                     uint8_t  node_idx,
                                     uint8_t  dock_action)
{
    PayloadDockReq p;
    p.agv_idx     = agv_id;
    p.node_idx    = node_idx;
    p.dock_action = dock_action;
    return buildPacket(buf, MSG_DOCK_REQ, agv_id, dst_ctrl_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── DOCK_ACK (0x31) ──────────────────────────────────────
static inline uint8_t buildDockAck(uint8_t *buf,
                                     uint8_t  src_ctrl_id,
                                     uint8_t  dst_agv_id,
                                     uint8_t  seq,
                                     uint8_t  agv_idx,
                                     uint8_t  node_idx,
                                     uint8_t  result)
{
    PayloadDockAck p;
    p.agv_idx  = agv_idx;
    p.node_idx = node_idx;
    p.result   = result;
    return buildPacket(buf, MSG_DOCK_ACK, src_ctrl_id, dst_agv_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── ERROR_REPORT (0xF0) ──────────────────────────────────
static inline uint8_t buildErrorReport(uint8_t *buf,
                                         uint8_t  src_id,
                                         uint8_t  seq,
                                         uint16_t error_id,
                                         uint8_t  severity,
                                         uint8_t  context)
{
    PayloadErrorReport p;
    p.error_id_hi = (uint8_t)(error_id >> 8);
    p.error_id_lo = (uint8_t)(error_id & 0xFF);
    p.severity    = severity;
    p.context     = context;
    return buildPacket(buf, MSG_ERROR_REPORT, src_id, DEV_SERVER, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── ACK (0xFE) ────────────────────────────────────────────
static inline uint8_t buildAck(uint8_t *buf,
                                 uint8_t  src_id,
                                 uint8_t  dst_id,
                                 uint8_t  seq,
                                 uint8_t  acked_type,
                                 uint8_t  acked_seq)
{
    PayloadAck p;
    p.acked_type = acked_type;
    p.seq        = acked_seq;
    return buildPacket(buf, MSG_ACK, src_id, dst_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}

// ── NAK (0xFF) ────────────────────────────────────────────
static inline uint8_t buildNak(uint8_t *buf,
                                 uint8_t  src_id,
                                 uint8_t  dst_id,
                                 uint8_t  seq,
                                 uint8_t  nacked_type,
                                 uint8_t  nacked_seq,
                                 uint8_t  reason)
{
    PayloadNak p;
    p.nacked_type = nacked_type;
    p.seq         = nacked_seq;
    p.reason      = reason;
    return buildPacket(buf, MSG_NAK, src_id, dst_id, seq,
                       (const uint8_t *)&p, sizeof(p));
}
