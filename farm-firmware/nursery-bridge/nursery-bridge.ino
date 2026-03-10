/**
 * ============================================================
 *  Smart Farm ESP32 Serial-TCP Bridge  v1.0
 *  Host PC (MySQL) <--WiFi/TCP--> ESP32 <--UART--> Arduino
 *
 *  Smart Farm Serial Packet Spec v1.0 준거
 *  CRC16-CCITT (Poly 0x1021 / Init 0xFFFF / No Reflection)
 * ============================================================
 *
 *  [역할]
 *   ① Arduino UART (115,200bps) ↔ WiFi TCP Socket 투명 브리지
 *   ② 양방향 수신 상태 머신 + CRC16 검증
 *   ③ CRC 오류 시 발신 측에 NAK 전송
 *   ④ TCP 미연결 시 HEARTBEAT_REQ 로컬 대리 응답
 *      → Arduino가 OFFLINE 처리되지 않도록 보호
 *   ⑤ PC → Arduino ACTUATOR_CMD 재전송 관리
 *      (ACK 200ms 타임아웃 / 최대 3회 재전송)
 *   ⑥ WiFi 끊김 / TCP 끊김 시 자동 재접속
 *   ⑦ USB Serial 디버그 로그
 *
 *  [핀 매핑 - ESP32 DevKit]
 *   UART2 RX : GPIO 16  ← Arduino A2(SW_TX)
 *   UART2 TX : GPIO 17  → Arduino A3(SW_RX)
 *   USB Serial0 : 디버그 (@115,200bps)
 *
 *  [통신 파라미터]
 *   Arduino ↔ ESP32  : 115,200 bps / 8N1
 *   ESP32   ↔ PC     : WiFi TCP / 동일 바이너리 패킷 포맷
 *
 *  [장치 ID]
 *   서버(PC)   : 0x00
 *   CTRL-01    : 0x10 (Arduino Nursery Controller)
 *   Broadcast  : 0xFF
 *   Bridge     : 0xE0 (로컬 NAK / 대리 응답 전용)
 *
 *  [패킷 프레임]
 *   SOF(1) MSG_TYPE(1) SRC(1) DST(1) SEQ(1) LEN(1) PAYLOAD(N) CRC16(2)
 *   최대 74 bytes (헤더 6 + 페이로드 64 + CRC 2)
 *
 * ============================================================
 */

#include <Arduino.h>
#include <WiFi.h>

// ─────────────────────────────────────────────────────────────
//  ★ 사용자 설정 (환경에 맞게 수정)
// ─────────────────────────────────────────────────────────────
#define WIFI_SSID           "YOUR_SSID"       // WiFi SSID
#define WIFI_PASS           "YOUR_PASSWORD"   // WiFi 패스워드
#define SERVER_HOST         "192.168.1.100"   // Host PC IP 주소
#define SERVER_PORT         9000              // TCP 수신 포트

// ─────────────────────────────────────────────────────────────
//  UART 핀
// ─────────────────────────────────────────────────────────────
#define PIN_ARD_RX          16    // GPIO16 = UART2 RX ← Arduino TX(A2)
#define PIN_ARD_TX          17    // GPIO17 = UART2 TX → Arduino RX(A3)
#define BAUD_ARDUINO        115200
#define BAUD_DEBUG          115200

// ─────────────────────────────────────────────────────────────
//  접속 타임아웃 / 재시도 간격
// ─────────────────────────────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS   10000UL
#define RECONNECT_INTERVAL_MS     3000UL   // WiFi/TCP 재접속 폴링 주기

// ─────────────────────────────────────────────────────────────
//  패킷 프로토콜 상수  (Packet Spec §1~§3)
// ─────────────────────────────────────────────────────────────
#define SOF                 0xAA
#define MAX_PAYLOAD         64
#define PKT_HDR_SIZE        6                              // 고정 헤더 크기
#define PKT_MAX_FULL        (PKT_HDR_SIZE + MAX_PAYLOAD + 2) // 74 bytes

