/**
 * RobotNetworkManager.cpp
 * =======================
 * ESP32 로봇 펌웨어용 네트워크 통신 매니저 구현 파일.
 *
 * ArduinoJson 라이브러리를 사용하여 TCP/UDP JSON 통신을 처리한다.
 * 핸들러 내부의 비즈니스 로직(모터 구동, 센서 읽기 등)은 팀원이 구현할 것.
 * [수정됨] ESP32 기본 라이브러리 충돌을 막기 위해
 * 클래스 이름이 RobotNetworkManager로 변경되었습니다.
 */

#include "RobotNetworkManager.h"

// ── 기본 포트 설정 ──
static const uint16_t DEFAULT_UDP_PORT = 9000;

// ============================================================
//  생성자 / 소멸자
// ============================================================

RobotNetworkManager::RobotNetworkManager()
    : _serverIP(nullptr)
    , _serverPort(0)
    , _udpPort(DEFAULT_UDP_PORT)
    , _msgCount(0)
    , _lastReconnectAttempt(0)
    , _motorController()
    , _lineFollower(_motorController)
{
    memset(_recvBuffer, 0, sizeof(_recvBuffer));
    Serial.println("[RobotNetworkManager] 초기화 완료");
}

// ============================================================
//  하드웨어 초기화
// ============================================================

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
//  Wi-Fi 연결
// ============================================================

bool RobotNetworkManager::connectWiFi(const char* ssid, const char* password) {
    Serial.printf("[RobotNetworkManager] Wi-Fi 연결 시도: %s\n", ssid);

    WiFi.begin(ssid, password);

    // 최대 10초간 연결 대기
    int timeout = 20;  // 500ms × 20 = 10초
    while (WiFi.status() != WL_CONNECTED && timeout > 0) {
        delay(500);
        Serial.print(".");
        timeout--;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[RobotNetworkManager] Wi-Fi 연결 성공! IP: %s, RSSI: %ddBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        return true;
    } else {
        Serial.println("\n[RobotNetworkManager] Wi-Fi 연결 실패");
        return false;
    }
}

// ============================================================
//  서버 TCP 연결
// ============================================================

bool RobotNetworkManager::connectToServer(const char* serverIP, uint16_t serverPort) {
    _serverIP = serverIP;
    _serverPort = serverPort;

    Serial.printf("[RobotNetworkManager] 서버 TCP 연결 시도: %s:%d\n", serverIP, serverPort);

    if (_tcpClient.connect(serverIP, serverPort)) {
        Serial.println("[RobotNetworkManager] 서버 연결 성공");
        return true;
    } else {
        Serial.println("[RobotNetworkManager] 서버 연결 실패");
        return false;
    }
}

// ============================================================
//  서버 연결 유지 (자동 복구)
// ============================================================

void RobotNetworkManager::maintainConnection() {
    // Wi-Fi는 연결되어 있는데, 서버 소켓이 끊어진 경우에만 재연결 시도
    if (WiFi.status() != WL_CONNECTED || _tcpClient.connected()) {
        return;
    }
    if (_serverIP == nullptr) {
        return;
    }

    unsigned long now = millis();
    const unsigned long RECONNECT_INTERVAL = 5000;  // 5초 간격

    if (now - _lastReconnectAttempt >= RECONNECT_INTERVAL) {
        _lastReconnectAttempt = now;
        Serial.println("[RobotNetworkManager] 서버와 연결이 끊어졌습니다. 재연결을 시도합니다...");
        if (_tcpClient.connect(_serverIP, _serverPort)) {
            Serial.println("[RobotNetworkManager] 서버 재연결 성공");
        } else {
            Serial.println("[RobotNetworkManager] 서버 재연결 실패");
        }
    }
}

// ============================================================
//  메인 루프: TCP 수신 데이터 처리
// ============================================================

