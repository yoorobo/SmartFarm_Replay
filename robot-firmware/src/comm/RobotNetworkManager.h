/**
 * RobotNetworkManager.h
 * =====================
 * ESP32 로봇 펌웨어용 네트워크 통신 매니저 헤더 파일.
 *
 * 역할:
 *   - Wi-Fi 연결 관리
 *   - 중앙 서버와 TCP 통신 (제어 명령 수신 / 응답 전송)
 *   - 서버로 TCP 상태 전송 (위치, 배터리 등)
 *   - ArduinoJson 라이브러리를 이용한 JSON 파싱/생성
 *   - 라인트레이싱 경로 추종 제어
 *
 * [수신 명령 포맷 – TCP]
 *   이동(경로): {"cmd": "MOVE", "path": "12345"}  (1=L, 2=R, 3=U, 4=S, 5=E)
 *   이동(노드): {"cmd": "MOVE", "target_node": "NODE-A1-001"}
 *   작업:  {"cmd": "TASK", "action": "PICK_AND_PLACE", "count": 5}
 *   수동:  {"cmd": "MANUAL", "device": "FAN", "state": "ON"}
 *
 * [송신 응답 포맷 – TCP]
 *   {"status": "SUCCESS", "msg": "도착 완료"}
 *
 * [송신 상태 포맷 – TCP]
 *   {"type": "ROBOT_STATE", "robot_id": "R01", "pos_x": 120, "pos_y": 350, "battery": 80,
 *    "state": 1, "node": "A1", "sensors": [0,1,1,1,0], "plant_id": "A1B2C3D4"}
 */

#ifndef ROBOT_NETWORK_MANAGER_H
#define ROBOT_NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

#include "SFAM_Protocol.h"
#include "../motor/MotorController.h"
#include "../line/LineFollower.h"
#include "../path/PathFinder.h"
#include "../rfid/RFIDReader.h"
#include "../motor/ServoArmController.h"

/**
 * @brief ESP32 로봇의 네트워크 통신을 총괄하는 매니저 클래스.
 *
 * 팀원 가이드:
 *   - 명령 수신 콜백을 등록하면, TCP 명령이 들어올 때 자동으로 호출됩니다.
 *   - 상태 전송은 주기적으로 broadcastRobotState()를 호출하세요.
 *   - [중요] ESP32 기본 라이브러리와의 이름 충돌을 피하기 위해
 *     기존 NetworkManager에서 RobotNetworkManager로 이름이 변경되었습니다.
 */
class RobotNetworkManager {
public:
    // ─────────── 생성자 / 소멸자 ───────────
    RobotNetworkManager();
    ~RobotNetworkManager();

    // ─────────── 초기화 ───────────
    /**
     * @brief 모터 컨트롤러 및 라인트레이서 초기화.
     *        setup()에서 Wi-Fi 연결 전에 호출해야 함.
     */
    void initHardware();

    /**
     * @brief 암/그리퍼 활성화 여부 설정. ENABLE_ARM 플래그로 제어.
     * @param enabled true=활성, false=비활성 (기본값)
     */
    void setArmEnabled(bool enabled);

    // ─────────── Wi-Fi 연결 ───────────
    /**
     * @brief Wi-Fi에 연결한다.
     * @param ssid     Wi-Fi SSID
     * @param password Wi-Fi 비밀번호
     * @return 연결 성공 여부
     */
    bool connectWiFi(const char* ssid, const char* password);

    // ─────────── 서버 연결 (TCP) ───────────
    /**
     * @brief 중앙 서버에 TCP 연결한다.
     * @param serverIP   서버 IP 주소
     * @param serverPort 서버 TCP 포트 번호
     * @return 연결 성공 여부
     */
    bool connectToServer(const char* serverIP, uint16_t serverPort);

    // ─────────── 서버 연결 유지 (자동 복구) ───────────
    /**
     * @brief 서버와 TCP 연결이 끊어졌을 경우 주기적으로 재연결을 시도한다.
     *        loop() 내부의 handleIncoming()에서 자동으로 호출됨.
     */
    void maintainConnection();

    // ─────────── 메인 루프 처리 ───────────
    /**
     * @brief loop()에서 매 사이클 호출.
     *        TCP 소켓에서 데이터를 수신하고, 명령이 있으면 파싱하여 처리한다.
     *        또한 라인트레이싱 로직을 업데이트한다.
     */
    void handleIncoming();

    // ─────────── 라인트레이서 접근 ───────────
    /**
     * @brief LineFollower 객체 참조 반환 (외부에서 상태 조회용).
     */
    LineFollower& getLineFollower() { return _lineFollower; }
    const LineFollower& getLineFollower() const { return _lineFollower; }

    // ─────────── 로봇 상태 TCP 전송 ───────────
    /**
     * @brief 로봇의 현재 상태를 TCP로 서버에 전송한다.
     * @param robotId  로봇 식별 ID (예: "R01")
     * @param posX     현재 X 좌표
     * @param posY     현재 Y 좌표
     * @param battery  배터리 잔량 (%)
     *
     * 송신 포맷:
     *   {"type": "ROBOT_STATE", "robot_id": "R01", "pos_x": 120, "pos_y": 350, "battery": 80,
     *    "state": 1, "node_idx": 0, "dir": 1, "sensors": [0,1,1,1,0], "plant_id": "..."}
     */
    void broadcastRobotState(const char* robotId, int posX, int posY, int battery);

    // ─────────── RFID 리더 접근 ───────────
    /**
     * @brief RFIDReader 객체 참조 반환 (외부에서 상태 조회용).
     */
    RFIDReader& getRFIDReader() { return _rfidReader; }
    const RFIDReader& getRFIDReader() const { return _rfidReader; }

