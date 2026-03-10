/**
 * RobotNetworkManager.cpp
 * =======================
 * ESP32 로봇 펌웨어용 네트워크 통신 매니저 구현 파일.
 *
 * SFAM Serial Packet Protocol Specification v1.0 호환.
 * CRC16-CCITT 바이너리 패킷을 TCP를 통해 송수신합니다.
 */

#include "RobotNetworkManager.h"

static const uint16_t DEFAULT_UDP_PORT = 9000;
#define HB_INTERVAL_MS 5000

// ============================================================
//  생성자 / 소멸자
// ============================================================

RobotNetworkManager::RobotNetworkManager()
    : _serverIP(nullptr)
    , _serverPort(0)
    , _udpPort(DEFAULT_UDP_PORT)
    , _robotId(0x01)
    , _tcpSeq(0)
    , _lastReconnectAttempt(0)
    , _lastHeartbeatMs(0)
    , _motorController()
    , _lineFollower(_motorController)
{
    memset((void*)&_rxParser, 0, sizeof(ProtocolRxParser));
    Serial.println("[RobotNetworkManager] 송수신(TCP Binary Protocol) 초기화 완료");
}

void RobotNetworkManager::initHardware() {
    _motorController.init();
    _rfidReader.init();
    _pathFinder.initGraph();
    Serial.println("[RobotNetworkManager] 하드웨어 초기화 완료");
}

RobotNetworkManager::~RobotNetworkManager() {
    _tcpClient.stop();
    Serial.println("[RobotNetworkManager] 소멸자 – 연결 해제");
}

// ============================================================
//  Wi-Fi / 서버 연결
// ============================================================

bool RobotNetworkManager::connectWiFi(const char* ssid, const char* password) {
    Serial.printf("[RobotNetworkManager] Wi-Fi %s\n", ssid);
    WiFi.begin(ssid, password);
    int timeout = 20;
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500); Serial.print("."); timeout--;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] OK IP: %s\n", WiFi.localIP().toString().c_str());
        return true;
    }
    return false;
}

bool RobotNetworkManager::connectToServer(const char* serverIP, uint16_t serverPort) {
    _serverIP = serverIP;
    _serverPort = serverPort;
    if (_tcpClient.connect(serverIP, serverPort)) {
        _tcpClient.setNoDelay(true);
        Serial.println("[TCP] 서버 연결 성공");
        return true;
    }
    return false;
}

void RobotNetworkManager::maintainConnection() {
    if (WiFi.status() != WL_CONNECTED || _tcpClient.connected()) return;
    if (_serverIP == nullptr) return;

    unsigned long now = millis();
    if (now - _lastReconnectAttempt >= 5000) {
        _lastReconnectAttempt = now;
        Serial.println("[TCP] 재연결 시도...");
        _tcpClient.connect(_serverIP, _serverPort);
    }
}

// ============================================================
//  메인 루프: 수신 파서 및 송신 트리거
// ============================================================

void RobotNetworkManager::handleIncoming() {
    _lineFollower.update();
    _rfidReader.readTag();
    maintainConnection();

    unsigned long now = millis();
    // 하트비트 전송
    if (_tcpClient.connected() && (now - _lastHeartbeatMs >= HB_INTERVAL_MS)) {
        _lastHeartbeatMs = now;
        sendPayload(MSG_HEARTBEAT_REQ, ID_SERVER, nullptr, 0);
    }

    if (!_tcpClient.connected() || !_tcpClient.available()) return;

    // 바이너리 패킷 파서
    while (_tcpClient.available()) {
        uint8_t b = _tcpClient.read();
        processByte(b);
    }
}

// ============================================================
//  Tx Functions (송신)
// ============================================================

