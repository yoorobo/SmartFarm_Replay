/*
 * ============================================================
 *  arduino_sfam_comm.ino  —  Arduino Uno SFAM 통합 펌웨어
 *  (제어 + SFAM 이진 패킷 통신 완전 통합 버전)
 * ============================================================
 *
 *  역할:
 *    1. 센서 주기 읽기    (DHT 온습도, 조도, 수위)
 *    2. 액추에이터 제어  (펌프, 팬, 히터, RGB LED)
 *    3. RFID 카드 인식  (MIFARE Ultralight, 인증 없음)
 *    4. 자동 모드 제어  (타이머 펌프, 히스테리시스 팬/히터, 조도 LED)
 *    5. USB 시리얼 명령 처리 (Serial Monitor)
 *    6. SFAM 패킷 통신  (ESP32 ↔ Arduino, SoftwareSerial 38400 bps)
 *       - 수신: HEARTBEAT_REQ, ACTUATOR_CMD, AGV_TASK_CMD, ACK, NAK
 *       - 송신: HEARTBEAT_ACK, SENSOR_BATCH, ACTUATOR_ACK,
 *               CTRL_STATUS_RPT, ERROR_REPORT, ACK, NAK
 *
 *  IO 맵핑:
 *    D2  : DHT 온습도 센서 (단선 데이터)
 *    D3  : RGB LED Red   채널 (PWM)
 *    D4  : 물 펌프 MOSFET
 *    D5  : RGB LED Green 채널 (PWM)
 *    D6  : RGB LED Blue  채널 (PWM)
 *    D7  : 순환 팬 MOSFET
 *    D8  : 히터 필름 MOSFET
 *    D9  : RFID-RC522 RST
 *    D10 : RFID-RC522 SS (SPI)
 *    D11 : RFID MOSI (SPI 하드웨어 고정)
 *    D12 : RFID MISO (SPI 하드웨어 고정)
 *    D13 : RFID SCK  (SPI 하드웨어 고정)
 *    A0  : 조도 센서 (아날로그 0~1023)
 *    A1  : 수위 센서 (아날로그 0~1023)
 *    A2  : ESP32 SoftwareSerial TX (Arduino → ESP32 GPIO16)
 *    A3  : ESP32 SoftwareSerial RX (ESP32 GPIO17 → Arduino)
 *
 *  배선 주의:
 *    Arduino A2(5V TX) → [1kΩ + 2kΩ 분압] → ESP32 GPIO16(3.3V RX)
 *    Arduino A3(RX)    ← ESP32 GPIO17(TX) 직결 가능 (3.3V → 5V 내성)
 *    GND 반드시 공통 연결
 *
 *  SoftwareSerial 속도 선정:
 *    Arduino Uno 16MHz 기준 → 38400 bps가 안정 최대값
 *    (115200 bps는 비트당 138클럭 → 인터럽트 오버헤드로 수신 뭉개짐)
 *    ESP32 Serial2도 반드시 동일 속도(38400 bps)로 설정
 *
 *  필요 라이브러리:
 *    - DHT sensor library (Adafruit)
 *    - MFRC522 (Miguel Balboa / GithubCommunity)
 *    - SoftwareSerial (Arduino 기본 내장)
 *    - SPI (Arduino 기본 내장)
 *
 *  필요 파일:
 *    sfam_packet.h (이 .ino 파일과 같은 폴더에 위치)
 * ============================================================
 */

// ── 라이브러리 포함 ──────────────────────────────────────────
#include "sfam_packet.h"     // SFAM 공용 패킷 구조체·빌더·CRC16
#include <SPI.h>             // RFID-RC522 SPI 통신
#include <MFRC522.h>         // RFID-RC522 제어 라이브러리
#include <DHT.h>             // Adafruit DHT 온습도 센서
#include <SoftwareSerial.h>  // ESP32 연결용 소프트웨어 UART

// ============================================================
//  핀 번호 정의
// ============================================================
#define PIN_DHT         2    // DHT 센서 단선 데이터 핀
#define PIN_LED_R       3    // RGB LED Red   채널 PWM (D3)
#define PIN_PUMP        4    // 물 펌프 MOSFET 제어 (D4) HIGH=ON
#define PIN_LED_G       5    // RGB LED Green 채널 PWM (D5)
#define PIN_LED_B       6    // RGB LED Blue  채널 PWM (D6)
#define PIN_FAN         7    // 순환 팬 MOSFET 제어 (D7) HIGH=ON
#define PIN_HEATER      8    // 히터 필름 MOSFET 제어 (D8) HIGH=ON
#define PIN_RFID_RST    9    // RFID-RC522 리셋 핀
#define PIN_RFID_SS     10   // RFID-RC522 SPI 슬레이브 선택
#define PIN_LIGHT       A0   // 조도 센서 아날로그 입력
#define PIN_WATER       A1   // 수위 센서 아날로그 입력
#define PIN_ESP_TX      A2   // SoftwareSerial TX → ESP32 GPIO16(RX2)
#define PIN_ESP_RX      A3   // SoftwareSerial RX ← ESP32 GPIO17(TX2)

// ── DHT 센서 타입 ────────────────────────────────────────────
#define DHT_TYPE        DHT11   // DHT22 사용 시 DHT22 로 변경

// ── 센서 RANGE ──────────────────────────────────────────────
#define WATER_MAX_RANGE 1000   // Water Level raw range(0~1023)
#define LIGHT_MAX_RANGE 1000   // Lighte raw range(0~1023)

// ── 이 Arduino 의 SFAM 장치 ID ───────────────────────────────
#define MY_DEV_ID       DEV_CTRL_01   // 0x11 = 육묘장 컨트롤러-01

// ── RFID 페이지 번호 (MIFARE Ultralight SFAM 카드 메모리 맵) ─
// Ultralight: 페이지 0~15, 페이지당 4 bytes
//   페이지 0~3  : 제조사 고정 영역 (UID, Lock, OTP) — 읽기 전용
//   페이지 4~15 : 사용자 영역 (48 bytes) — SFAM 데이터 기록
//
//  ┌──────┬──────────────────────────────────────────────────┐
//  │ 페이지│ SFAM 용도 (4 bytes)                             │
//  ├──────┼──────────────────────────────────────────────────┤
//  │  4   │ 카드정보   : card_type(1) | card_id(2) | rsv(1) │
//  │  5   │ 작물정보   : variety_id(2) | sow_mm(1) | sow_dd(1)│
//  │  6   │ 제어파라미 : temp_set(1) | hum_set(1) | rsv(2)  │
//  │  7   │ 펌프파라미 : interval_min(1) | dur_sec(1) | rsv(2)│
//  │  8   │ 조도파라미 : threshold_h(1) | threshold_l(1) | rsv(2)│
//  │ 9~15 │ 예약 (향후 확장)                                │
//  └──────┴──────────────────────────────────────────────────┘
#define RFID_PAGE_UL_CARDINFO     4   // 카드정보
#define RFID_PAGE_UL_CROP         5   // 작물정보
#define RFID_PAGE_UL_CTRL_PARAM   6   // 제어파라미터
#define RFID_PAGE_UL_PUMP_PARAM   7   // 펌프파라미터
#define RFID_PAGE_UL_LIGHT_PARAM  8   // 조도파라미터

// ── RFID 등록 카드 종류 코드 ─────────────────────────────────
// rfid_handler.py 의 REGISTERED_USERS / WORKER_CARDS / POT_CARDS 동기화
#define CARD_TYPE_UNKNOWN    0   // 미등록 카드
#define CARD_TYPE_USER       1   // REGISTERED_USERS (사용자 인증·관리)
#define CARD_TYPE_WORKER     2   // WORKER_CARDS (입/출고 스테이션 모드 전환)
#define CARD_TYPE_POT        3   // POT_CARDS (화분·트레이)

// ── 역할(Role) 코드 ─────────────────────────────────────────
#define ROLE_ADMIN           1   // 관리자
#define ROLE_WORKER_ROLE     2   // 작업자

// ── 액션(Action) 코드 ───────────────────────────────────────
#define ACTION_OPEN_DOOR     1   // 출입문 개방
#define ACTION_ROBOT_R01     2   // AGV R01 호출
#define ACTION_ROBOT_R02     3   // AGV R02 호출

// ── 스테이션 작업 모드 코드 ──────────────────────────────────
#define WMODE_INBOUND        1   // 입고 모드
#define WMODE_OUTBOUND       2   // 출고 모드

// ── UID 최대 바이트 수 (Ultralight=7B) ──────────────────────
#define MAX_UID_BYTES        7

// ============================================================
//  RFID 등록 카드 구조체 정의
// ============================================================

// 등록 사용자 카드 (rfid_handler.py REGISTERED_USERS)
struct RegUser {
    uint8_t uid[MAX_UID_BYTES];
    uint8_t uidLen;
    char    name[16];   // UTF-8 한글 최대 4자+공백+영자 = 15B + NULL
    uint8_t role;       // ROLE_ADMIN / ROLE_WORKER_ROLE
    uint8_t action;     // ACTION_OPEN_DOOR / ROBOT_R01 / ROBOT_R02
};

// 작업자 모드 전환 카드 (rfid_handler.py WORKER_CARDS)
struct WorkerCardEntry {
    uint8_t uid[MAX_UID_BYTES];
    uint8_t uidLen;
    uint8_t mode;       // WMODE_INBOUND / WMODE_OUTBOUND
};

// 화분·트레이 카드 (rfid_handler.py POT_CARDS)
struct PotCardEntry {
    uint8_t uid[MAX_UID_BYTES];
    uint8_t uidLen;
};

// ============================================================
//  RFID 등록 카드 레지스트리 데이터
//  ※ rfid_handler.py 변경 시 이 테이블도 반드시 동기화할 것
// ============================================================