// MSG_TYPE  (Packet Spec §2)
#define MSG_HEARTBEAT_REQ   0x01
#define MSG_HEARTBEAT_ACK   0x02
#define MSG_AGV_TELEMETRY   0x10
#define MSG_AGV_TASK_CMD    0x11
#define MSG_AGV_TASK_ACK    0x12
#define MSG_AGV_STATUS_RPT  0x13
#define MSG_AGV_EMERGENCY   0x14
#define MSG_SENSOR_BATCH    0x20
#define MSG_ACTUATOR_CMD    0x21
#define MSG_ACTUATOR_ACK    0x22
#define MSG_CTRL_STATUS     0x23
#define MSG_RFID_EVENT      0x24
#define MSG_DOCK_REQ        0x30
#define MSG_DOCK_ACK        0x31
#define MSG_ERROR_REPORT    0xF0
#define MSG_ACK             0xFE
#define MSG_NAK             0xFF

// Device ID  (Packet Spec §3)
#define ID_SERVER           0x00
#define ID_CTRL_BASE        0x10    // 육묘 컨트롤러 시작 주소
#define ID_CTRL_MAX         0x1F    // 육묘 컨트롤러 끝 주소
#define ID_AGV_BASE         0x01
#define ID_AGV_MAX          0x08
#define ID_NODE_BASE        0x20
#define ID_NODE_MAX         0x5F
#define ID_BROADCAST        0xFF
#define ID_BRIDGE           0xE0    // 브리지 자체 (로컬 응답 전용)

// ACK 재전송 파라미터  (Packet Spec §8.1)
#define ACK_TIMEOUT_MS      200UL
#define ACK_MAX_RETRY       3

// ─────────────────────────────────────────────────────────────
//  수신 상태 머신 열거형
// ─────────────────────────────────────────────────────────────
enum RxState : uint8_t {
  S_WAIT_SOF,
  S_HEADER,
  S_PAYLOAD,
  S_CRC_HI,
  S_CRC_LO
};

// ─────────────────────────────────────────────────────────────
//  수신 파서 구조체
//  buf[] = 헤더(6B) + 페이로드(최대 64B) — CRC 제외
// ─────────────────────────────────────────────────────────────
struct RxParser {
  RxState  state   = S_WAIT_SOF;
  uint8_t  buf[PKT_HDR_SIZE + MAX_PAYLOAD];
  uint8_t  count   = 0;
  uint8_t  payLen  = 0;
  uint8_t  crcHi   = 0;
};

// ─────────────────────────────────────────────────────────────
//  ACTUATOR_CMD 재전송 대기 항목
//  PC → Arduino 방향 명령 패킷 한 건 추적
// ─────────────────────────────────────────────────────────────
struct PendingCmd {
  bool          active    = false;
  uint8_t       seq       = 0;        // 추적할 SEQ 번호
  uint8_t       retries   = 0;        // 현재 재전송 횟수
  unsigned long sentMs    = 0;        // 마지막 송신 시각
  uint8_t       pkt[PKT_MAX_FULL];    // 완성 패킷 (재전송용)
  uint8_t       pktLen    = 0;
};

// ═══════════════════════════════════════════════════════════
//  전역 변수
// ═══════════════════════════════════════════════════════════
WiFiClient  g_tcp;
RxParser    g_ardParser;          // Arduino → ESP32 파서
RxParser    g_tcpParser;          // PC     → ESP32 파서
PendingCmd  g_pendingCmd;         // ACTUATOR_CMD ACK 대기

static uint8_t  g_bridgeSeq    = 0;   // 브리지 자체 SEQ 카운터
unsigned long   g_lastReconMs  = 0;   // 재접속 타이머
unsigned long   g_lastHbMs     = 0;   // 로컬 HEARTBEAT 타이머

// ═══════════════════════════════════════════════════════════
//  ■ CRC16-CCITT
//  Poly=0x1021 / Init=0xFFFF / No Input·Output Reflection
//  범위: SOF(1) + 헤더(5) + PAYLOAD(N) = (6+N) bytes
// ═══════════════════════════════════════════════════════════
uint16_t calcCRC16(const uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= ((uint16_t)data[i] << 8);
    for (uint8_t b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
  }
  return crc;
}