void RobotNetworkManager::sendPayload(uint8_t msgType, uint8_t dstId, const uint8_t* payload, uint8_t len) {
    if (!_tcpClient.connected()) return;

    uint8_t buf[PKT_MAX_FULL];
    buf[0] = SOF;
    buf[1] = msgType;
    buf[2] = _robotId;
    buf[3] = dstId;
    buf[4] = _tcpSeq++;
    buf[5] = len;

    if (len > 0 && payload != nullptr) {
        memcpy(buf + 6, payload, len);
    }

    uint8_t pktLen = makeFullPacket(buf, PKT_HDR_SIZE + len, buf);
    _tcpClient.write(buf, pktLen);
}

void RobotNetworkManager::sendAck(uint8_t ackedType, uint8_t seq) {
    uint8_t pay[2] = { ackedType, seq };
    sendPayload(MSG_ACK, ID_SERVER, pay, 2);
    Serial.printf("[Tx] ACK (Type:0x%02X, Seq:%d)\n", ackedType, seq);
}

void RobotNetworkManager::sendNak(uint8_t nackedType, uint8_t seq, uint8_t reason) {
    uint8_t pay[3] = { nackedType, seq, reason };
    sendPayload(MSG_NAK, ID_SERVER, pay, 3);
    Serial.printf("[Tx] NAK (Type:0x%02X, Seq:%d, Rsn:%d)\n", nackedType, seq, reason);
}

void RobotNetworkManager::broadcastRobotState() {
    // 0x10 AGV_TELEMETRY (8 bytes)
    int s1, s2, s3, s4, s5;
    _lineFollower.getSensorValues(s1, s2, s3, s4, s5);
    uint8_t lineMask = (s1<<4) | (s2<<3) | (s3<<2) | (s4<<1) | s5;

    uint8_t pay[8] = {0};
    pay[0] = static_cast<uint8_t>(_lineFollower.getState()); // status_id
    pay[1] = 100; // battery_level (가짜 100%)
    pay[2] = _lineFollower.getCurrentNodeIndex();
    pay[3] = 0x00; // task_id_hi (가짜)
    pay[4] = 0x00; // task_id_lo (가짜)
    pay[5] = lineMask;
    pay[6] = 200;  // motor_pwm 가짜
    pay[7] = 0x00; // error_id

    sendPayload(MSG_AGV_TELEMETRY, ID_SERVER, pay, sizeof(pay));
}

// ============================================================
//  Rx State Machine (수신)
// ============================================================

void RobotNetworkManager::processByte(uint8_t b) {
    switch (_rxParser.state) {
        case S_WAIT_SOF:
            if (b == SOF) {
                _rxParser.buf[0] = b;
                _rxParser.count = 1;
                _rxParser.state = S_HEADER;
            }
            break;

        case S_HEADER:
            _rxParser.buf[_rxParser.count++] = b;
            if (_rxParser.count == PKT_HDR_SIZE) {
                _rxParser.payLen = _rxParser.buf[5];
                if (_rxParser.payLen > MAX_PAYLOAD) {
                    _rxParser.state = S_WAIT_SOF;
                } else {
                    _rxParser.state = (_rxParser.payLen > 0) ? S_PAYLOAD : S_CRC_HI;
                }
            }
            break;

        case S_PAYLOAD:
            _rxParser.buf[_rxParser.count++] = b;
            if (_rxParser.count == PKT_HDR_SIZE + _rxParser.payLen) {
                _rxParser.state = S_CRC_HI;
            }
            break;

        case S_CRC_HI:
            _rxParser.crcHi = b;
            _rxParser.state = S_CRC_LO;
            break;

        case S_CRC_LO:
            uint16_t rxCrc = ((uint16_t)_rxParser.crcHi << 8) | b;
            uint16_t calCrc = calcCRC16(_rxParser.buf, PKT_HDR_SIZE + _rxParser.payLen);

            if (rxCrc == calCrc) {
                routePacket(_rxParser.buf, PKT_HDR_SIZE + _rxParser.payLen);
            } else {
                Serial.printf("[Rx] CRC Fail. Calc:0x%04X, Got:0x%04X\n", calCrc, rxCrc);
                sendNak(_rxParser.buf[1], _rxParser.buf[4], 0); // reason=0 (CRC ERR)
            }

            _rxParser.state = S_WAIT_SOF;
            _rxParser.count = 0;
            break;
    }
}