// ─ REGISTERED_USERS ─────────────────────────────────────────
//  412E261B      → 관리자 A (admin,  open_door)
//  049C37026C2190→ 작업자 B (worker, call_robot_R01)
//  049C36026C2190→ 작업자 C (worker, call_robot_R02)
const RegUser REG_USERS[] = {
    { {0x41,0x2E,0x26,0x1B,0x00,0x00,0x00}, 4, "Admin A",    ROLE_ADMIN,       ACTION_OPEN_DOOR },
    { {0x04,0x9C,0x37,0x02,0x6C,0x21,0x90}, 7, "Worker B",   ROLE_WORKER_ROLE, ACTION_ROBOT_R01 },
    { {0x04,0x9C,0x36,0x02,0x6C,0x21,0x90}, 7, "Worker C",   ROLE_WORKER_ROLE, ACTION_ROBOT_R02 },
};
#define REG_USERS_COUNT    (sizeof(REG_USERS) / sizeof(REG_USERS[0]))

// ─ WORKER_CARDS ──────────────────────────────────────────────
//  B034105F → INBOUND  모드 전환
//  412E261B → OUTBOUND 모드 전환 (★ 관리자 A UID 와 중복 — 서버 우선 처리)
const WorkerCardEntry WORKER_CARDS_TBL[] = {
    { {0xB0,0x34,0x10,0x5F,0x00,0x00,0x00}, 4, WMODE_INBOUND  },
    { {0x41,0x2E,0x26,0x1B,0x00,0x00,0x00}, 4, WMODE_OUTBOUND },
};
#define WORKER_CARDS_COUNT (sizeof(WORKER_CARDS_TBL) / sizeof(WORKER_CARDS_TBL[0]))

// ─ POT_CARDS ─────────────────────────────────────────────────
//  화분 10개 (049C**026C2190 패턴)
const PotCardEntry POT_CARDS[] = {
    { {0x04,0x9C,0x37,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x36,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x33,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x32,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x31,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x30,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x2F,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x2E,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x2D,0x02,0x6C,0x21,0x90}, 7 },
    { {0x04,0x9C,0x2A,0x02,0x6C,0x21,0x90}, 7 },
};
#define POT_CARDS_COUNT    (sizeof(POT_CARDS) / sizeof(POT_CARDS[0]))

// ── 통신 속도 ────────────────────────────────────────────────
#define BAUD_DEBUG      115200   // USB 시리얼 (Serial Monitor)
#define BAUD_ESP        38400    // SoftwareSerial (ESP32 연결)

// ── SFAM 통신 주기 ───────────────────────────────────────────
#define SENSOR_PERIOD_MS   2000   // SENSOR_BATCH 전송 주기 (2초)
#define CTRL_PERIOD_MS     60000  // CTRL_STATUS_RPT 전송 주기 (60초)
#define RX_TIMEOUT_MS      100    // 패킷 수신 타임아웃 (ms)

// ── 액추에이터 ID (ACTUATOR_CMD actuator_id 필드와 일치) ─────
#define ACT_ID_MODE    0   // 자동 or 수동 모드
#define ACT_ID_PUMP    1   // 펌프
#define ACT_ID_FAN     2   // 팬
#define ACT_ID_HEATER  3   // 히터
#define ACT_ID_LED     4   // LED (state_value = 밝기 %)

// ── 액추에이터 비트마스크 비트 위치 ─────────────────────────
#define ACT_BIT_PUMP    (1 << 0)   // bit0 = 펌프
#define ACT_BIT_FAN     (1 << 1)   // bit1 = 팬
#define ACT_BIT_HEATER  (1 << 2)   // bit2 = 히터
#define ACT_BIT_LED     (1 << 3)   // bit3 = LED

// ============================================================
//  라이브러리 객체 생성
// ============================================================
DHT           dht(PIN_DHT, DHT_TYPE);              // DHT 온습도 센서
SoftwareSerial espSerial(PIN_ESP_RX, PIN_ESP_TX);  // ESP32 통신용 소프트 UART
MFRC522       rfid(PIN_RFID_SS, PIN_RFID_RST);     // RFID-RC522 리더

// ============================================================
//  전역 상태 변수 — 제어
// ============================================================

// ── 동작 모드 ────────────────────────────────────────────────
bool autoMode = false;   // false = 수동(MANUAL), true = 자동(AUTO)

// ── 액추에이터 현재 상태 ─────────────────────────────────────
bool pumpState   = false;  // 펌프 ON/OFF
bool fanState    = false;  // 팬 ON/OFF
bool heaterState = false;  // 히터 ON/OFF
int  ledBright   = 0;      // LED 밝기 (0~100 %)

// ── 실제 센서 측정값 (taskReadSensors() 에서 주기 갱신) ──────
float temperature = NAN;   // 온도 (℃), NAN = 아직 유효값 없음
float humidity    = NAN;   // 습도 (%)
int   lightLevel  = 0;     // 조도 ADC (0~1023)
int   waterLevel  = 0;     // 수위 ADC (0~1023)

// ── 가짜(Fake) 센서 데이터 — 테스트 모드용 ──────────────────
bool  fakeMode        = false;    // true = 가짜 데이터 사용 (기본 ON)
float fakeTemperature = 25.5f;   // 가짜 온도 (℃)
float fakeHumidity    = 60.0f;   // 가짜 습도 (%)
int   fakeLightLevel  = 350;     // 가짜 조도 ADC
int   fakeWaterLevel  = 512;     // 가짜 수위 ADC

// ── 자동 모드 - 펌프 타이머 ──────────────────────────────────
unsigned long pumpInterval  = 3600000UL;  // 가동 간격 기본값: 3600초 (ms)
unsigned long pumpDuration  = 10000UL;   // 1회 가동 시간 기본값: 10초 (ms)
unsigned long pumpLastStart = 0;          // 마지막 펌프 가동 시작 시각
bool          pumpRunning   = false;      // 현재 펌프 가동 중 여부

// ── 자동 모드 - 팬/히터 온도 제어 파라미터 ──────────────────
float tempSetpoint   = 25.0f;  // 기준 온도 (℃)
float tempHysteresis = 4.0f;   // 히스테리시스 총 폭 (℃) → 상하 각 ±2℃

// ── 자동 모드 - 조도 기반 LED 임계값 ────────────────────────
int lightThreshold = 400;   // 이 값 이하이면 LED 점등 (0~1023)

// ── 센서 읽기 주기 관리 ──────────────────────────────────────
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_INTERVAL = 3000;   // 센서 읽기 주기: 3초

// ============================================================
//  전역 상태 변수 — SFAM 통신
// ============================================================

// ── 시퀀스 번호 카운터 (8-bit 롤오버 0x00→0xFF→0x00) ────────
static uint8_t gSeq = 0x00;

// ── 주기 전송 타이머 ─────────────────────────────────────────
static uint32_t lastSensorMs = 0;   // 마지막 SENSOR_BATCH 전송 시각
static uint32_t lastCtrlMs   = 0;   // 마지막 CTRL_STATUS_RPT 전송 시각

// ============================================================
//  수신 상태 머신 변수 (4단계 FSM)
// ============================================================
/*
 *  WAIT_SOF     : 0xAA 마커 대기
 *  READ_HEADER  : 나머지 헤더 5바이트 수집 (MSG_TYPE ~ LEN)
 *  READ_PAYLOAD : LEN 바이트 페이로드 수집
 *  READ_CRC     : CRC16 2바이트 수집 → 검증 → 디스패치
 */
enum RxState { WAIT_SOF, READ_HEADER, READ_PAYLOAD, READ_CRC };

static RxState  rxState      = WAIT_SOF;   // 현재 수신 상태
static uint8_t  rxBuf[PKT_MAX_TOTAL];      // 수신 버퍼 (최대 74 bytes)
static uint8_t  rxIdx        = 0;          // 버퍼 쓰기 인덱스
static uint8_t  rxPayloadLen = 0;          // 현재 패킷 페이로드 길이
static uint32_t rxStartMs    = 0;          // 수신 시작 시각 (타임아웃 판단)

// ============================================================
//  함수 전방 선언
// ============================================================

// ── 센서 ─────────────────────────────────────────────────────
void taskReadSensors();
void printSensorTemp();
void printSensorLight();
void printSensorWater();
void printSensorAll();

// ── RFID ─────────────────────────────────────────────────────
void taskReadRFID();
void printRFIDCardData();
void printRFIDPageUL(byte pageNum, const char *label);   // Ultralight 페이지 읽기

// ── RFID 카드 레지스트리 조회 ────────────────────────────────
bool         uidMatches(const uint8_t *regUid, uint8_t regLen,
                        const uint8_t *cardUid, uint8_t cardLen);
int          lookupRegUser(const uint8_t *uid, uint8_t len);
int          lookupWorkerCard(const uint8_t *uid, uint8_t len);
int          lookupPotCard(const uint8_t *uid, uint8_t len);
void         printRegisteredCardInfo(const uint8_t *uid, uint8_t len);
void         printCardRegistry();
void         cmdRfidScan();

// ── 가짜 데이터 ──────────────────────────────────────────────
void printFakeStatus();

// ── 액추에이터 ───────────────────────────────────────────────
void controlPump(bool state);
void controlFan(bool state);
void controlHeater(bool state);
void controlLED(int brightness);
uint8_t getActuatorBitmask();   // ★ 통합 추가: 실제 상태 → 비트마스크 계산

// ── 자동 모드 태스크 ─────────────────────────────────────────
void taskAutoPump();
void taskAutoFanHeater(float temp);
void taskAutoLED(int light);

// ── 상태/도움말 출력 ─────────────────────────────────────────
void printStatus();
void printHelp();

// ── 명령어 처리 ──────────────────────────────────────────────
void processCommand(String raw);

// ── SFAM 통신 ────────────────────────────────────────────────
void rxStateMachine(uint8_t b);
void dispatchPacket(const uint8_t *buf, uint8_t total_len, const PktHeader &hdr);
void handleHeartbeatReq(const PktHeader &hdr);
void handleActuatorCmd(const PktHeader &hdr, const uint8_t *payload);
void handleAgvTaskCmd(const PktHeader &hdr, const uint8_t *payload);
void sendSensorBatch();
void sendCtrlStatusRpt();
void sendPacket(const uint8_t *buf, uint8_t len);
void debugPrintPacket(const char *dir, const uint8_t *buf, uint8_t len);

