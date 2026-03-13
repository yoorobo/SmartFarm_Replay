/*
 * ============================================================
 *  esp32_sfam_comm.ino  —  ESP32 SFAM 패킷 브리지 펌웨어
 * ============================================================
 *  역할 : Host PC (TCP 서버) ↔ ESP32 (TCP 클라이언트·브리지) ↔ Arduino (UART2)
 *
 *  통신 구조:
 *    Host PC  : TCP 서버, 고정 IP  ← sfam_test.py 를 먼저 실행
 *    ESP32    : TCP 클라이언트, DHCP (IP 자동 할당)
 *    Arduino  : UART2 (Serial2) 로 ESP32 와 연결
 *
 *  ★ 설정 항목 (2곳만 수정):
 *    WIFI_SSID / WIFI_PASS  : 공유기 AP 정보
 *    HOST_PC_IP             : Host PC 의 고정 IP
 *    (ESP32 IP 는 DHCP 자동 할당 — 별도 설정 불필요)
 *
 *  브리지 동작:
 *    Host PC → ESP32 → Arduino : TCP 수신 패킷을 Serial2 로 포워딩
 *    Arduino → ESP32 → Host PC : Serial2 수신 패킷을 TCP 로 포워딩
 *    재연결  : TCP 끊김 감지 시 5초 간격으로 자동 재접속
 *    Heartbeat: 5초마다 HEARTBEAT_REQ 를 Arduino·Host PC 양방향 전송
 *
 *  배선:
 *    ESP32 GPIO16 (RX2) ← Arduino A2 (TX)   [레벨시프터 권장]
 *    ESP32 GPIO17 (TX2) → Arduino A3 (RX)
 *    ESP32 GND          ── Arduino GND
 *
 *  필요 파일     : sfam_packet.h (이 .ino 파일과 같은 폴더)
 *  필요 라이브러리: WiFi (ESP32 Arduino Core 기본 포함)
 * ============================================================
 */

#include "sfam_packet.h"
#include <WiFi.h>

// ============================================================
//  ★ 설정 항목 — 여기만 수정하면 됩니다
// ============================================================

// ── WiFi AP ───────────────────────────────────────────────────
#define WIFI_SSID  "addinedu_201class_4-2.4G"
#define WIFI_PASS  "201class4!"

// #define WIFI_SSID  "zaksim4_2.4G"
// #define WIFI_PASS  "zaksim123@"

// ── Host PC 고정 IP (TCP 서버) ────────────────────────────────
// sfam_test.py 의 HOST_PC_IP 와 반드시 동일해야 함
static const IPAddress HOST_PC_IP(192, 168, 0, 29);
#define HOST_PC_PORT  8000

// ============================================================
//  고정 설정 (변경 불필요)
// ============================================================
#define PIN_UART2_RX        16       // GPIO16 (RX2) ← Arduino A2 (TX)
#define PIN_UART2_TX        17       // GPIO17 (TX2) → Arduino A3 (RX)
#define MY_DEV_ID           0x20     // 브리지 장치 ID
#define BAUD_DEBUG          115200
#define BAUD_ARDUINO        38400    // Arduino SoftwareSerial 안정 최대 속도
#define RECONNECT_INTERVAL  5000     // TCP 재접속 간격 (ms)
#define HEARTBEAT_PERIOD    5000     // HEARTBEAT_REQ 전송 주기 (ms)
#define RX_TIMEOUT_MS       200      // 패킷 수신 타임아웃 (ms)

// ── TCP 클라이언트 ────────────────────────────────────────────
WiFiClient tcpClient;

// ── 시퀀스 번호 및 타이머 ────────────────────────────────────
static uint8_t  gSeq            = 0x00;
static uint32_t lastHeartbeatMs = 0;
static uint32_t lastReconnectMs = 0;

// ============================================================
//  수신 상태 머신 변수
// ============================================================
enum RxState { WAIT_SOF, READ_HEADER, READ_PAYLOAD, READ_CRC };

// TCP 수신: Host PC → ESP32 → Arduino
static RxState  tcpRxState      = WAIT_SOF;
static uint8_t  tcpRxBuf[PKT_MAX_TOTAL];
static uint8_t  tcpRxIdx        = 0;
static uint8_t  tcpRxPayloadLen = 0;
static uint32_t tcpRxStartMs    = 0;

// UART2 수신: Arduino → ESP32 → Host PC
static RxState  uartRxState      = WAIT_SOF;
static uint8_t  uartRxBuf[PKT_MAX_TOTAL];
static uint8_t  uartRxIdx        = 0;
static uint8_t  uartRxPayloadLen = 0;
static uint32_t uartRxStartMs    = 0;