void RobotNetworkManager::handleIncoming() {
    // 라인트레이싱 업데이트 (매 사이클 실행)
    _lineFollower.update();

    // RFID 태그 읽기 (매 사이클 실행)
    _rfidReader.readTag();

    // 서버 연결 유지 (끊어졌으면 재연결 시도)
    maintainConnection();

    // TCP 소켓에 수신 데이터가 있는지 확인
    if (!_tcpClient.connected() || !_tcpClient.available()) {
        return;
    }

    // ── 수신 버퍼에 데이터 읽기 ──
    int len = _tcpClient.readBytesUntil('\n', _recvBuffer, sizeof(_recvBuffer) - 1);
    _recvBuffer[len] = '\0';

    String rawData = String(_recvBuffer);
    Serial.printf("[RobotNetworkManager] 수신: %s\n", rawData.c_str());

    // ── JSON 파싱 ──
    JsonDocument doc;
    if (!parseCommand(rawData, doc)) {
        sendResponse("FAIL", "JSON 파싱 실패");
        return;
    }

    // ── cmd 필드에 따라 핸들러 분기 ──
    const char* cmd = doc["cmd"];

    if (strcmp(cmd, "MOVE") == 0) {
        handleMove(doc);

    } else if (strcmp(cmd, "GOTO") == 0) {
        handleGoto(doc);

    } else if (strcmp(cmd, "SET_LOC") == 0) {
        handleSetLoc(doc);

    } else if (strcmp(cmd, "TASK") == 0) {
        handleTask(doc);

    } else if (strcmp(cmd, "MANUAL") == 0) {
        handleManual(doc);

    } else {
        Serial.printf("[RobotNetworkManager] 알 수 없는 명령: %s\n", cmd);
        sendResponse("FAIL", "알 수 없는 명령");
    }
}

// ============================================================
//  JSON 파싱
// ============================================================

bool RobotNetworkManager::parseCommand(const String& rawData, JsonDocument& doc) {
    DeserializationError error = deserializeJson(doc, rawData);

    if (error) {
        Serial.printf("[RobotNetworkManager] JSON 파싱 오류: %s\n", error.c_str());
        return false;
    }

    return true;
}

// ============================================================
//  로봇 상태 TCP 전송
// ============================================================

void RobotNetworkManager::broadcastRobotState(const char* robotId, int posX, int posY, int battery) {
    /*
     * 서버에 로봇의 현재 상태를 TCP로 전송한다.
     *
     * 송신 포맷:
     *   {"type": "ROBOT_STATE", "robot_id": "R01", "pos_x": 120, "pos_y": 350, "battery": 80,
     *    "state": 1, "node_idx": 0, "dir": 1, "sensors": [0,1,1,1,0], "plant_id": "..."}
     */

    // TCP 연결 확인 (연결 없을 때는 조용히 스킵, 재연결은 maintainConnection이 담당)
    if (!_tcpClient.connected()) {
        return;
    }

    // 센서 값 조회
    int s1, s2, s3, s4, s5;
    _lineFollower.getSensorValues(s1, s2, s3, s4, s5);

    // JSON 문서 생성
    JsonDocument doc;
    doc["type"]     = "ROBOT_STATE";
    doc["robot_id"] = robotId;
    doc["count"]    = _msgCount++;
    doc["pos_x"]    = posX;
    doc["pos_y"]    = posY;
    doc["battery"]  = battery;

    // 라인트레이싱 상태 추가
    doc["state"]    = static_cast<int>(_lineFollower.getState());
    doc["node_idx"] = _lineFollower.getCurrentNodeIndex();
    doc["dir"]      = _lineFollower.getCurrentDirection();

    // 센서 배열 추가
    JsonArray sensors = doc["sensors"].to<JsonArray>();
    sensors.add(s1);
    sensors.add(s2);
    sensors.add(s3);
    sensors.add(s4);
    sensors.add(s5);

    // RFID 태그 정보 추가 (식물 ID)
    String plantId = _rfidReader.getLastTagUID();
    doc["plant_id"] = plantId.length() > 0 ? plantId : "";

    // JSON → 문자열 직렬화
    char jsonBuffer[512];
    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

    // TCP로 전송
    _tcpClient.println(jsonBuffer);

    Serial.printf("[RobotNetworkManager] 상태 전송(TCP): %s\n", jsonBuffer);
}

// ============================================================
//  TCP 응답 전송
// ============================================================