// ============================================================
//  setup()
// ============================================================
void setup() {
    // ── USB 시리얼 (Serial Monitor 디버그) ───────────────────
    Serial.begin(BAUD_DEBUG);

    // ── ESP32 SoftwareSerial 초기화 ──────────────────────────
    // 38400 bps: Arduino Uno 16MHz 에서 안정적으로 동작하는 최대 속도
    // ESP32 Serial2 도 반드시 38400 bps 로 맞춰야 함
    espSerial.begin(BAUD_ESP);

    // ── DHT 센서 초기화 ──────────────────────────────────────
    dht.begin();

    // ── SPI 버스 및 RFID-RC522 초기화 ────────────────────────
    SPI.begin();
    rfid.PCD_Init();

    // ── 출력 핀 모드 설정 ────────────────────────────────────
    // 아날로그 입력(A0~A3)은 기본이 INPUT 이므로 별도 설정 불필요
    pinMode(PIN_PUMP,   OUTPUT);
    pinMode(PIN_FAN,    OUTPUT);
    pinMode(PIN_HEATER, OUTPUT);
    pinMode(PIN_LED_R,  OUTPUT);
    pinMode(PIN_LED_G,  OUTPUT);
    pinMode(PIN_LED_B,  OUTPUT);

    // ── 전원 인가 시 모든 액추에이터 OFF 초기화 ──────────────
    // 의도치 않은 동작 방지 (릴레이·MOSFET 초기 상태 불명확)
    controlPump(false);
    controlFan(false);
    controlHeater(false);
    controlLED(0);

    // DHT 센서 안정화 대기 (최소 500ms 필요, 데이터시트 권고)
    delay(500);

    // ── 시작 메시지 출력 ─────────────────────────────────────
    Serial.println(F("=================================================="));
    Serial.println(F(" Arduino SFAM 통합 펌웨어 (제어 + 통신)"));
    Serial.print(F(" 장치 ID  : 0x")); Serial.println(MY_DEV_ID, HEX);
    Serial.println(F(" Fake 모드: ON (가짜 센서 데이터 사용 중)"));
    Serial.println(F("=================================================="));

    // 명령어 도움말 자동 출력
    printHelp();
}

// ============================================================
//  loop()  — 논블로킹 멀티태스크 구조 (delay 미사용)
// ============================================================
void loop() {

    // ════════════════════════════════════════════════════════
    //  [제어 태스크]
    // ════════════════════════════════════════════════════════

    // ── 태스크 1: 실제 센서 주기 읽기 (3초마다) ──────────────
    taskReadSensors();

    // ── 태스크 2: RFID 카드 감지 (폴링, 카드 없으면 즉시 복귀) ─
    taskReadRFID();

    // ── 태스크 3: USB 시리얼 명령 수신 및 처리 ───────────────
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');  // 개행까지 한 줄 읽기
        cmd.trim();                                  // 앞뒤 공백·개행 제거
        if (cmd.length() > 0) processCommand(cmd);
    }

    // ── 태스크 4: 자동 모드 액추에이터 제어 ──────────────────
    if (autoMode) {
        // 제어에 사용할 센서값 선택: fakeMode=true → 가짜, false → 실제
        float activeTemp  = fakeMode ? fakeTemperature : temperature;
        int   activeLight = fakeMode ? fakeLightLevel  : lightLevel;

        taskAutoPump();               // 타이머 기반 펌프 제어
        taskAutoFanHeater(activeTemp);// 히스테리시스 팬/히터 제어
        taskAutoLED(activeLight);     // 조도 반비례 LED 밝기 제어
    }

    // ════════════════════════════════════════════════════════
    //  [통신 태스크]
    // ════════════════════════════════════════════════════════

    // ── 태스크 5: SoftSerial 수신 바이트 → FSM 공급 ──────────
    while (espSerial.available()) {
        rxStateMachine((uint8_t)espSerial.read());
    }

    // ── 태스크 6: 수신 타임아웃 감지 → 상태 초기화 ──────────
    // 패킷 수신 중 연결이 끊기면 FSM이 멈춤 → 강제 초기화
    if (rxState != WAIT_SOF) {
        if (millis() - rxStartMs > RX_TIMEOUT_MS) {
            Serial.println(F("[RX] 타임아웃 → 수신 상태 초기화"));
            rxState = WAIT_SOF;
            rxIdx   = 0;
        }
    }

    // ── 태스크 7: 5초마다 SENSOR_BATCH 전송 ──────────────────
    if (millis() - lastSensorMs >= SENSOR_PERIOD_MS) {
        lastSensorMs = millis();
        sendSensorBatch();
    }

    // ── 태스크 8: 60초마다 CTRL_STATUS_RPT 전송 ──────────────
    if (millis() - lastCtrlMs >= CTRL_PERIOD_MS) {
        lastCtrlMs = millis();
        sendCtrlStatusRpt();
    }
}

// ============================================================
//  ─── 센서 함수 모음 ──────────────────────────────────────
// ============================================================

/*
 * taskReadSensors()
 * 역할 : SENSOR_INTERVAL(3초) 마다 DHT·조도·수위를 읽어 전역 변수에 갱신
 * 방식 : millis() 비교 논블로킹 타이머
 * 참고 : fakeMode 여부와 무관하게 항상 실행 (실제 값도 병행 유지)
 */
void taskReadSensors() {
    unsigned long now = millis();
    if (now - lastSensorRead < SENSOR_INTERVAL) return;
    lastSensorRead = now;

    // DHT 온도·습도 읽기 (실패 시 NAN 반환 → 이전 유효값 유지)
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) temperature = t;
    if (!isnan(h)) humidity    = h;

    // 아날로그 센서 읽기
    lightLevel = analogRead(PIN_LIGHT);
    waterLevel = analogRead(PIN_WATER);
}

/*
 * printSensorTemp() / printSensorLight() / printSensorWater() / printSensorAll()
 * 역할 : 실제 센서 측정값을 Serial Monitor 에 출력
 */
void printSensorTemp() {
    Serial.print(F("[TEMP/HUM] "));
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println(F("DHT 읽기 오류 (센서 연결 확인)"));
    } else {
        Serial.print(F("온도: "));    Serial.print(temperature, 1); Serial.print(F(" °C  |  습도: "));
        Serial.print(humidity, 1);    Serial.println(F(" %"));
    }
}

void printSensorLight() {
    int pct = map(lightLevel, 0, LIGHT_MAX_RANGE, 0, 100);
    Serial.print(F("[LIGHT]    조도 RAW: ")); Serial.print(lightLevel);
    Serial.print(F("  /  환산: "));           Serial.print(pct); Serial.println(F(" %"));
}

void printSensorWater() {
    int pct = map(waterLevel, 0, WATER_MAX_RANGE, 0, 100);
    Serial.print(F("[WATER]    수위 RAW: ")); Serial.print(waterLevel);
    Serial.print(F("  /  환산: "));           Serial.print(pct); Serial.println(F(" %"));
}

void printSensorAll() {
    Serial.println(F("========== 전체 센서 데이터 =========="));
    printSensorTemp();
    printSensorLight();
    printSensorWater();
    Serial.println(F("======================================"));
}

// ============================================================
//  ─── RFID 함수 모음 ──────────────────────────────────────
// ============================================================

/*
 * taskReadRFID()
 * 역할 : RFID 리더에 카드가 감지되면 모드에 따라 처리
 *
 *  ▶ 자동 모드(AUTO) :
 *    - UID·블록 데이터 전체 자동 출력
 *    - 등록 카드 레지스트리 자동 조회 후 Serial 출력
 *      (REGISTERED_USERS / WORKER_CARDS / POT_CARDS 순 검색)
 *
 *  ▶ 수동 모드(MANUAL) :
 *    - UID 간략 알림만 출력 → 상세 정보는 명령어로 조회
 *    - 'RFID SCAN' 명령 → 카드 스캔 + 전체 정보 출력
 *    - 'RFID LIST' 명령 → 등록 카드 레지스트리 목록 출력
 *
 * 방식 : 폴링 (카드 없으면 즉시 복귀, loop() 블로킹 없음)
 */
void taskReadRFID() {
    if (!rfid.PICC_IsNewCardPresent()) return;
    if (!rfid.PICC_ReadCardSerial())   return;

    if (autoMode) {
        // ── 자동 모드: 전체 카드 정보 자동 출력 ───────────────
        printRFIDCardData();
        printRegisteredCardInfo(rfid.uid.uidByte, rfid.uid.size);
    } else {
        // ── 수동 모드: UID 간략 알림만 출력 ──────────────────
        Serial.print(F("[RFID] 카드 감지 UID="));
        for (byte i = 0; i < rfid.uid.size; i++) {
            if (rfid.uid.uidByte[i] < 0x10) Serial.print(F("0"));
            Serial.print(rfid.uid.uidByte[i], HEX);
        }
        Serial.println(F("  → 상세 정보: RFID SCAN"));
    }

    rfid.PICC_HaltA();        // 카드 HALT 상태 전환 (재감지 방지)
    rfid.PCD_StopCrypto1();   // 암호화 세션 종료
}

/*
 * printRFIDCardData()
 * 역할 : RFID 카드 UID + 카드 타입 출력 후 Ultralight 페이지 단위로 SFAM 데이터 읽기
 *
 *  MIFARE Ultralight 읽기 특성:
 *    - Key A 인증 불필요 (암호화 없음)
 *    - 페이지 0~3  : 제조사 고정 영역 (UID, Lock, OTP) → 참고용 출력
 *    - 페이지 4~8  : SFAM 사용자 데이터 영역
 */