    // ─────────── 위치 수동 설정 (시리얼 등에서 사용) ───────────
    /**
     * @brief 노드 이름과 방향으로 현재 위치를 수동 설정한다.
     *        내부적으로 PathFinder와 LineFollower를 사용한다.
     * @param nodeName 노드 이름 (예: "s06")
     * @param dir      방향 코드 (0=N, 1=E, 2=S, 3=W)
     * @return 설정 성공 여부
     */
    bool setLocationByNodeName(const char* nodeName, int dir);

    // ─────────── TCP 응답 전송 ───────────
    /**
     * @brief 서버에 명령 처리 결과를 TCP로 응답한다.
     * @param status 처리 결과 ("SUCCESS" 또는 "FAIL")
     * @param msg    결과 메시지
     *
     * 응답 포맷:
     *   {"status": "SUCCESS", "msg": "도착 완료"}
     */
    void sendResponse(const char* status, const char* msg);

private:
    // ─────────── TCP 명령 파싱 ───────────
    /**
     * @brief 수신된 JSON 문자열을 파싱하여 ArduinoJson Document로 변환한다.
     * @param rawData  수신된 원시 문자열
     * @param doc      파싱 결과를 저장할 JsonDocument 참조
     * @return 파싱 성공 여부
     */
    bool parseCommand(const String& rawData, JsonDocument& doc);

    // ─────────── 명령별 핸들러 (팀원이 내부 로직 구현) ───────────

    /**
     * @brief 이동 명령 처리.
     *        수신 포맷 1 (경로): {"cmd": "MOVE", "path": "12345"}
     *        수신 포맷 2 (노드): {"cmd": "MOVE", "target_node": "NODE-A1-001"}
     *
     *   path 필드가 있으면 라인트레이싱 경로 추종 시작.
     *   target_node 필드만 있으면 기존 방식으로 처리.
     */
    void handleMove(JsonDocument& doc);

    /**
     * @brief GOTO 명령 처리.
     *        {"cmd": "GOTO", "target": 5} 또는 {"cmd": "GOTO", "target_node": "s06"}
     */
    void handleGoto(JsonDocument& doc);

    /**
     * @brief SET_LOC 명령 처리.
     *        {"cmd": "SET_LOC", "node": 0, "dir": 1}
     *
     *        사용 시점:
     *        - 전원 켠 직후 (초기 위치 설정)
     *        - 로봇을 물리적으로 다른 노드로 옮긴 뒤
     *
     *        참고: 주행 중(GOTO)에는 교차로마다 node_idx, dir가 자동 갱신됨.
     */
    void handleSetLoc(JsonDocument& doc);

    /**
     * @brief 작업 명령 처리 (Pick-and-Place 등).
     *        수신: {"cmd": "TASK", "action": "PICK_AND_PLACE", "count": 5}
     *
     * TODO (팀원 구현):
     *   1) action, count 값 추출
     *   2) so-arm(STS3215 서보) 제어 함수 호출
     *   3) 작업 완료 후 sendResponse() 호출
     */
    void handleTask(JsonDocument& doc);

    /**
     * @brief 수동 제어 명령 처리.
     *        수신: {"cmd": "MANUAL", "device": "FAN", "state": "ON"}
     *
     * TODO (팀원 구현):
     *   1) device, state 값 추출
     *   2) 해당 GPIO 핀 제어
     *   3) 제어 완료 후 sendResponse() 호출
     */
    void handleManual(JsonDocument& doc);

    // ─────────── 멤버 변수 ───────────
    WiFiClient  _tcpClient;     // TCP 클라이언트 소켓
    WiFiUDP     _udpClient;     // UDP 소켓

    const char* _serverIP;      // 서버 IP 주소
    uint16_t    _serverPort;    // 서버 TCP 포트
    uint16_t    _udpPort;       // UDP 브로드캐스트 포트

    char _recvBuffer[1024];     // TCP 수신 버퍼 (JSON용)
    uint8_t _rxSfamBuf[SFAM_PKT_MAX];  // SFAM 수신 버퍼
    uint8_t _rxSfamCount;       // 수신된 SFAM 바이트 수
    uint8_t _rxSfamPayLen;      // SFAM 페이로드 길이

    // ─────────── 통신 재연결 및 상태 관리를 위한 변수 ───────────
    unsigned long _msgCount;              // 송신 메시지 카운트
    unsigned long _lastReconnectAttempt;  // 마지막으로 서버 재연결을 시도한 시간 (밀리초)
    unsigned long _lastHeartbeatMs;       // 마지막 HEARTBEAT_REQ 전송 시각
    uint8_t _sfamSeq;                     // SFAM 패킷 시퀀스
    uint16_t _currentTaskId;              // 현재 수행 중 task_id (AGV_STATUS_RPT용)

    void sendSfamTelemetry(int battery);
    void sendSfamHeartbeat();
    void sendSfamRfidEvent(const char* uid);
    void sendSfamTaskAck(uint16_t taskId, uint8_t ackCode);
    void sendSfamStatusRpt(uint16_t taskId, uint8_t taskStatus, uint8_t nodeIdx, uint8_t errorId);

    void processSfamPacket(const uint8_t* pkt, uint8_t payLen);

    // ─────────── 모터, 라인트레이싱, 팔 제어 ───────────
    MotorController _motorController;   // 모터 컨트롤러
    LineFollower    _lineFollower;      // 라인트레이싱 모듈
    PathFinder      _pathFinder;        // 경로 탐색기 (최단거리 계산)
    ServoArmController _armController;  // [추가] 16번, 17번 서보 팔 제어기

    // ─────────── 센서 ───────────
    RFIDReader      _rfidReader;        // RFID 리더
};

#endif // ROBOT_NETWORK_MANAGER_H