void RobotNetworkManager::sendResponse(const char* status, const char* msg) {
    /*
     * 서버에 명령 처리 결과를 TCP로 응답한다.
     *
     * 응답 포맷:
     *   {"status": "SUCCESS", "msg": "도착 완료"}
     */

    JsonDocument doc;
    doc["status"] = status;
    doc["msg"]    = msg;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));

    _tcpClient.println(jsonBuffer);
    Serial.printf("[RobotNetworkManager] 응답 전송: %s\n", jsonBuffer);
}

// ============================================================
//  명령 핸들러 (뼈대 – 팀원이 내부 로직 구현)
// ============================================================

void RobotNetworkManager::handleMove(JsonDocument& doc) {
    /*
     * 이동 명령 처리.
     *
     * 수신 포맷 1 (경로): {"cmd": "MOVE", "path": "12345"}
     *   - 1=L(좌회전), 2=R(우회전), 3=U(U턴), 4=S(직진), 5=E(종료)
     *   - 라인트레이싱으로 경로 추종 시작
     *
     * 수신 포맷 2 (노드): {"cmd": "MOVE", "target_node": "NODE-A1-001"}
     *   - 기존 방식 (미구현)
     */

    // 경로 기반 이동 (path 필드가 있는 경우)
    if (doc.containsKey("path")) {
        const char* path = doc["path"];
        unsigned int backMs = doc["crossroad_backward_ms"] | 0;
        Serial.printf("[RobotNetworkManager] 경로 이동 명령 수신 → 경로: %s, 교차로후진: %ums\n", path, backMs);

        _lineFollower.setCrossroadBackwardMs(backMs);
        _lineFollower.setPath(path);
        _lineFollower.start();

        sendResponse("SUCCESS", "경로 추종 시작");
        return;
    }

    // 노드 기반 이동 (target_node 필드가 있는 경우)
    if (doc.containsKey("target_node")) {
        const char* targetNode = doc["target_node"];
        Serial.printf("[RobotNetworkManager] 노드 이동 명령 수신 → 목표: %s\n", targetNode);

        // TODO: 노드 좌표 조회 및 이동 로직 구현
        sendResponse("SUCCESS", "노드 이동 명령 수신 확인");
        return;
    }

    Serial.println("[RobotNetworkManager] MOVE 명령에 path 또는 target_node 필드 없음");
    sendResponse("FAIL", "path 또는 target_node 필드 필요");
}

void RobotNetworkManager::handleGoto(JsonDocument& doc) {
    /*
     * GOTO 명령: PathFinder로 경로 계산 후 LineFollower로 이동.
     * {"cmd": "GOTO", "target": 5} - 노드 인덱스
     * {"cmd": "GOTO", "target_node": "s06"} - 노드 이름
     */
    int targetIdx = -1;
    if (doc.containsKey("target")) {
        targetIdx = doc["target"].as<int>();
    } else if (doc.containsKey("target_node")) {
        const char* nodeName = doc["target_node"];
        targetIdx = _pathFinder.nodeNameToIndex(nodeName);
    }

    if (targetIdx < 0 || targetIdx >= _pathFinder.getNodeCount()) {
        sendResponse("FAIL", "잘못된 목표 노드");
        return;
    }

    int startIdx = _lineFollower.getCurrentNodeIndex();
    int startDir = _lineFollower.getCurrentDirection();

    char pathBuf[64];
    int nodeSeq[16];
    int len = _pathFinder.calculatePath(startIdx, targetIdx, startDir, pathBuf, nodeSeq, 16);

    if (len < 0) {
        sendResponse("FAIL", "경로 탐색 실패");
        return;
    }

    // 노드 시퀀스 길이 계산 (nodeSeq에서 -1 또는 경로길이+1)
    int nodeCount = 0;
    for (int i = 0; i < 16 && nodeSeq[i] >= 0; i++) nodeCount++;

    unsigned int backMs = doc["crossroad_backward_ms"] | 0;
    _lineFollower.setCrossroadBackwardMs(backMs);
    _lineFollower.setPath(String(pathBuf), nodeSeq, nodeCount);
    _lineFollower.start();

    Serial.printf("[RobotNetworkManager] GOTO %d → 경로: %s, 교차로후진: %ums\n", targetIdx, pathBuf, backMs);
    sendResponse("SUCCESS", "GOTO 경로 추종 시작");
}