void printRFIDCardData() {
    Serial.println(F(""));
    Serial.println(F("========== RFID 카드 감지 =========="));

    // ── UID 출력 ─────────────────────────────────────────────
    Serial.print(F("[UID] "));
    for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) Serial.print(F("0"));
        Serial.print(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1) Serial.print(F(":"));
    }
    Serial.println();

    // ── 카드 타입 출력 ───────────────────────────────────────
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    Serial.print(F("[카드 타입] "));
    Serial.println(rfid.PICC_GetTypeName(piccType));

    // ── 제조사 영역 (페이지 0~3) ─────────────────────────────
    Serial.println(F("  ─ [제조사 영역 / 읽기 전용] ─────────────"));
    printRFIDPageUL(0, "UID part1 ");
    printRFIDPageUL(1, "UID part2 ");
    printRFIDPageUL(2, "Lock/Int  ");
    printRFIDPageUL(3, "OTP       ");

    // ── SFAM 사용자 영역 (페이지 4~8) ───────────────────────
    Serial.println(F("  ─ [SFAM 사용자 영역] ────────────────────"));
    printRFIDPageUL(RFID_PAGE_UL_CARDINFO,    "카드정보  ");
    printRFIDPageUL(RFID_PAGE_UL_CROP,        "작물정보  ");
    printRFIDPageUL(RFID_PAGE_UL_CTRL_PARAM,  "제어파라미");
    printRFIDPageUL(RFID_PAGE_UL_PUMP_PARAM,  "펌프파라미");
    printRFIDPageUL(RFID_PAGE_UL_LIGHT_PARAM, "조도파라미");

    Serial.println(F("====================================="));
    Serial.println(F(""));
}

/*
 * printRFIDPageUL(pageNum, label)
 * 역할 : MIFARE Ultralight 카드의 지정 페이지를 인증 없이 읽어 출력
 *
 * Ultralight 읽기 특성:
 *   - PCD_Authenticate 불필요 (암호화 없음)
 *   - MIFARE_Read(page) 는 page 부터 4페이지(16 bytes) 반환
 *   - SFAM 메모리 맵상 페이지 1개 = 4 bytes 데이터로 취급
 *   → 따라서 buffer[0..3] 만 해당 페이지의 실제 데이터
 *     (buffer[4..15] 는 다음 페이지들 — 이 함수에서는 표시 안 함)
 *
 * 출력 형식:
 *   [페이지 04] 카드정보  : HEX[ 01 02 03 04 ]  TXT[ .... ]
 */
void printRFIDPageUL(byte pageNum, const char *label) {
    byte buffer[18];
    byte bufSize = 18;

    Serial.print(F("  [페이지 "));
    if (pageNum < 10) Serial.print(F("0"));
    Serial.print(pageNum);
    Serial.print(F("] "));
    Serial.print(label);
    Serial.print(F(": "));

    // Ultralight 는 인증 없이 바로 MIFARE_Read 호출
    MFRC522::StatusCode readStatus = rfid.MIFARE_Read(pageNum, buffer, &bufSize);
    if (readStatus != MFRC522::STATUS_OK) {
        Serial.print(F("읽기 실패 ("));
        Serial.print(rfid.GetStatusCodeName(readStatus));
        Serial.println(F(")"));
        return;
    }

    // HEX: 해당 페이지 4 bytes만 표시 (buffer[0..3])
    Serial.print(F("HEX[ "));
    for (byte i = 0; i < 4; i++) {
        if (buffer[i] < 0x10) Serial.print(F("0"));
        Serial.print(buffer[i], HEX);
        Serial.print(F(" "));
    }
    Serial.print(F("]  TXT[ "));

    // ASCII: 출력 불가 문자는 '.'
    for (byte i = 0; i < 4; i++) {
        if (buffer[i] >= 0x20 && buffer[i] <= 0x7E)
            Serial.print((char)buffer[i]);
        else
            Serial.print(F("."));
    }
    Serial.println(F(" ]"));
}
// ============================================================

/*
 * uidMatches(regUid, regLen, cardUid, cardLen)
 * 역할 : 등록 테이블 UID 와 스캔된 카드 UID 바이트 단위 비교
 * 반환 : 일치 시 true
 */
bool uidMatches(const uint8_t *regUid, uint8_t regLen,
                const uint8_t *cardUid, uint8_t cardLen) {
    if (regLen != cardLen) return false;
    for (uint8_t i = 0; i < regLen; i++) {
        if (regUid[i] != cardUid[i]) return false;
    }
    return true;
}

/*
 * lookupRegUser / lookupWorkerCard / lookupPotCard
 * 역할 : 각 등록 테이블에서 UID 검색
 * 반환 : 테이블 인덱스 (0 이상), 미등록 시 -1
 */
int lookupRegUser(const uint8_t *uid, uint8_t len) {
    for (uint8_t i = 0; i < REG_USERS_COUNT; i++) {
        if (uidMatches(REG_USERS[i].uid, REG_USERS[i].uidLen, uid, len))
            return (int)i;
    }
    return -1;
}

int lookupWorkerCard(const uint8_t *uid, uint8_t len) {
    for (uint8_t i = 0; i < WORKER_CARDS_COUNT; i++) {
        if (uidMatches(WORKER_CARDS_TBL[i].uid, WORKER_CARDS_TBL[i].uidLen, uid, len))
            return (int)i;
    }
    return -1;
}

int lookupPotCard(const uint8_t *uid, uint8_t len) {
    for (uint8_t i = 0; i < POT_CARDS_COUNT; i++) {
        if (uidMatches(POT_CARDS[i].uid, POT_CARDS[i].uidLen, uid, len))
            return (int)i;
    }
    return -1;
}

/*
 * printRegisteredCardInfo(uid, len)
 * 역할 : 스캔된 UID 를 3개 테이블에서 검색하여 등록 정보를 Serial 출력
 *        (rfid_handler.py 의 REGISTERED_USERS / WORKER_CARDS / POT_CARDS 대응)
 *
 *  출력 예시 (등록 사용자):
 *    ─ 등록 카드 정보 ──
 *    [카드 종류] REGISTERED_USERS (사용자 인증)
 *    [이  름]    Admin A
 *    [역  할]    admin (관리자)
 *    [액  션]    open_door (출입문 개방)
 *
 *  출력 예시 (미등록):
 *    [카드 종류] *** 미등록 카드 ***
 */
void printRegisteredCardInfo(const uint8_t *uid, uint8_t len) {
    Serial.println(F("  ─ 등록 카드 레지스트리 조회 ────────────"));

    int uIdx = lookupRegUser(uid, len);
    int wIdx = lookupWorkerCard(uid, len);
    int pIdx = lookupPotCard(uid, len);

    bool found = false;

    // ── REGISTERED_USERS 결과 ──────────────────────────────
    if (uIdx >= 0) {
        found = true;
        Serial.println(F("  [카드 종류] REGISTERED_USERS (사용자 인증)"));
        Serial.print(F("  [이  름]    ")); Serial.println(REG_USERS[uIdx].name);
        Serial.print(F("  [역  할]    "));
        switch (REG_USERS[uIdx].role) {
            case ROLE_ADMIN:       Serial.println(F("admin (관리자)")); break;
            case ROLE_WORKER_ROLE: Serial.println(F("worker (작업자)")); break;
            default:               Serial.println(F("unknown")); break;
        }
        Serial.print(F("  [액  션]    "));
        switch (REG_USERS[uIdx].action) {
            case ACTION_OPEN_DOOR: Serial.println(F("open_door (출입문 개방)")); break;
            case ACTION_ROBOT_R01: Serial.println(F("call_robot_R01 (AGV R01 호출)")); break;
            case ACTION_ROBOT_R02: Serial.println(F("call_robot_R02 (AGV R02 호출)")); break;
            default:               Serial.println(F("unknown")); break;
        }
    }

    // ── WORKER_CARDS 결과 ────────────────────────────────────
    if (wIdx >= 0) {
        found = true;
        if (uIdx >= 0) Serial.println(F("  ※ 복수 테이블 등록 카드 (UID 중복 주의)"));
        Serial.println(F("  [카드 종류] WORKER_CARDS (스테이션 모드 전환)"));
        Serial.print(F("  [모  드]    "));
        switch (WORKER_CARDS_TBL[wIdx].mode) {
            case WMODE_INBOUND:  Serial.println(F("INBOUND  → 입고 모드 전환")); break;
            case WMODE_OUTBOUND: Serial.println(F("OUTBOUND → 출고 모드 전환")); break;
            default:             Serial.println(F("unknown")); break;
        }
    }

    // ── POT_CARDS 결과 ───────────────────────────────────────
    if (pIdx >= 0) {
        found = true;
        Serial.println(F("  [카드 종류] POT_CARDS (화분·트레이)"));
        Serial.print(F("  [인  덱 스] "));
        Serial.print(pIdx + 1); Serial.print(F(" / ")); Serial.println(POT_CARDS_COUNT);
    }

    // ── 미등록 카드 ─────────────────────────────────────────
    if (!found) {
        Serial.println(F("  [카드 종류] *** 미등록 카드 ***"));
        Serial.println(F("  → 서버(rfid_handler.py) 에 카드 등록이 필요합니다."));
    }

    Serial.println(F("  ─────────────────────────────────────────"));
}

/*
 * printCardRegistry()
 * 역할 : Arduino 에 내장된 3개 카드 레지스트리 전체 목록을 출력
 *        명령어: RFID LIST
 */