// ============================================================
//  Packet Router
// ============================================================

void RobotNetworkManager::routePacket(const uint8_t* buf, uint8_t hdrPayLen) {
    uint8_t msgType = buf[1];
    uint8_t srcId   = buf[2];
    uint8_t dstId   = buf[3];
    uint8_t seq     = buf[4];
    uint8_t payLen  = buf[5];
    const uint8_t* payload = buf + 6;

    // 자신을 시도하거나 브로드캐스트가 아니면 무시
    if (dstId != _robotId && dstId != ID_BROADCAST) return;

    Serial.printf("[Rx] Type:0x%02X, Seq:%d, Len:%d\n", msgType, seq, payLen);

    switch (msgType) {
        case MSG_HEARTBEAT_REQ:
            {
                uint8_t pay[2] = {1, 0}; // ONLINE
                sendPayload(MSG_HEARTBEAT_ACK, srcId, pay, 2);
            }
            break;

        case MSG_AGV_TASK_CMD:
            handleTaskCmd(payload, payLen);
            sendAck(msgType, seq);
            break;

        case MSG_ACTUATOR_CMD:
            handleActuatorCmd(payload, payLen);
            sendAck(msgType, seq);
            break;

        default:
            Serial.printf("[Rx] Unhandled MsgType: 0x%02X\n", msgType);
            sendNak(msgType, seq, 1); // 1: Not Supported
            break;
    }
}

// ============================================================
//  Handlers
// ============================================================

void RobotNetworkManager::handleTaskCmd(const uint8_t* payload, uint8_t len) {
    if (len < 10) return;
    
    uint16_t taskId = (payload[0] << 8) | payload[1];
    uint8_t srcNode = payload[4];
    uint8_t dstNode = payload[5];

    Serial.printf("[TASK_CMD] TaskID:%d, Src:%d, Dst:%d\n", taskId, srcNode, dstNode);

    // GOTO 경로 탐색 및 실행
    int startIdx = _lineFollower.getCurrentNodeIndex();
    int startDir = _lineFollower.getCurrentDirection();
    char pathBuf[64];
    int nodeSeq[16];

    int pLen = _pathFinder.calculatePath(startIdx, (int)dstNode, startDir, pathBuf, nodeSeq, 16);
    if (pLen >= 0) {
        int nodeCount = 0;
        for (int i=0; i<16 && nodeSeq[i]>=0; i++) nodeCount++;
        _lineFollower.setPath(String(pathBuf), nodeSeq, nodeCount);
        _lineFollower.start();

        uint8_t ackPay[3] = { payload[0], payload[1], 1 }; // 1: 수락
        sendPayload(MSG_AGV_TASK_ACK, ID_SERVER, ackPay, 3);
    } else {
        uint8_t ackPay[3] = { payload[0], payload[1], 0 }; // 0: 거부
        sendPayload(MSG_AGV_TASK_ACK, ID_SERVER, ackPay, 3);
    }
}

void RobotNetworkManager::handleActuatorCmd(const uint8_t* payload, uint8_t len) {
    if (len < 4) return;
    uint8_t actId = payload[0];
    uint8_t stateVal = payload[1];
    Serial.printf("[ACT_CMD] ActID:%d, State:%d\n", actId, stateVal);

    // TODO: GPIO 처리 로직 (이전 MANUAL 커맨드 처리 역할)
    // digitalWrite(actId, stateVal ? HIGH : LOW);
}

bool RobotNetworkManager::setLocationByNodeName(const char* nodeName, int dir) {
    int nodeIdx = _pathFinder.nodeNameToIndex(nodeName);
    if (nodeIdx < 0 || nodeIdx >= _pathFinder.getNodeCount()) return false;
    _lineFollower.setLocation(nodeIdx, dir, _pathFinder.indexToNodeName(nodeIdx));
    return true;
}