// ============================================================
//  함수 전방 선언
// ============================================================
void connectWiFi();
bool connectToServer();
void tcpRxStateMachine(uint8_t b);
void uartRxStateMachine(uint8_t b);
void forwardToArduino(const uint8_t *buf, uint8_t len);
void forwardToHostPC(const uint8_t *buf, uint8_t len);
void sendHeartbeatReq();
void debugPrintPacket(const char *dir, const uint8_t *buf, uint8_t len);

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(BAUD_DEBUG);
    delay(500);

    Serial2.begin(BAUD_ARDUINO, SERIAL_8N1, PIN_UART2_RX, PIN_UART2_TX);

    Serial.println("==================================================");
    Serial.println(" ESP32 SFAM 브리지  [TCP 클라이언트 / DHCP 모드]");
    Serial.printf(" Host PC  : %d.%d.%d.%d:%d\n",
                  HOST_PC_IP[0], HOST_PC_IP[1],
                  HOST_PC_IP[2], HOST_PC_IP[3], HOST_PC_PORT);
    Serial.printf(" UART2    : RX=GPIO%d  TX=GPIO%d  %dbps\n",
                  PIN_UART2_RX, PIN_UART2_TX, BAUD_ARDUINO);
    Serial.println("==================================================");

    connectWiFi();
    connectToServer();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // ── 태스크 1: TCP 연결 유지 및 자동 재접속 ───────────────
    if (!tcpClient.connected()) {
        if (millis() - lastReconnectMs >= RECONNECT_INTERVAL) {
            lastReconnectMs = millis();
            Serial.println("[TCP] 연결 끊김 → 재접속 시도...");
            connectToServer();
        }
    }

    // ── 태스크 2: Host PC → Arduino 포워딩 ───────────────────
    while (tcpClient.connected() && tcpClient.available()) {
        tcpRxStateMachine((uint8_t)tcpClient.read());
    }

    // ── 태스크 3: Arduino → Host PC 포워딩 ───────────────────
    while (Serial2.available()) {
        uartRxStateMachine((uint8_t)Serial2.read());
    }

    // ── 태스크 4: 수신 타임아웃 감지 ─────────────────────────
    if (tcpRxState != WAIT_SOF && millis() - tcpRxStartMs > RX_TIMEOUT_MS) {
        tcpRxState = WAIT_SOF; tcpRxIdx = 0;
        Serial.println("[TCP-RX] 타임아웃 → 상태 초기화");
    }
    if (uartRxState != WAIT_SOF && millis() - uartRxStartMs > RX_TIMEOUT_MS) {
        uartRxState = WAIT_SOF; uartRxIdx = 0;
        Serial.println("[UART-RX] 타임아웃 → 상태 초기화");
    }

    // ── 태스크 5: Heartbeat 주기 전송 ────────────────────────
    if (millis() - lastHeartbeatMs >= HEARTBEAT_PERIOD) {
        lastHeartbeatMs = millis();
        sendHeartbeatReq();
    }
}

// ============================================================
//  WiFi 연결 (DHCP — IP 자동 할당)
// ============================================================
void connectWiFi() {
    // WiFi.config() 를 호출하지 않으면 DHCP 로 동작 (기본값)
    Serial.printf("[WiFi] %s 에 연결 중", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        // DHCP 로 할당된 IP 를 시리얼 모니터에 출력
        // → 같은 네트워크 내 Host PC 에서 이 IP 로 ping 하여 연결 확인 가능
        Serial.printf("[WiFi] 연결 성공  ESP32 IP(DHCP): %s\n",
                      WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] 연결 실패 → SSID/비밀번호 확인 후 재시작");
    }
}

// ============================================================
//  Host PC TCP 서버 접속
// ============================================================
bool connectToServer() {
    Serial.printf("[TCP] Host PC %d.%d.%d.%d:%d 접속 중...\n",
                  HOST_PC_IP[0], HOST_PC_IP[1],
                  HOST_PC_IP[2], HOST_PC_IP[3], HOST_PC_PORT);

    if (tcpClient.connect(HOST_PC_IP, HOST_PC_PORT)) {
        tcpClient.setNoDelay(true);   // Nagle 알고리즘 비활성 → 저지연
        Serial.println("[TCP] 접속 성공");
        return true;
    }
    Serial.println("[TCP] 접속 실패 → sfam_test.py 서버가 먼저 실행되어야 합니다");
    return false;
}