void printCardRegistry() {
    Serial.println(F(""));
    Serial.println(F("========== RFID 등록 카드 레지스트리 =========="));

    // ── REGISTERED_USERS ───────────────────────────────────
    Serial.print(F("[REGISTERED_USERS]  총 ")); Serial.print(REG_USERS_COUNT); Serial.println(F("개"));
    Serial.println(F("  번호  UID              이름       역할    액션"));
    for (uint8_t i = 0; i < REG_USERS_COUNT; i++) {
        Serial.print(F("  #")); Serial.print(i + 1); Serial.print(F("    "));
        for (uint8_t j = 0; j < REG_USERS[i].uidLen; j++) {
            if (REG_USERS[i].uid[j] < 0x10) Serial.print(F("0"));
            Serial.print(REG_USERS[i].uid[j], HEX);
        }
        // UID 길이 보정 (4B=8자리, 7B=14자리 기준 정렬)
        if (REG_USERS[i].uidLen == 4) Serial.print(F("       "));
        else                          Serial.print(F(" "));
        Serial.print(REG_USERS[i].name); Serial.print(F("  "));
        switch (REG_USERS[i].role) {
            case ROLE_ADMIN:       Serial.print(F("admin   ")); break;
            case ROLE_WORKER_ROLE: Serial.print(F("worker  ")); break;
        }
        switch (REG_USERS[i].action) {
            case ACTION_OPEN_DOOR: Serial.println(F("open_door")); break;
            case ACTION_ROBOT_R01: Serial.println(F("call_robot_R01")); break;
            case ACTION_ROBOT_R02: Serial.println(F("call_robot_R02")); break;
        }
    }

    // ── WORKER_CARDS ───────────────────────────────────────
    Serial.println(F(""));
    Serial.print(F("[WORKER_CARDS]  총 ")); Serial.print(WORKER_CARDS_COUNT); Serial.println(F("개"));
    Serial.println(F("  번호  UID              모드"));
    for (uint8_t i = 0; i < WORKER_CARDS_COUNT; i++) {
        Serial.print(F("  #")); Serial.print(i + 1); Serial.print(F("    "));
        for (uint8_t j = 0; j < WORKER_CARDS_TBL[i].uidLen; j++) {
            if (WORKER_CARDS_TBL[i].uid[j] < 0x10) Serial.print(F("0"));
            Serial.print(WORKER_CARDS_TBL[i].uid[j], HEX);
        }
        if (WORKER_CARDS_TBL[i].uidLen == 4) Serial.print(F("       "));
        else                                  Serial.print(F(" "));
        switch (WORKER_CARDS_TBL[i].mode) {
            case WMODE_INBOUND:  Serial.println(F("INBOUND  (입고 모드 전환)")); break;
            case WMODE_OUTBOUND: Serial.println(F("OUTBOUND (출고 모드 전환)")); break;
        }
    }

    // ── POT_CARDS ──────────────────────────────────────────
    Serial.println(F(""));
    Serial.print(F("[POT_CARDS]  총 ")); Serial.print(POT_CARDS_COUNT); Serial.println(F("개"));
    Serial.println(F("  번호  UID"));
    for (uint8_t i = 0; i < POT_CARDS_COUNT; i++) {
        Serial.print(F("  #")); 
        if (i + 1 < 10) Serial.print(F("0"));
        Serial.print(i + 1); Serial.print(F("   "));
        for (uint8_t j = 0; j < POT_CARDS[i].uidLen; j++) {
            if (POT_CARDS[i].uid[j] < 0x10) Serial.print(F("0"));
            Serial.print(POT_CARDS[i].uid[j], HEX);
        }
        Serial.println();
    }

    Serial.println(F(""));
    Serial.println(F("================================================"));
    Serial.println(F(""));
}

/*
 * cmdRfidScan()
 * 역할 : 수동 모드에서 'RFID SCAN' 명령 실행 시 카드 1회 스캔
 *        최대 3초간 카드를 기다린 뒤 성공 시 전체 정보 출력
 *
 *  출력 순서:
 *    1. UID + 카드 타입 + SFAM 정의 블록 데이터
 *    2. 등록 카드 레지스트리 조회 결과
 */
void cmdRfidScan() {
    Serial.println(F("[RFID SCAN] 카드를 리더에 태그하세요... (3초 대기)"));
    unsigned long deadline = millis() + 3000UL;
    while (millis() < deadline) {
        if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
            printRFIDCardData();
            printRegisteredCardInfo(rfid.uid.uidByte, rfid.uid.size);
            rfid.PICC_HaltA();
            rfid.PCD_StopCrypto1();
            return;
        }
    }
    Serial.println(F("[RFID SCAN] 카드가 감지되지 않았습니다. 다시 시도하세요."));
}

/*
 * printFakeStatus()
 * 역할 : 가짜 데이터 모드 상태 및 설정값 출력
 */
void printFakeStatus() {
    Serial.println(F("========== 가짜 데이터 상태 =========="));
    Serial.print(F("  Fake 모드 : "));
    Serial.println(fakeMode ? F("ON (가짜 데이터 사용 중)") : F("OFF (실제 센서 사용 중)"));
    Serial.println(F("  ─ 설정된 가짜 값 ────────────────────"));
    Serial.print(F("  온도  : ")); Serial.print(fakeTemperature, 1); Serial.println(F(" °C"));
    Serial.print(F("  습도  : ")); Serial.print(fakeHumidity, 1);    Serial.println(F(" %"));
    Serial.print(F("  조도  : ")); Serial.print(fakeLightLevel);
    Serial.print(F(" RAW  /  환산: ")); Serial.print(map(fakeLightLevel, 0, 1023, 0, 100)); Serial.println(F(" %"));
    Serial.print(F("  수위  : ")); Serial.print(fakeWaterLevel);
    Serial.print(F(" RAW  /  환산: ")); Serial.print(map(fakeWaterLevel, 0, 1023, 0, 100)); Serial.println(F(" %"));
    Serial.println(F("  ─ 현재 제어에 사용 중인 값 ──────────"));
    Serial.print(F("  온도 (제어용) : ")); Serial.print(fakeMode ? fakeTemperature : temperature, 1);
    Serial.println(fakeMode ? F(" °C [FAKE]") : F(" °C [REAL]"));
    Serial.print(F("  조도 (제어용) : ")); Serial.print(fakeMode ? fakeLightLevel : lightLevel);
    Serial.println(fakeMode ? F(" RAW [FAKE]") : F(" RAW [REAL]"));
    Serial.println(F("======================================"));
}

// ============================================================
//  ─── 액추에이터 제어 함수 모음 ───────────────────────────
// ============================================================

/*
 * controlPump / controlFan / controlHeater
 * 역할 : MOSFET 게이트 제어 → 상태 변수 갱신 → Serial 출력
 * 인자 : state = true(ON) / false(OFF)
 */
void controlPump(bool state) {
    pumpState = state;
    digitalWrite(PIN_PUMP, state ? HIGH : LOW);
    Serial.print(F("[PUMP]   ")); Serial.println(state ? F("ON") : F("OFF"));
}

void controlFan(bool state) {
    fanState = state;
    digitalWrite(PIN_FAN, state ? HIGH : LOW);
    Serial.print(F("[FAN]    ")); Serial.println(state ? F("ON") : F("OFF"));
}

void controlHeater(bool state) {
    heaterState = state;
    digitalWrite(PIN_HEATER, state ? HIGH : LOW);
    Serial.print(F("[HEATER] ")); Serial.println(state ? F("ON") : F("OFF"));
}

/*
 * controlLED(brightness)
 * 역할 : RGB LED를 흰색(R=G=B)으로 0~100% 밝기 PWM 제어
 * 인자 : brightness = 밝기 % (0=소등, 100=최대)
 */
void controlLED(int brightness) {
    ledBright   = constrain(brightness, 0, 100);
    int pwm     = map(ledBright, 0, 100, 0, 255);  // % → PWM(0~255) 변환
    analogWrite(PIN_LED_R, pwm);
    analogWrite(PIN_LED_G, pwm);
    analogWrite(PIN_LED_B, pwm);
    Serial.print(F("[LED]    흰색 밝기: ")); Serial.print(ledBright); Serial.println(F(" %"));
}

/*
 * getActuatorBitmask()
 * 역할 : 현재 실제 액추에이터 ON/OFF 상태를 비트마스크로 변환
 *        CTRL_STATUS_RPT 의 actuator_bitmask 필드에 사용
 * 반환 : bit0=펌프, bit1=팬, bit2=히터, bit3=LED(켜짐 여부)
 *
 * ★ 통합 핵심: 하드코딩 대신 실제 상태 변수를 반영하여
 *    SFAM 서버가 항상 정확한 액추에이터 상태를 파악할 수 있게 함
 */
uint8_t getActuatorBitmask() {
    uint8_t mask = 0x00;
    if (pumpState)      mask |= ACT_BIT_PUMP;    // bit0
    if (fanState)       mask |= ACT_BIT_FAN;     // bit1
    if (heaterState)    mask |= ACT_BIT_HEATER;  // bit2
    if (ledBright > 0)  mask |= ACT_BIT_LED;     // bit3
    return mask;
}

// ============================================================
//  ─── 자동 모드 태스크 함수 모음 ──────────────────────────
// ============================================================

/*
 * taskAutoPump()
 * 역할 : pumpInterval 마다 pumpDuration 동안 펌프 가동 (타이머 기반)
 * 방식 : millis() 기반 논블로킹 (delay 미사용)
 */
void taskAutoPump() {
    unsigned long now = millis();
    if (pumpRunning) {
        // 가동 시간 초과 → 펌프 OFF
        if (now - pumpLastStart >= pumpDuration) {
            controlPump(false);
            pumpRunning = false;
        }
    } else {
        // 간격 초과 또는 최초 실행 → 펌프 ON
        if (pumpLastStart == 0 || (now - pumpLastStart >= pumpInterval)) {
            controlPump(true);
            pumpRunning   = true;
            pumpLastStart = now;
        }
    }
}

/*
 * taskAutoFanHeater(temp)
 * 역할 : 3구간 히스테리시스로 팬/히터 자동 ON/OFF
 *
 *  온도축: ──[lowerBand]──────[upperBand]──▶
 *  구간  :  히터ON,팬OFF │ 둘다OFF │ 팬ON,히터OFF
 *
 *  temp < lowerBand  → 히터 ON, 팬 OFF
 *  lowerBand~upper   → 팬 OFF, 히터 OFF (쾌적 데드밴드)
 *  temp > upperBand  → 팬 ON,  히터 OFF
 *
 * 예: Setpoint=25, Hyst=4 → lowerBand=23, upperBand=27
 */