// ─────────────────────────────────────────────────────────────
//  완전 패킷 조립 (헤더+페이로드 버퍼 → CRC 붙인 전송 버퍼)
//  반환값: 완성 패킷 총 바이트 수
// ─────────────────────────────────────────────────────────────
uint8_t makeFullPacket(const uint8_t* hdrPayBuf, uint8_t hdrPayLen,
                       uint8_t* outBuf) {
  memcpy(outBuf, hdrPayBuf, hdrPayLen);
  uint16_t crc = calcCRC16(hdrPayBuf, hdrPayLen);
  outBuf[hdrPayLen]     = (uint8_t)(crc >> 8);   // Big-Endian MSB
  outBuf[hdrPayLen + 1] = (uint8_t)(crc & 0xFF); // Big-Endian LSB
  return hdrPayLen + 2;
}

// ─────────────────────────────────────────────────────────────
//  디버그 유틸리티
// ─────────────────────────────────────────────────────────────
static inline void dbgHex(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

void dbgDump(const char* tag, const uint8_t* pkt, uint8_t len) {
  Serial.print(tag);
  for (uint8_t i = 0; i < len; i++) { dbgHex(pkt[i]); Serial.print(' '); }
  Serial.println();
}

// ─────────────────────────────────────────────────────────────
//  MSG_TYPE 이름 (로그용)
// ─────────────────────────────────────────────────────────────
const char* msgName(uint8_t t) {
  switch (t) {
    case MSG_HEARTBEAT_REQ:  return "HB_REQ";
    case MSG_HEARTBEAT_ACK:  return "HB_ACK";
    case MSG_SENSOR_BATCH:   return "SENSOR_BATCH";
    case MSG_ACTUATOR_CMD:   return "ACT_CMD";
    case MSG_ACTUATOR_ACK:   return "ACT_ACK";
    case MSG_CTRL_STATUS:    return "CTRL_STATUS";
    case MSG_RFID_EVENT:     return "RFID_EVENT";
    case MSG_ERROR_REPORT:   return "ERR_RPT";
    case MSG_ACK:            return "ACK";
    case MSG_NAK:            return "NAK";
    case MSG_AGV_TELEMETRY:  return "AGV_TELE";
    case MSG_AGV_TASK_CMD:   return "AGV_TASK";
    case MSG_AGV_TASK_ACK:   return "AGV_TASK_ACK";
    case MSG_AGV_STATUS_RPT: return "AGV_STATUS";
    case MSG_AGV_EMERGENCY:  return "AGV_EMRG";
    case MSG_DOCK_REQ:       return "DOCK_REQ";
    case MSG_DOCK_ACK:       return "DOCK_ACK";
    default:                 return "UNKNOWN";
  }
}

// ═══════════════════════════════════════════════════════════
//  ■ 네트워크 관리
// ═══════════════════════════════════════════════════════════

void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to '%s'", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - t < WIFI_CONNECT_TIMEOUT_MS) {
    delay(300); Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] OK  IP=%s  RSSI=%d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("\n[WiFi] TIMEOUT — 재시도 예정");
  }
}