// ============================================================
//  TCP 수신 상태 머신 (Host PC → Arduino 방향)
// ============================================================
void tcpRxStateMachine(uint8_t b) {
    switch (tcpRxState) {
    case WAIT_SOF:
        if (b == PKT_SOF) {
            tcpRxIdx = 0;
            tcpRxBuf[tcpRxIdx++] = b;
            tcpRxStartMs = millis();
            tcpRxState   = READ_HEADER;
        }
        break;
    case READ_HEADER:
        tcpRxBuf[tcpRxIdx++] = b;
        if (tcpRxIdx == PKT_HDR_LEN) {
            tcpRxPayloadLen = tcpRxBuf[5];
            if      (tcpRxPayloadLen > PKT_MAX_PAYLOAD) { tcpRxState = WAIT_SOF; tcpRxIdx = 0; }
            else if (tcpRxPayloadLen == 0)               { tcpRxState = READ_CRC; }
            else                                          { tcpRxState = READ_PAYLOAD; }
        }
        break;
    case READ_PAYLOAD:
        tcpRxBuf[tcpRxIdx++] = b;
        if (tcpRxIdx == PKT_HDR_LEN + tcpRxPayloadLen) tcpRxState = READ_CRC;
        break;
    case READ_CRC:
        tcpRxBuf[tcpRxIdx++] = b;
        if (tcpRxIdx == PKT_HDR_LEN + tcpRxPayloadLen + PKT_CRC_LEN) {
            PktHeader hdr;
            if (validatePacket(tcpRxBuf, tcpRxIdx, &hdr)) {
                debugPrintPacket("PC→UNO", tcpRxBuf, tcpRxIdx);
                forwardToArduino(tcpRxBuf, tcpRxIdx);
            } else {
                Serial.println("[TCP-RX] CRC 오류 → 폐기");
            }
            tcpRxState = WAIT_SOF; tcpRxIdx = 0;
        }
        break;
    }
}

// ============================================================
//  UART2 수신 상태 머신 (Arduino → Host PC 방향)
// ============================================================
void uartRxStateMachine(uint8_t b) {
    switch (uartRxState) {
    case WAIT_SOF:
        if (b == PKT_SOF) {
            uartRxIdx = 0;
            uartRxBuf[uartRxIdx++] = b;
            uartRxStartMs = millis();
            uartRxState   = READ_HEADER;
        }
        break;
    case READ_HEADER:
        uartRxBuf[uartRxIdx++] = b;
        if (uartRxIdx == PKT_HDR_LEN) {
            uartRxPayloadLen = uartRxBuf[5];
            if      (uartRxPayloadLen > PKT_MAX_PAYLOAD) { uartRxState = WAIT_SOF; uartRxIdx = 0; }
            else if (uartRxPayloadLen == 0)               { uartRxState = READ_CRC; }
            else                                           { uartRxState = READ_PAYLOAD; }
        }
        break;
    case READ_PAYLOAD:
        uartRxBuf[uartRxIdx++] = b;
        if (uartRxIdx == PKT_HDR_LEN + uartRxPayloadLen) uartRxState = READ_CRC;
        break;
    case READ_CRC:
        uartRxBuf[uartRxIdx++] = b;
        if (uartRxIdx == PKT_HDR_LEN + uartRxPayloadLen + PKT_CRC_LEN) {
            PktHeader hdr;
            if (validatePacket(uartRxBuf, uartRxIdx, &hdr)) {
                debugPrintPacket("UNO→PC", uartRxBuf, uartRxIdx);
                forwardToHostPC(uartRxBuf, uartRxIdx);
            } else {
                Serial.println("[UART-RX] CRC 오류 → 폐기");
            }
            uartRxState = WAIT_SOF; uartRxIdx = 0;
        }
        break;
    }
}

// ============================================================
//  포워딩 / Heartbeat / 디버그
// ============================================================
void forwardToArduino(const uint8_t *buf, uint8_t len) {
    Serial2.write(buf, len);
}

void forwardToHostPC(const uint8_t *buf, uint8_t len) {
    if (tcpClient.connected()) tcpClient.write(buf, len);
}

void sendHeartbeatReq() {
    uint8_t buf[PKT_MAX_TOTAL];
    uint8_t len = buildHeartbeatReq(buf, MY_DEV_ID, DEV_BROADCAST, gSeq++);
    forwardToArduino(buf, len);
    forwardToHostPC(buf, len);
    debugPrintPacket("HB→ALL", buf, len);
}

void debugPrintPacket(const char *dir, const uint8_t *buf, uint8_t len) {
    Serial.printf("[%s] type=0x%02X src=0x%02X dst=0x%02X seq=%d len=%d | hex:",
                  dir, buf[1], buf[2], buf[3], buf[4], buf[5]);
    for (uint8_t i = 0; i < len; i++) Serial.printf(" %02X", buf[i]);
    Serial.println();
}