void taskAutoFanHeater(float temp) {
    if (isnan(temp)) return;   // NaN이면 제어 중단

    float upperBand = tempSetpoint + tempHysteresis / 2.0f;
    float lowerBand = tempSetpoint - tempHysteresis / 2.0f;

    if (temp > upperBand) {
        // 과열 → 팬 ON, 히터 OFF
        if (!fanState)   controlFan(true);
        if (heaterState) controlHeater(false);
    } else if (temp < lowerBand) {
        // 저온 → 히터 ON, 팬 OFF
        if (fanState)     controlFan(false);
        if (!heaterState) controlHeater(true);
    } else {
        // 쾌적 데드밴드 → 팬·히터 모두 OFF (불필요한 전력 소모 방지)
        if (fanState)    controlFan(false);
        if (heaterState) controlHeater(false);
    }
}

/*
 * taskAutoLED(light)
 * 역할 : 조도에 반비례하여 LED 밝기 자동 조절
 *   light >= lightThreshold → LED 0% (충분히 밝음)
 *   light → 0 (완전 어둠)   → LED 100%
 *   2% 미만 변화는 controlLED 호출 생략 (Serial 과다 출력 방지)
 */
void taskAutoLED(int light) {
    int newBright;
    if (light >= lightThreshold) {
        newBright = 0;
    } else {
        newBright = map(light, lightThreshold, 0, 0, 100);
        newBright = constrain(newBright, 0, 100);
    }
    if (abs(newBright - ledBright) >= 2) {
        controlLED(newBright);
    }
}

// ============================================================
//  ─── 전체 상태 출력 / 도움말 ────────────────────────────
// ============================================================

void printStatus() {
    Serial.println(F("========== 현재 시스템 상태 =========="));
    Serial.print(F("  동작 모드  : ")); Serial.println(autoMode ? F("AUTO (자동)") : F("MANUAL (수동)"));
    Serial.print(F("  Fake 모드  : ")); Serial.println(fakeMode ? F("ON  ← 가짜 데이터 사용") : F("OFF ← 실제 센서 사용"));
    Serial.println(F("  ─ 액추에이터 ─────────────────────"));
    Serial.print(F("  PUMP    : ")); Serial.println(pumpState   ? F("ON") : F("OFF"));
    Serial.print(F("  FAN     : ")); Serial.println(fanState    ? F("ON") : F("OFF"));
    Serial.print(F("  HEATER  : ")); Serial.println(heaterState ? F("ON") : F("OFF"));
    Serial.print(F("  LED     : ")); Serial.print(ledBright); Serial.println(F(" %"));
    Serial.println(F("  ─ 실제 센서 값 ───────────────────"));
    printSensorTemp();
    printSensorLight();
    printSensorWater();
    Serial.println(F("  ─ 자동 모드 파라미터 ─────────────"));
    Serial.print(F("  펌프 간격 : ")); Serial.print(pumpInterval / 1000UL);   Serial.println(F(" 초"));
    Serial.print(F("  펌프 시간 : ")); Serial.print(pumpDuration / 1000UL);   Serial.println(F(" 초"));
    Serial.print(F("  기준 온도 : ")); Serial.print(tempSetpoint, 1);         Serial.println(F(" °C"));
    Serial.print(F("  히스테리시스: ±")); Serial.print(tempHysteresis/2.0f,1); Serial.println(F(" °C"));
    Serial.print(F("  조도 임계값 : ")); Serial.println(lightThreshold);
    Serial.println(F("======================================"));
}

void printHelp() {
    Serial.println(F(""));
    Serial.println(F("╔════════════════════════════════════════╗"));
    Serial.println(F("║   SFAM Controller  -  Arduino Uno      ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [모드]  MODE MANUAL  /  MODE AUTO     ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [수동]  PUMP ON/OFF                   ║"));
    Serial.println(F("║          FAN ON/OFF                    ║"));
    Serial.println(F("║          HEATER ON/OFF                 ║"));
    Serial.println(F("║          LED <0~100>                   ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [센서]  SENSOR TEMP / LIGHT / WATER   ║"));
    Serial.println(F("║          SENSOR ALL                    ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [RFID]  RFID SCAN  (카드 1회 스캔)   ║"));
    Serial.println(F("║          RFID LIST  (등록 카드 목록)   ║"));
    Serial.println(F("║  ※ AUTO 모드: 카드 감지 시 자동 출력  ║"));
    Serial.println(F("║  ※ MANUAL  : RFID SCAN 명령 사용      ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [설정]  SET PUMP INTERVAL <초>        ║"));
    Serial.println(F("║          SET PUMP DURATION <초>        ║"));
    Serial.println(F("║          SET TEMP <℃>                 ║"));
    Serial.println(F("║          SET HYST <℃>                 ║"));
    Serial.println(F("║          SET LIGHT <0~1023>            ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║  [FAKE]  FAKE ON / FAKE OFF            ║"));
    Serial.println(F("║          FAKE TEMP/HUM/LIGHT/WATER <값>║"));
    Serial.println(F("║          FAKE STATUS                   ║"));
    Serial.println(F("╠════════════════════════════════════════╣"));
    Serial.println(F("║          STATUS  /  HELP               ║"));
    Serial.println(F("╚════════════════════════════════════════╝"));
    Serial.println(F(""));
}

// ============================================================
//  ─── 명령어 파싱 및 처리 ─────────────────────────────────
// ============================================================

/*
 * processCommand(raw)
 * 역할 : USB 시리얼로 수신된 명령 문자열을 파싱하여 동작 실행
 * 방식 : 대소문자 구분 없이 처리 (toUpperCase() 사용)
 *        소수점 파싱이 필요한 경우 원본 raw 에서 직접 읽음
 */
void processCommand(String raw) {
    String cmd = raw;
    cmd.toUpperCase();

    // ── 모드 전환 ────────────────────────────────────────────
    if (cmd == F("MODE MANUAL")) {
        autoMode = false;
        Serial.println(F("[MODE] 수동(MANUAL) 모드 전환"));
        // 모드 변경 즉시 서버에 상태 보고
        sendCtrlStatusRpt();
        return;
    }
    if (cmd == F("MODE AUTO")) {
        autoMode      = true;
        pumpLastStart = 0;      // 0 초기화 → 즉시 첫 펌프 가동 허용
        pumpRunning   = false;
        Serial.println(F("[MODE] 자동(AUTO) 모드 전환"));
        sendCtrlStatusRpt();
        return;
    }

    // ── 상태·도움말 ──────────────────────────────────────────
    if (cmd == F("STATUS")) { printStatus(); return; }
    if (cmd == F("HELP"))   { printHelp();   return; }

    // ── 수동 액추에이터 제어 (MANUAL 모드에서만 허용) ────────
    if (cmd == F("PUMP ON")   || cmd == F("PUMP OFF")   ||
        cmd == F("FAN ON")    || cmd == F("FAN OFF")    ||
        cmd == F("HEATER ON") || cmd == F("HEATER OFF") ||
        cmd.startsWith(F("LED "))) {

        if (autoMode) {
            Serial.println(F("[ERR] 자동 모드에서는 수동 제어 불가. MODE MANUAL 로 전환하세요."));
            return;
        }
        if (cmd == F("PUMP ON"))    { controlPump(true);                     return; }
        if (cmd == F("PUMP OFF"))   { controlPump(false);                    return; }
        if (cmd == F("FAN ON"))     { controlFan(true);                      return; }
        if (cmd == F("FAN OFF"))    { controlFan(false);                     return; }
        if (cmd == F("HEATER ON"))  { controlHeater(true);                   return; }
        if (cmd == F("HEATER OFF")) { controlHeater(false);                  return; }
        if (cmd.startsWith(F("LED "))) {
            controlLED(cmd.substring(4).toInt());
            return;
        }
    }

    // ── 센서 데이터 조회 ─────────────────────────────────────
    if (cmd == F("SENSOR TEMP"))  { printSensorTemp();  return; }
    if (cmd == F("SENSOR LIGHT")) { printSensorLight(); return; }
    if (cmd == F("SENSOR WATER")) { printSensorWater(); return; }
    if (cmd == F("SENSOR ALL"))   { printSensorAll();   return; }

    // ── RFID 카드 명령 ───────────────────────────────────────
    // RFID SCAN : 카드 1회 스캔 → UID + 블록 데이터 + 등록 정보 출력
    //             수동 모드 전용 (자동 모드에서는 카드 감지 시 자동 출력)
    // RFID LIST : Arduino 내장 등록 카드 레지스트리 전체 목록 출력
    if (cmd == F("RFID SCAN")) {
        if (autoMode) {
            Serial.println(F("[RFID] 자동 모드에서는 카드 감지 시 자동 출력됩니다."));
            Serial.println(F("       수동 모드(MODE MANUAL)로 전환 후 사용하세요."));
        } else {
            cmdRfidScan();
        }
        return;
    }
    if (cmd == F("RFID LIST")) { printCardRegistry(); return; }

    // ── 가짜 데이터 명령 ─────────────────────────────────────
    if (cmd.startsWith(F("FAKE"))) {
        String fArgs = cmd.substring(4); fArgs.trim();

        if (fArgs == F("ON")) {
            fakeMode = true;
            Serial.println(F("[FAKE] 가짜 데이터 모드 ON"));
            printFakeStatus();
            return;
        }
        if (fArgs == F("OFF")) {
            fakeMode = false;
            Serial.println(F("[FAKE] 가짜 데이터 모드 OFF → 실제 센서 사용"));
            return;
        }
        if (fArgs == F("STATUS")) { printFakeStatus(); return; }

        // FAKE TEMP <값>  ("FAKE TEMP " = 10글자)
        if (fArgs.startsWith(F("TEMP "))) {
            fakeTemperature = raw.substring(10).toFloat();
            Serial.print(F("[FAKE] 가짜 온도: ")); Serial.print(fakeTemperature, 1); Serial.println(F(" °C"));
            return;
        }
        // FAKE HUM <값>  ("FAKE HUM " = 9글자)
        if (fArgs.startsWith(F("HUM "))) {
            fakeHumidity = constrain(raw.substring(9).toFloat(), 0.0f, 100.0f);
            Serial.print(F("[FAKE] 가짜 습도: ")); Serial.print(fakeHumidity, 1); Serial.println(F(" %"));
            return;
        }
        // FAKE LIGHT <값>  ("LIGHT " = 6글자)
        if (fArgs.startsWith(F("LIGHT "))) {
            fakeLightLevel = constrain(fArgs.substring(6).toInt(), 0, LIGHT_MAX_RANGE);
            Serial.print(F("[FAKE] 가짜 조도: ")); Serial.print(fakeLightLevel);
            Serial.print(F(" RAW / 환산: ")); Serial.print(map(fakeLightLevel, 0, LIGHT_MAX_RANGE, 0, 100)); Serial.println(F(" %"));
            return;
        }
        // FAKE WATER <값>  ("WATER " = 6글자)
        if (fArgs.startsWith(F("WATER "))) {
            fakeWaterLevel = constrain(fArgs.substring(6).toInt(), 0, WATER_MAX_RANGE);
            Serial.print(F("[FAKE] 가짜 수위: ")); Serial.print(fakeWaterLevel);
            Serial.print(F(" RAW / 환산: ")); Serial.print(map(fakeWaterLevel, 0, WATER_MAX_RANGE, 0, 100)); Serial.println(F(" %"));
            return;
        }
        Serial.print(F("[ERR] 알 수 없는 FAKE 명령: ")); Serial.println(raw);
        return;
    }

    // ── 자동 모드 파라미터 설정 ──────────────────────────────
    if (cmd.startsWith(F("SET "))) {
        String args = cmd.substring(4);

        // SET PUMP INTERVAL <초>  ("PUMP INTERVAL " = 14글자)
        if (args.startsWith(F("PUMP INTERVAL "))) {
            pumpInterval = (unsigned long)args.substring(14).toInt() * 1000UL;
            Serial.print(F("[SET] 펌프 간격: ")); Serial.print(pumpInterval / 1000UL); Serial.println(F(" 초"));
            return;
        }
        // SET PUMP DURATION <초>  ("PUMP DURATION " = 14글자)
        if (args.startsWith(F("PUMP DURATION "))) {
            pumpDuration = (unsigned long)args.substring(14).toInt() * 1000UL;
            Serial.print(F("[SET] 펌프 시간: ")); Serial.print(pumpDuration / 1000UL); Serial.println(F(" 초"));
            return;
        }
        // SET TEMP <값>  ("SET TEMP " = 9글자, 소수점 원본에서 파싱)
        if (args.startsWith(F("TEMP "))) {
            tempSetpoint = raw.substring(9).toFloat();
            Serial.print(F("[SET] 기준 온도: ")); Serial.print(tempSetpoint, 1); Serial.println(F(" °C"));
            return;
        }
        // SET HYST <값>  ("SET HYST " = 9글자)
        if (args.startsWith(F("HYST "))) {
            tempHysteresis = raw.substring(9).toFloat();
            Serial.print(F("[SET] 히스테리시스: ±")); Serial.print(tempHysteresis / 2.0f, 1); Serial.println(F(" °C"));
            return;
        }
        // SET LIGHT <값>  ("LIGHT " = 6글자)
        if (args.startsWith(F("LIGHT "))) {
            lightThreshold = constrain(args.substring(6).toInt(), 0, LIGHT_MAX_RANGE);
            Serial.print(F("[SET] 조도 임계값: ")); Serial.println(lightThreshold);
            return;
        }
    }

    // ── 알 수 없는 명령 ──────────────────────────────────────
    Serial.print(F("[ERR] 알 수 없는 명령: ")); Serial.println(raw);
    Serial.println(F("       'HELP' 를 입력하면 명령어 목록을 볼 수 있습니다."));
}