void RobotNetworkManager::handleSetLoc(JsonDocument& doc) {
    /*
     * SET_LOC 명령: 현재 위치 수동 설정 (역추적/초기화용).
     * {"cmd": "SET_LOC", "node": 0, "dir": 1}
     */
    if (!doc.containsKey("node") || !doc.containsKey("dir")) {
        sendResponse("FAIL", "node, dir 필드 필요");
        return;
    }

    int nodeIdx = doc["node"].as<int>();
    int dir = doc["dir"].as<int>();

    if (nodeIdx < 0 || nodeIdx >= _pathFinder.getNodeCount()) {
        sendResponse("FAIL", "잘못된 노드 인덱스");
        return;
    }

    dir = (dir % 4 + 4) % 4;
    _lineFollower.setLocation(nodeIdx, dir, _pathFinder.indexToNodeName(nodeIdx));

    Serial.printf("[RobotNetworkManager] SET_LOC node=%d dir=%d\n", nodeIdx, dir);
    sendResponse("SUCCESS", "위치 설정 완료");
}

bool RobotNetworkManager::setLocationByNodeName(const char* nodeName, int dir) {
    if (nodeName == nullptr) {
        Serial.println("[RobotNetworkManager] setLocationByNodeName: nodeName is null");
        return false;
    }

    int nodeIdx = _pathFinder.nodeNameToIndex(nodeName);
    if (nodeIdx < 0 || nodeIdx >= _pathFinder.getNodeCount()) {
        Serial.printf("[RobotNetworkManager] setLocationByNodeName: 잘못된 노드 이름: %s\n", nodeName);
        return false;
    }

    dir = (dir % 4 + 4) % 4;
    _lineFollower.setLocation(nodeIdx, dir, _pathFinder.indexToNodeName(nodeIdx));
    Serial.printf("[RobotNetworkManager] setLocationByNodeName: %s (idx=%d) dir=%d\n",
                  nodeName, nodeIdx, dir);
    return true;
}

void RobotNetworkManager::handleTask(JsonDocument& doc) {
    /*
     * 작업 명령 처리 (Pick-and-Place 등).
     * 수신: {"cmd": "TASK", "action": "PICK_AND_PLACE", "count": 5}
     *
     * TODO (팀원 구현):
     *   1) action, count 값 추출
     *   2) action이 "PICK_AND_PLACE"인 경우:
     *   3) 작업 완료 후 sendResponse() 호출
     */
    const char* action = doc["action"];
    int count = doc["count"] | 1;  // 기본값 1
    Serial.printf("[RobotNetworkManager] 작업 명령 수신 → 동작: %s, 횟수: %d\n", action, count);

    // TODO: so-arm 제어 로직 구현
    // ArmController::pickAndPlace(count);

    sendResponse("SUCCESS", "작업 명령 수신 확인");
}

void RobotNetworkManager::handleManual(JsonDocument& doc) {
    /*
     * 수동 제어 명령 처리.
     * 수신: {"cmd": "MANUAL", "device": "FAN", "state": "ON"}
     *
     * TODO (팀원 구현):
     *   1) device, state 값 추출
     *   2) device에 해당하는 GPIO 핀 번호 매핑
     *   3) state가 "ON"이면 HIGH, "OFF"이면 LOW로 핀 출력
     *   4) 제어 완료 후 sendResponse() 호출
     */
    const char* device = doc["device"];
    const char* state  = doc["state"];
    Serial.printf("[RobotNetworkManager] 수동 제어 수신 → 장치: %s, 상태: %s\n", device, state);

    // TODO: GPIO 핀 제어 로직 구현
    // int pin = getPinForDevice(device);
    // digitalWrite(pin, strcmp(state, "ON") == 0 ? HIGH : LOW);

    sendResponse("SUCCESS", "수동 제어 수신 확인");
}