bool tcpConnect() {
  if (g_tcp.connected()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("[TCP]  Connecting %s:%d ... ", SERVER_HOST, SERVER_PORT);
  if (g_tcp.connect(SERVER_HOST, SERVER_PORT)) {
    g_tcp.setNoDelay(true);   // Nagle 비활성화 → 지연 최소화
    Serial.println("OK");
    return true;
  }
  Serial.println("FAIL");
  return false;
}

// ═══════════════════════════════════════════════════════════
//  ■ 저수준 전송 함수
// ═══════════════════════════════════════════════════════════

// hdrPayBuf 내용에 CRC를 붙여 Arduino UART2로 전송
void sendToArduino(const uint8_t* hdrPayBuf, uint8_t hdrPayLen) {
  uint8_t pkt[PKT_MAX_FULL];
  uint8_t len = makeFullPacket(hdrPayBuf, hdrPayLen, pkt);
  Serial2.write(pkt, len);
  dbgDump("[→ARD] ", pkt, len);
}

// hdrPayBuf 내용에 CRC를 붙여 TCP로 전송
bool sendToTcp(const uint8_t* hdrPayBuf, uint8_t hdrPayLen) {
  if (!g_tcp.connected()) return false;
  uint8_t pkt[PKT_MAX_FULL];
  uint8_t len = makeFullPacket(hdrPayBuf, hdrPayLen, pkt);
  g_tcp.write(pkt, len);
  dbgDump("[→TCP] ", pkt, len);
  return true;
}

// 이미 완성된 패킷(CRC 포함)을 그대로 TCP로 전송 (재전송용)
bool resendToArduino(const uint8_t* fullPkt, uint8_t fullLen) {
  Serial2.write(fullPkt, fullLen);
  Serial.printf("[→ARD][RETRY] seq=0x%02X\n", fullPkt[4]);
  return true;
}

// ─────────────────────────────────────────────────────────────
//  브리지 자체 발신 패킷 빌더 (src=BRIDGE_ID)
// ─────────────────────────────────────────────────────────────
void buildBridgePkt(uint8_t* buf, uint8_t msgType, uint8_t dstId,
                    const uint8_t* pay, uint8_t payLen) {
  buf[0] = SOF;
  buf[1] = msgType;
  buf[2] = ID_BRIDGE;
  buf[3] = dstId;
  buf[4] = g_bridgeSeq++;   // 8-bit 롤오버
  buf[5] = payLen;
  if (payLen > 0) memcpy(buf + 6, pay, payLen);
}

// ─────────────────────────────────────────────────────────────
//  NAK 전송 (브리지 → Arduino / TCP)
// ─────────────────────────────────────────────────────────────
void sendNakToArduino(uint8_t nackedType, uint8_t seq, uint8_t reason) {
  uint8_t pay[3] = { nackedType, seq, reason };
  uint8_t buf[PKT_HDR_SIZE + 3];
  buildBridgePkt(buf, MSG_NAK, ID_CTRL_BASE, pay, 3);
  sendToArduino(buf, PKT_HDR_SIZE + 3);
  Serial.printf("[BRIDGE] NAK→ARD  type=0x%02X seq=%d reason=%d\n",
                nackedType, seq, reason);
}

void sendNakToTcp(uint8_t nackedType, uint8_t seq, uint8_t reason) {
  uint8_t pay[3] = { nackedType, seq, reason };
  uint8_t buf[PKT_HDR_SIZE + 3];
  buildBridgePkt(buf, MSG_NAK, ID_SERVER, pay, 3);
  sendToTcp(buf, PKT_HDR_SIZE + 3);
  Serial.printf("[BRIDGE] NAK→TCP  type=0x%02X seq=%d reason=%d\n",
                nackedType, seq, reason);
}

// ─────────────────────────────────────────────────────────────
//  HEARTBEAT_ACK — 브리지가 서버를 대신하여 Arduino에 응답
//  TCP 미연결 시 Arduino가 OFFLINE 처리되지 않도록 보호
// ─────────────────────────────────────────────────────────────
void sendLocalHeartbeatAck(uint8_t dstCtrlId) {
  // src=SERVER_ID 로 위장하여 Arduino 정상 동작 유지
  uint8_t buf[PKT_HDR_SIZE + 2];
  buf[0] = SOF;
  buf[1] = MSG_HEARTBEAT_ACK;
  buf[2] = ID_SERVER;          // 서버 대신 응답
  buf[3] = dstCtrlId;
  buf[4] = g_bridgeSeq++;
  buf[5] = 2;
  buf[6] = 0x01;               // status_id  = ONLINE
  buf[7] = 0x00;               // aux_value  = MANUAL 모드
  sendToArduino(buf, PKT_HDR_SIZE + 2);
  Serial.printf("[BRIDGE] HB_ACK → 0x%02X (로컬 대리 응답)\n", dstCtrlId);
}

// ─────────────────────────────────────────────────────────────
//  ACK 대기 등록 (PC→Arduino 명령 패킷 재전송 관리)
// ─────────────────────────────────────────────────────────────
void registerPendingCmd(uint8_t seq,
                        const uint8_t* fullPkt, uint8_t fullLen) {
  g_pendingCmd.active  = true;
  g_pendingCmd.seq     = seq;
  g_pendingCmd.retries = 0;
  g_pendingCmd.sentMs  = millis();
  g_pendingCmd.pktLen  = fullLen;
  memcpy(g_pendingCmd.pkt, fullPkt, fullLen);
  Serial.printf("[ACK_WAIT] 등록 seq=0x%02X\n", seq);
}

void clearPendingCmd() {
  if (g_pendingCmd.active) {
    Serial.printf("[ACK_WAIT] 해제 seq=0x%02X retries=%d\n",
                  g_pendingCmd.seq, g_pendingCmd.retries);
    g_pendingCmd.active = false;
  }
}

// ═══════════════════════════════════════════════════════════
//  ■ 패킷 라우팅 (CRC 검증 완료 후 호출)
//
//  fromArduino=true  : buf = Arduino에서 수신한 패킷 헤더+페이로드
//  fromArduino=false : buf = TCP(PC)에서 수신한 패킷 헤더+페이로드
// ═══════════════════════════════════════════════════════════
void routePacket(const uint8_t* buf, uint8_t hdrPayLen, bool fromArduino) {
  uint8_t msgType = buf[1];
  uint8_t srcId   = buf[2];
  uint8_t dstId   = buf[3];
  uint8_t seq     = buf[4];
  uint8_t payLen  = buf[5];

  Serial.printf("[PKT] %-12s | %s | src=0x%02X dst=0x%02X seq=%3d len=%d\n",
                msgName(msgType),
                fromArduino ? "ARD→PC" : "PC→ARD",
                srcId, dstId, seq, payLen);

  // ══════════════════════════════════════════════════════
  //  Arduino → PC 방향
  // ══════════════════════════════════════════════════════
  if (fromArduino) {

    // TCP 연결 중 → 투명 포워딩
    if (g_tcp.connected()) {
      sendToTcp(buf, hdrPayLen);
    }
    // TCP 미연결 — HEARTBEAT_REQ 한정 로컬 대리 응답
    else if (msgType == MSG_HEARTBEAT_REQ) {
      sendLocalHeartbeatAck(srcId);
      return;   // PC로는 전달하지 않음
    }
    // 그 외 TCP 미연결 패킷 → 드롭 + 로그
    else {
      Serial.printf("[WARN] TCP 미연결 — ARD→PC 패킷 드롭 (type=0x%02X)\n",
                    msgType);
    }

    // ACK / NAK 수신 시 대기 중인 PendingCmd 해제
    if (msgType == MSG_ACK || msgType == MSG_ACTUATOR_ACK) {
      // ACK payload[1] = acked_seq
      if (payLen >= 2 && g_pendingCmd.active &&
          buf[PKT_HDR_SIZE + 1] == g_pendingCmd.seq) {
        clearPendingCmd();
      }
      // ACTUATOR_ACK는 seq 필드로도 매핑 가능
      else if (msgType == MSG_ACTUATOR_ACK && g_pendingCmd.active) {
        // 연속된 ACT_ACK는 seq 기반 정확 매칭이 어려우므로 active 중 수신 시 해제
        clearPendingCmd();
      }
    }
    else if (msgType == MSG_NAK && g_pendingCmd.active) {
      // Arduino가 NAK 응답하면 재전송 취소
      if (payLen >= 2 && buf[PKT_HDR_SIZE + 1] == g_pendingCmd.seq) {
        Serial.println("[ACK_WAIT] Arduino NAK 수신 — 재전송 취소");
        clearPendingCmd();
      }
    }
  }

  // ══════════════════════════════════════════════════════
  //  PC → Arduino 방향
  // ══════════════════════════════════════════════════════
  else {
    bool destIsCtrl = (dstId >= ID_CTRL_BASE && dstId <= ID_CTRL_MAX);
    bool destIsBroadcast = (dstId == ID_BROADCAST);

    if (destIsCtrl || destIsBroadcast) {
      // 완성 패킷으로 조립 후 Arduino 전송
      uint8_t fullPkt[PKT_MAX_FULL];
      uint8_t fullLen = makeFullPacket(buf, hdrPayLen, fullPkt);
      Serial2.write(fullPkt, fullLen);
      dbgDump("[→ARD] ", fullPkt, fullLen);

      // ACTUATOR_CMD: ACK 대기 등록
      if (msgType == MSG_ACTUATOR_CMD) {
        registerPendingCmd(seq, fullPkt, fullLen);
      }
    }
    // Broadcast: TCP 루프백 없음 — AGV나 노드 주소는 현재 라우팅 미지원
    else if (!destIsCtrl && !destIsBroadcast) {
      Serial.printf("[BRIDGE] 라우팅 불가 dst=0x%02X (지원 범위 0x10~0x1F)\n",
                    dstId);
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  ■ 수신 상태 머신 (공용)
//  parser : 파서 인스턴스 (Arduino용 / TCP용 각각 독립)
//  b      : 수신 바이트
//  fromArduino : true=Arduino 수신, false=TCP 수신
// ═══════════════════════════════════════════════════════════
void processByte(RxParser& p, uint8_t b, bool fromArduino) {
  switch (p.state) {

    // ── SOF 대기 ─────────────────────────────────────────
    case S_WAIT_SOF:
      if (b == SOF) {
        p.buf[0] = b;
        p.count  = 1;
        p.state  = S_HEADER;
      }
      break;

    // ── 헤더 수집 (SOF 이후 5 bytes) ─────────────────────
    case S_HEADER:
      p.buf[p.count++] = b;
      if (p.count == PKT_HDR_SIZE) {           // 헤더 6바이트 완료
        p.payLen = p.buf[5];                   // LEN 필드
        if (p.payLen > MAX_PAYLOAD) {
          Serial.println("[RX ERR] LEN 초과 — 리셋");
          p.state = S_WAIT_SOF; p.count = 0;
        } else {
          p.state = (p.payLen > 0) ? S_PAYLOAD : S_CRC_HI;
        }
      }
      break;

    // ── 페이로드 수집 ─────────────────────────────────────
    case S_PAYLOAD:
      p.buf[p.count++] = b;
      if (p.count == PKT_HDR_SIZE + p.payLen)
        p.state = S_CRC_HI;
      break;

    // ── CRC 상위 바이트 ───────────────────────────────────
    case S_CRC_HI:
      p.crcHi = b;
      p.state = S_CRC_LO;
      break;

    // ── CRC 하위 바이트 → 검증 & 라우팅 ──────────────────
    case S_CRC_LO: {
      uint16_t rxCrc  = ((uint16_t)p.crcHi << 8) | b;
      uint16_t calCrc = calcCRC16(p.buf, PKT_HDR_SIZE + p.payLen);

      if (rxCrc == calCrc) {
        routePacket(p.buf, PKT_HDR_SIZE + p.payLen, fromArduino);
      } else {
        Serial.printf("[RX ERR] CRC 불일치 | calc=0x%04X recv=0x%04X | %s\n",
                      calCrc, rxCrc, fromArduino ? "ARD" : "TCP");
        uint8_t mtype = p.buf[1];
        uint8_t seq   = p.buf[4];
        if (fromArduino) sendNakToArduino(mtype, seq, 0); // reason=0: CRC오류
        else             sendNakToTcp(mtype, seq, 0);
      }
      // 파서 리셋
      p.state = S_WAIT_SOF;
      p.count = 0;
      break;
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  ■ ACK 타임아웃 / 재전송 핸들러
//  loop()에서 매 틱 호출
// ═══════════════════════════════════════════════════════════
void handleAckTimeout() {
  if (!g_pendingCmd.active) return;

  if (millis() - g_pendingCmd.sentMs >= ACK_TIMEOUT_MS) {
    if (g_pendingCmd.retries < ACK_MAX_RETRY) {
      g_pendingCmd.retries++;
      g_pendingCmd.sentMs = millis();
      Serial.printf("[ACK_WAIT] 타임아웃 → 재전송 #%d seq=0x%02X\n",
                    g_pendingCmd.retries, g_pendingCmd.seq);
      resendToArduino(g_pendingCmd.pkt, g_pendingCmd.pktLen);
    } else {
      // 최대 재전송 초과 → 포기 + 오류 로그
      Serial.printf("[ACK_WAIT] 재전송 %d회 실패 seq=0x%02X — 포기\n",
                    ACK_MAX_RETRY, g_pendingCmd.seq);
      // 서버에 오류 보고 (ERROR_REPORT 0xF0, severity=2 WARN)
      if (g_tcp.connected()) {
        uint8_t pay[4] = {
          0x00, 0x01,   // error_id=1 (통신 오류)
          0x02,         // severity=2 WARN
          0x04          // context bit2=통신이상
        };
        uint8_t buf[PKT_HDR_SIZE + 4];
        buildBridgePkt(buf, MSG_ERROR_REPORT, ID_SERVER, pay, 4);
        sendToTcp(buf, PKT_HDR_SIZE + 4);
      }
      clearPendingCmd();
    }
  }
}

// ═══════════════════════════════════════════════════════════
//  ■ setup()
// ═══════════════════════════════════════════════════════════
void setup() {
  // USB 디버그 시리얼
  Serial.begin(BAUD_DEBUG);
  delay(500);
  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║ Smart Farm ESP32 Serial-TCP Bridge  v1.0  ║");
  Serial.println("║  Arduino(UART2) <-> WiFi TCP              ║");
  Serial.println("╚══════════════════════════════════════════╝");

  // Arduino UART2 초기화
  Serial2.begin(BAUD_ARDUINO, SERIAL_8N1, PIN_ARD_RX, PIN_ARD_TX);
  Serial.printf("[UART2] RX=GPIO%d TX=GPIO%d  @%d bps\n",
                PIN_ARD_RX, PIN_ARD_TX, BAUD_ARDUINO);

  // WiFi 연결
  wifiConnect();

  // TCP 연결
  tcpConnect();

  g_lastReconMs = millis();
  g_lastHbMs    = millis();

  Serial.println("[READY] 브리지 동작 시작");
  Serial.println("────────────────────────────────────────────");
}

// ═══════════════════════════════════════════════════════════
//  ■ loop()
// ═══════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ──────────────────────────────────────────────────────
  //  1) WiFi / TCP 재접속 (RECONNECT_INTERVAL_MS 주기)
  // ──────────────────────────────────────────────────────
  if (now - g_lastReconMs >= RECONNECT_INTERVAL_MS) {
    g_lastReconMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("[WiFi] 연결 끊김 — 재연결 시도");
      wifiConnect();
    } else if (!g_tcp.connected()) {
      Serial.println("[TCP]  연결 끊김 — 재연결 시도");
      tcpConnect();
    }
  }

  // ──────────────────────────────────────────────────────
  //  2) Arduino UART2 수신 처리
  // ──────────────────────────────────────────────────────
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    processByte(g_ardParser, b, true);
  }

  // ──────────────────────────────────────────────────────
  //  3) TCP 수신 처리
  // ──────────────────────────────────────────────────────
  while (g_tcp.available()) {
    uint8_t b = (uint8_t)g_tcp.read();
    processByte(g_tcpParser, b, false);
  }

  // ──────────────────────────────────────────────────────
  //  4) ACK 타임아웃 / 재전송 체크
  // ──────────────────────────────────────────────────────
  handleAckTimeout();

  // ──────────────────────────────────────────────────────
  //  5) USB 디버그 명령 처리  ('S'→상태, 'R'→리셋)
  // ──────────────────────────────────────────────────────
  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'S' || c == 's') {
      Serial.println("────────── BRIDGE STATUS ──────────");
      Serial.printf("WiFi   : %s (%s)\n",
                    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE",
                    WiFi.localIP().toString().c_str());
      Serial.printf("TCP    : %s\n", g_tcp.connected() ? "CONNECTED" : "DISCONNECTED");
      Serial.printf("BridgeSeq : 0x%02X\n", g_bridgeSeq);
      Serial.printf("PendingCmd: %s (seq=0x%02X retries=%d)\n",
                    g_pendingCmd.active ? "ACTIVE" : "NONE",
                    g_pendingCmd.seq, g_pendingCmd.retries);
      Serial.println("────────────────────────────────────");
    }
    else if (c == 'R' || c == 'r') {
      Serial.println("[CMD] ESP32 소프트 리셋");
      delay(100);
      ESP.restart();
    }
  }
}