// ============================================================
//  ─── SFAM 수신 상태 머신 ────────────────────────────────
// ============================================================

/*
 * rxStateMachine(b)
 * 역할 : espSerial 에서 읽은 바이트 1개를 4단계 FSM 으로 처리
 *        패킷 완성 → validatePacket() → dispatchPacket() 호출
 *
 * 상태 전이:
 *   WAIT_SOF    → 0xAA 수신 시 → READ_HEADER
 *   READ_HEADER → 헤더 6B 완성 시 → READ_PAYLOAD 또는 READ_CRC
 *   READ_PAYLOAD→ 페이로드 완성 시 → READ_CRC
 *   READ_CRC    → CRC 2B 완성 시 → 검증 → WAIT_SOF
 */
void rxStateMachine(uint8_t b) {
    switch (rxState) {

    case WAIT_SOF:
        // 0xAA(SOF) 마커 대기. 다른 바이트는 모두 무시
        if (b == PKT_SOF) {
            rxIdx         = 0;
            rxBuf[rxIdx++] = b;         // SOF 저장
            rxStartMs     = millis();   // 타임아웃 카운트 시작
            rxState       = READ_HEADER;
        }
        break;

    case READ_HEADER:
        // MSG_TYPE(1B) + SRC_ID(1B) + DST_ID(1B) + SEQ(1B) + LEN(1B) 수집
        rxBuf[rxIdx++] = b;
        if (rxIdx == PKT_HDR_LEN) {           // 헤더 6바이트 완성
            rxPayloadLen = rxBuf[5];           // LEN 필드 읽기
            if (rxPayloadLen > PKT_MAX_PAYLOAD) {
                // LEN 범위 초과 → 비정상 패킷, 처음부터 재시작
                Serial.println(F("[RX] LEN 범위 초과 → 패킷 폐기"));
                rxState = WAIT_SOF; rxIdx = 0;
            } else if (rxPayloadLen == 0) {
                rxState = READ_CRC;      // 페이로드 없으면 CRC 단계로 바로 이동
            } else {
                rxState = READ_PAYLOAD;  // 페이로드 수집 단계로 이동
            }
        }
        break;

    case READ_PAYLOAD:
        // 페이로드 rxPayloadLen 바이트 수집
        rxBuf[rxIdx++] = b;
        if (rxIdx == PKT_HDR_LEN + rxPayloadLen)
            rxState = READ_CRC;
        break;

    case READ_CRC:
        // CRC16 2바이트 수집 후 패킷 무결성 검증
        rxBuf[rxIdx++] = b;
        if (rxIdx == PKT_HDR_LEN + rxPayloadLen + PKT_CRC_LEN) {
            PktHeader hdr;
            if (validatePacket(rxBuf, rxIdx, &hdr)) {
                // 유효 패킷 → 디버그 출력 후 디스패치
                debugPrintPacket("RX", rxBuf, rxIdx);
                dispatchPacket(rxBuf, rxIdx, hdr);
            } else {
                Serial.println(F("[RX] CRC 오류 또는 무효 패킷 → 폐기"));
            }
            rxState = WAIT_SOF; rxIdx = 0;  // 다음 패킷 대기 상태 복귀
        }
        break;
    }
}

// ============================================================
//  ─── 수신 패킷 디스패처 ─────────────────────────────────
// ============================================================

/*
 * dispatchPacket(buf, total_len, hdr)
 * 역할 : 검증 완료 패킷의 MSG_TYPE 에 따라 핸들러 함수 호출
 *        본 장치(MY_DEV_ID) 또는 브로드캐스트 대상 패킷만 처리
 */
void dispatchPacket(const uint8_t *buf, uint8_t total_len, const PktHeader &hdr) {
    const uint8_t *payload = buf + PKT_HDR_LEN;  // 페이로드 시작 포인터

    // 수신 대상 확인 (내 장치 ID 또는 브로드캐스트만 처리)
    if (hdr.dst_id != MY_DEV_ID && hdr.dst_id != DEV_BROADCAST) {
        // 다른 장치 대상 패킷 → 무시 (멀티드롭 버스 환경 대비)
        return;
    }

    switch (hdr.msg_type) {

    case MSG_HEARTBEAT_REQ:
        // 하트비트 요청 → 즉시 ACK 응답
        handleHeartbeatReq(hdr);
        break;

    case MSG_ACTUATOR_CMD:
        // 액추에이터 명령 → 실제 제어 실행 + ACK 응답
        handleActuatorCmd(hdr, payload);
        break;

    case MSG_AGV_TASK_CMD:
        // AGV 작업 명령 → 수신 확인 ACK + (필요 시 AGV 포워딩)
        handleAgvTaskCmd(hdr, payload);
        break;

    case MSG_ACK:
        // 서버에서 보낸 ACK 수신 확인 (로그만)
        Serial.print(F("[ACK 수신] acked_type=0x")); Serial.print(payload[0], HEX);
        Serial.print(F(" seq="));                     Serial.println(payload[1]);
        break;

    case MSG_NAK:
        // 서버에서 보낸 NAK → 이후 재전송 로직 구현 위치
        Serial.print(F("[NAK 수신] reason=")); Serial.println(payload[2]);
        break;

    default:
        // 미지원 MSG_TYPE → NAK 응답 (reason=9: 알 수 없음)
        {
            uint8_t txBuf[PKT_MAX_TOTAL];
            uint8_t len = buildNak(txBuf, MY_DEV_ID, hdr.src_id,
                                   gSeq++, hdr.msg_type, hdr.seq, 9);
            sendPacket(txBuf, len);
            Serial.print(F("[DISPATCH] 미지원 MSG_TYPE=0x"));
            Serial.println(hdr.msg_type, HEX);
        }
        break;
    }
}

// ============================================================
//  ─── 수신 핸들러 함수들 ─────────────────────────────────
// ============================================================

/*
 * handleHeartbeatReq(hdr)
 * 역할 : HEARTBEAT_REQ 수신 → HEARTBEAT_ACK 즉시 응답
 *
 * ★ 통합: aux_value 에 실제 autoMode 반영
 *   status_id = 0x01 (ONLINE)
 *   aux_value = 0x00 (MANUAL) / 0x01 (AUTO) ← 실제 상태 반영
 */
