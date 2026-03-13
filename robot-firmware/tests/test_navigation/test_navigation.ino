/**
 * test_navigation.ino
 * ===================
 * AGV 라인트레이서 네비게이션 통합 테스트.
 *
 * 흐름:
 *   1. Wi-Fi 연결 → 서버 TCP 접속
 *   2. 서버에서 MOVE 명령 수신 (JSON)
 *   3. PathPlanner로 경로 계산
 *   4. LineFollower로 라인 추종 주행
 *   5. 도착 시 서버에 보고
 *
 * 테스트 방법:
 *   PC: python3 control-server/tests/test_nav_server.py
 *   ESP32: 이 스케치 업로드 (Wi-Fi/서버IP 수정 후)
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include "PathPlanner.h"

// src 폴더 모듈 (스케치 폴더에 복사됨)
// ※ .cpp는 아두이노가 자동 컴파일하므로 헤더만 include
#include "MotorController.h"
#include "LineFollower.h"

// ==========================================
// 1. Wi-Fi 및 네트워크 설정
// ==========================================
const char* WIFI_SSID     = "addinedu_201class_4-2.4G";
const char* WIFI_PASSWORD = "201class4!";

const char* SERVER_IP = "192.168.0.29";   // 파이썬 TCP 서버 IP
const int   SERVER_PORT = 8000;           // 파이썬 서버 포트

WiFiClient client;
const char* ROBOT_ID      = "R01";

// ============================================================
//  전역 객체
// ============================================================
WiFiClient    tcpClient;
MotorController motor;
LineFollower    lineFollower(motor);
PathPlanner     pathPlanner;

// 상태 플래그
bool navigating = false;
NodeId targetNode = NODE_COUNT;
char recvBuffer[512];

// ============================================================
//  서버에 JSON 응답 전송
// ============================================================
void sendToServer(const char* status, const char* msg) {
    if (!tcpClient.connected()) return;

    JsonDocument doc;
    doc["type"]     = "ROBOT_STATE";
    doc["robot_id"] = ROBOT_ID;
    doc["status"]   = status;
    doc["msg"]      = msg;
    doc["node"]     = PathPlanner::getNodeName(pathPlanner.getCurrentNode());
    doc["heading"]  = pathPlanner.getCurrentHeading();

    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    tcpClient.println(buf);
    Serial.printf("[Send] %s\n", buf);
}

// ============================================================
//  TCP 명령 수신 처리
// ============================================================
void handleTcpCommand() {
    if (!tcpClient.connected() || !tcpClient.available()) return;

    int len = tcpClient.readBytesUntil('\n', recvBuffer, sizeof(recvBuffer) - 1);
    recvBuffer[len] = '\0';
    Serial.printf("[Recv] %s\n", recvBuffer);

    JsonDocument doc;
    if (deserializeJson(doc, recvBuffer)) {
        Serial.println("[Error] JSON 파싱 실패");
        return;
    }

    const char* cmd = doc["cmd"];
    if (!cmd) return;

    if (strcmp(cmd, "MOVE") == 0) {
        const char* target = doc["target_node"];
        if (!target) {
            sendToServer("FAIL", "target_node 누락");
            return;
        }

        // 노드 이름 → NodeId 변환
        NodeId nodeId = PathPlanner::findNodeByName(target);
        if (nodeId == NODE_COUNT) {
            Serial.printf("[Error] 알 수 없는 노드: %s\n", target);
            sendToServer("FAIL", "알 수 없는 노드");
            return;
        }

        // 경로 계산
        String path = pathPlanner.planRoute(nodeId);
        if (path.length() == 0) {
            sendToServer("FAIL", "경로 탐색 실패");
            return;
        }

        // LineFollower에 경로 전달 및 주행 시작
        targetNode = nodeId;
        lineFollower.setPath(path);
        lineFollower.start();
        navigating = true;

        Serial.printf("[Nav] 주행 시작 → %s (path: %s)\n", target, path.c_str());
        sendToServer("SUCCESS", "주행 시작");

    } else if (strcmp(cmd, "STOP") == 0) {
        lineFollower.stop();
        motor.stop();
        navigating = false;
        sendToServer("SUCCESS", "정지 완료");

    // ── 모터 테스트 명령 ──
    } else if (strcmp(cmd, "SPIN_LEFT") == 0) {
        Serial.println("[Test] 제자리 좌회전 1초");
        motor.spinLeft();
        delay(1000);
        motor.stop();
        sendToServer("SUCCESS", "spinLeft 완료");

    } else if (strcmp(cmd, "SPIN_RIGHT") == 0) {
        Serial.println("[Test] 제자리 우회전 1초");
        motor.spinRight();
        delay(1000);
        motor.stop();
        sendToServer("SUCCESS", "spinRight 완료");

    } else if (strcmp(cmd, "FORWARD") == 0) {
        Serial.println("[Test] 직진 1초");
        motor.goForward();
        delay(1000);
        motor.stop();
        sendToServer("SUCCESS", "forward 완료");
    }
}

// ============================================================
//  도착 감지 및 보고
// ============================================================
void checkArrival() {
    if (!navigating) return;

    if (lineFollower.getState() == RobotState::ARRIVED) {
        navigating = false;

        // PathPlanner 상태 갱신
        pathPlanner.onArrived(targetNode, pathPlanner.getCurrentHeading());

        // 서버에 도착 보고
        char msg[100];
        snprintf(msg, sizeof(msg), "%s 도착 완료",
                 PathPlanner::getNodeName(targetNode));
        sendToServer("SUCCESS", msg);

        Serial.printf("[Nav] ✅ %s 도착!\n", PathPlanner::getNodeName(targetNode));
    }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n========================================");
    Serial.println("  🧪 AGV 네비게이션 테스트");
    Serial.println("========================================\n");

    // 1. 하드웨어 초기화
    motor.init();

    // 2. 맵 초기화
    pathPlanner.initMap();
    Serial.printf("[Init] 시작 위치: %s, 방향: %d (EAST)\n",
                  PathPlanner::getNodeName(pathPlanner.getCurrentNode()),
                  pathPlanner.getCurrentHeading());

    // 3. Wi-Fi 연결
    Serial.printf("[WiFi] 연결 중: %s\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] ✅ 연결 성공! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] ❌ 연결 실패!");
        return;
    }

    // 4. 서버 TCP 연결
    Serial.printf("[TCP] 서버 연결 중: %s:%d\n", SERVER_IP, SERVER_PORT);
    if (tcpClient.connect(SERVER_IP, SERVER_PORT)) {
        Serial.println("[TCP] ✅ 서버 연결 성공");
        sendToServer("SUCCESS", "AGV 연결 완료");
    } else {
        Serial.println("[TCP] ❌ 서버 연결 실패");
    }

    Serial.println("\n✅ 초기화 완료 - MOVE 명령 대기 중...\n");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // 1. 서버 명령 수신 처리
    handleTcpCommand();

    // 2. 라인트레이싱 업데이트 (주행 중일 때만)
    if (navigating) {
        lineFollower.update();
    }

    // 3. 도착 감지
    checkArrival();

    // 4. 서버 재연결
    if (WiFi.status() == WL_CONNECTED && !tcpClient.connected()) {
        Serial.println("[TCP] 재연결 시도...");
        tcpClient.connect(SERVER_IP, SERVER_PORT);
        delay(2000);
    }

    delay(10);
}