void handleHeartbeatReq(const PktHeader &hdr) {
    uint8_t txBuf[PKT_MAX_TOTAL];
    uint8_t len = buildHeartbeatAck(
        txBuf,
        MY_DEV_ID,                    // SRC: 이 컨트롤러
        hdr.src_id,                   // DST: 요청 장치
        gSeq++,
        0x01,                         // status_id = ONLINE (0x01)
        autoMode ? 0x01 : 0x00        // aux_value: 0=MANUAL, 1=AUTO ★실제 상태 반영
    );
    sendPacket(txBuf, len);
    Serial.println(F("[HEARTBEAT] ACK 전송 완료"));
}

/*
 * handleActuatorCmd(hdr, payload)
 * 역할 : ACTUATOR_CMD(0x21) 수신 → 실제 하드웨어 제어 → ACTUATOR_ACK 응답
 *
 * ★ 통합 핵심: 주석처리됐던 controlPump/Fan/Heater/LED 실제 호출
 *
 * 액추에이터 ID 매핑:
 *   1 = PUMP   (state_value: 0=OFF, 1=ON)
 *   2 = FAN    (state_value: 0=OFF, 1=ON)
 *   3 = HEATER (state_value: 0=OFF, 1=ON)
 *   4 = LED    (state_value: 0=OFF, 1~100=밝기%)
 */
void handleActuatorCmd(const PktHeader &hdr, const uint8_t *payload) {
    const PayloadActuatorCmd *cmd =
        reinterpret_cast<const PayloadActuatorCmd *>(payload);

    Serial.print(F("[ACTUATOR_CMD] ID="));    Serial.print(cmd->actuator_id);
    Serial.print(F(" state="));               Serial.print(cmd->state_value);
    Serial.print(F(" trigger="));             Serial.print(cmd->trigger_id);
    Serial.print(F(" dur="));                 Serial.println(cmd->duration_sec);

    // ── 실제 하드웨어 제어 ──────────────────────────────────
    // AUTO 모드에서도 원격 명령을 강제 적용 (서버가 우선권 보유)
    uint8_t result = 0;   // 0=SUCCESS, 1=FAIL

    switch (cmd->actuator_id) {
    case ACT_ID_MODE:
        autoMode = (cmd->state_value == 1);
        if (autoMode) {
            pumpLastStart = 0;    // AUTO 전환 시 펌프 타이머 초기화
            pumpRunning   = false;
        }
        sendCtrlStatusRpt();      // 모드 변경 즉시 서버에 상태 보고
        break;
    case ACT_ID_PUMP:
        // PUMP: 0=OFF, 1=ON
        controlPump(cmd->state_value > 0);
        break;

    case ACT_ID_FAN:
        // FAN: 0=OFF, 1=ON
        controlFan(cmd->state_value > 0);
        break;

    case ACT_ID_HEATER:
        // HEATER: 0=OFF, 1=ON
        controlHeater(cmd->state_value > 0);
        break;

    case ACT_ID_LED:
        // LED: state_value = 밝기 % (0=소등, 1~100=밝기)
        controlLED((int)cmd->state_value);
        break;

    default:
        // 알 수 없는 액추에이터 ID → FAIL 응답
        Serial.print(F("[ACTUATOR_CMD] 알 수 없는 actuator_id="));
        Serial.println(cmd->actuator_id);
        result = 1;   // result = FAIL
        break;
    }

    // ── ACTUATOR_ACK 응답 전송 ─────────────────────────────
    uint8_t txBuf[PKT_MAX_TOTAL];
    uint8_t len = buildActuatorAck(
        txBuf,
        MY_DEV_ID,           // SRC
        hdr.seq,             // 요청 SEQ 를 그대로 사용 (매칭 식별용)
        cmd->actuator_id,    // 제어된 액추에이터 ID
        cmd->state_value,    // 실제 적용된 상태값
        result               // 0=SUCCESS, 1=FAIL
    );
    sendPacket(txBuf, len);
    Serial.println(F("[ACTUATOR_CMD] ACK 전송 완료"));
}

/*
 * handleAgvTaskCmd(hdr, payload)
 * 역할 : AGV_TASK_CMD(0x11) 수신 → 수신 확인 ACK 응답
 *        (실제 AGV 포워딩 구현 시 이 위치에 Serial2/RS485 전송 추가)
 */
void handleAgvTaskCmd(const PktHeader &hdr, const uint8_t *payload) {
    const PayloadAgvTaskCmd *cmd =
        reinterpret_cast<const PayloadAgvTaskCmd *>(payload);

    uint16_t task_id = ((uint16_t)cmd->task_id_hi << 8) | cmd->task_id_lo;
    Serial.print(F("[AGV_TASK_CMD] task_id=")); Serial.print(task_id);
    Serial.print(F(" src_node=0x")); Serial.print(cmd->src_node_idx, HEX);
    Serial.print(F(" dst_node=0x")); Serial.println(cmd->dst_node_idx, HEX);

    // ACK 응답 (수신 확인)
    uint8_t txBuf[PKT_MAX_TOTAL];
    uint8_t len = buildAck(txBuf, MY_DEV_ID, hdr.src_id,
                           gSeq++, hdr.msg_type, hdr.seq);
    sendPacket(txBuf, len);
}

// ============================================================
//  ─── 주기적 송신 함수들 ─────────────────────────────────
// ============================================================

/*
 * sendSensorBatch()
 * 역할 : 온도·습도·조도·수위 4채널을 SENSOR_BATCH 패킷으로 전송
 *
 * ★ 통합 핵심:
 *   fakeMode=true  → fakeTemperature, fakeHumidity 등 실제 fake 변수 사용
 *   fakeMode=false → temperature, humidity 등 실제 센서 전역 변수 사용
 *   (기존 코드에서 fakeMode=false 시 모두 0으로 하드코딩 → 수정됨)
 *
 * 값 형식: 실측값 × 100 → int32_t (int24 범위)
 *   예: 온도 25.50℃ → 2550
 */
void sendSensorBatch() {
    // 센서 ID 배열 (nursery_sensors.sensor_id 와 일치)
    const uint8_t sensor_ids[4] = {
        0x01,   // 온도 센서
        0x02,   // 습도 센서
        0x03,   // 조도 센서
        0x04    // 수위 센서
    };

    int32_t values[4];
    if (fakeMode) {
        // 가짜 데이터 사용 (테스트·디버깅용)
        values[0] = (int32_t)(fakeTemperature * 100.0f);  // 25.5℃ → 2550
        values[1] = (int32_t)(fakeHumidity    * 100.0f);  // 60.0% → 6000
        values[2] = (int32_t)(fakeLightLevel);     // ADC
        values[3] = (int32_t)(fakeWaterLevel);     // ADC
    } else {
        // ★ 실제 센서 전역 변수 사용 (통합 후 연결)
        // NAN인 경우 0 전송 (서버에서 유효성 판단)
        values[0] = isnan(temperature) ? 0 : (int32_t)(temperature * 100.0f);
        values[1] = isnan(humidity)    ? 0 : (int32_t)(humidity    * 100.0f);
        values[2] = (int32_t)(lightLevel);          // ADC
        values[3] = (int32_t)(waterLevel);          // ADC
    }

    uint8_t txBuf[PKT_MAX_TOTAL];
    uint8_t len = buildSensorBatch(txBuf, MY_DEV_ID, gSeq++,
                                   4, sensor_ids, values);
    sendPacket(txBuf, len);
    Serial.println(F("[SENSOR_BATCH] 전송 완료"));
}

/*
 * sendCtrlStatusRpt()
 * 역할 : 컨트롤러 전체 상태를 CTRL_STATUS_RPT 패킷으로 전송
 *
 * ★ 통합 핵심:
 *   control_mode    : 실제 autoMode 변수 반영 (0=MANUAL, 1=AUTO)
 *   actuator_bitmask: getActuatorBitmask() 로 실제 상태 반영
 *   (기존 코드에서 0x00 하드코딩 → 수정됨)
 */
void sendCtrlStatusRpt() {
    uint8_t txBuf[PKT_MAX_TOTAL];
    uint8_t len = buildCtrlStatusRpt(
        txBuf,
        MY_DEV_ID,              // SRC
        gSeq++,                 // SEQ
        autoMode ? 0x01 : 0x00, // control_mode ★실제 autoMode 반영
        0x01,                   // device_status = ONLINE
        getActuatorBitmask(),   // actuator_bitmask ★실제 상태 반영
        0x00                    // error_flags = 이상 없음
    );
    sendPacket(txBuf, len);
    Serial.println(F("[CTRL_STATUS_RPT] 전송 완료"));
}

// ============================================================
//  ─── 패킷 전송 / 디버그 출력 ────────────────────────────
// ============================================================

/*
 * sendPacket(buf, len)
 * 역할 : 조립된 패킷을 espSerial(SoftwareSerial) 로 전송
 *        전송 전 디버그 출력으로 내용 확인 가능
 */
void sendPacket(const uint8_t *buf, uint8_t len) {
    espSerial.write(buf, len);          // SoftwareSerial 로 바이트 배열 전송
    debugPrintPacket("TX", buf, len);   // 전송 내용 디버그 출력
}

/*
 * debugPrintPacket(dir, buf, len)
 * 역할 : 패킷 내용을 HEX 형식으로 Serial Monitor 에 출력
 * 인자 : dir = "TX"(송신) 또는 "RX"(수신)
 */
void debugPrintPacket(const char *dir, const uint8_t *buf, uint8_t len) {
    Serial.print(F("["));   Serial.print(dir);
    Serial.print(F("] type=0x"));
    if (buf[1] < 0x10) Serial.print(F("0"));
    Serial.print(buf[1], HEX);              // MSG_TYPE
    Serial.print(F(" src=0x")); Serial.print(buf[2], HEX);  // SRC_ID
    Serial.print(F(" dst=0x")); Serial.print(buf[3], HEX);  // DST_ID
    Serial.print(F(" seq="));   Serial.print(buf[4]);        // SEQ
    Serial.print(F(" len="));   Serial.print(buf[5]);        // LEN
    Serial.print(F(" | hex:"));
    for (uint8_t i = 0; i < len; i++) {
        Serial.print(F(" "));
        if (buf[i] < 0x10) Serial.print(F("0"));
        Serial.print(buf[i], HEX);
    }
    Serial.println();
}