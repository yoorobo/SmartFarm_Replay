/**
 * MotorController.cpp
 * ===================
 * ESP32 로봇 모터 및 센서 제어 구현 파일.
 *
 * [모터 드라이버]
 *   - 기존: L298N (ENA/ENB + IN1~IN4)
 *   - 현재: Dual H-Bridge L9110S
 *
 * L9110S 특성:
 *   - 모터 1: IA, IB
 *   - 모터 2: IA, IB
 *   - 별도의 EN 핀이 없으며, 두 입력 핀 중 하나에 PWM을 걸어 속도 제어.
 *
 * 이 구현에서는:
 *   - 좌측 모터: IN1(IA), IN2(IB)
 *   - 우측 모터: IN3(IA), IN4(IB)
 *   - ENA/ENB 핀은 더 이상 사용하지 않지만, OUTPUT LOW로 유지한다.
 */

#include "MotorController.h"

// ============================================================
//  생성자
// ============================================================

MotorController::MotorController()
    : _speedForward(150)
    , _speedSoft(150)
    , _speedHard(165)
{
}

// ============================================================
//  초기화
// ============================================================

void MotorController::init() {
    // 모터 드라이버 핀 설정
    pinMode(PIN_ENA, OUTPUT);
    pinMode(PIN_IN1, OUTPUT);
    pinMode(PIN_IN2, OUTPUT);
    pinMode(PIN_IN3, OUTPUT);
    pinMode(PIN_IN4, OUTPUT);
    pinMode(PIN_ENB, OUTPUT);

    // IR 센서 핀 설정
    pinMode(PIN_S1, INPUT);
    pinMode(PIN_S2, INPUT);
    pinMode(PIN_S3, INPUT);
    pinMode(PIN_S4, INPUT);
    pinMode(PIN_S5, INPUT);

    // 초기 상태: 정지
    stop();

    Serial.println("[MotorController] 초기화 완료");
}

// ============================================================
//  모터 제어 함수 (L9110S)
// ============================================================

void MotorController::goForward() {
    // 좌측 모터: 전진 (IN1=PWM, IN2=LOW)
    analogWrite(PIN_IN1, _speedForward);
    digitalWrite(PIN_IN2, LOW);

    // 우측 모터: 전진 (IN3=PWM, IN4=LOW)
    analogWrite(PIN_IN3, _speedForward);
    digitalWrite(PIN_IN4, LOW);

    // ENA/ENB는 사용하지 않으므로 LOW 유지
    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::turnLeftSoft() {
    // 좌측 모터 정지, 우측 모터만 전진
    analogWrite(PIN_IN1, 0);
    digitalWrite(PIN_IN2, LOW);

    analogWrite(PIN_IN3, _speedSoft);
    digitalWrite(PIN_IN4, LOW);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::turnRightSoft() {
    // 우측 모터 정지, 좌측 모터만 전진
    analogWrite(PIN_IN3, 0);
    digitalWrite(PIN_IN4, LOW);

    analogWrite(PIN_IN1, _speedSoft);
    digitalWrite(PIN_IN2, LOW);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::turnLeftHard() {
    // 좌측 후진, 우측 전진 → 제자리 좌회전
    analogWrite(PIN_IN1, 0);
    analogWrite(PIN_IN2, _speedHard);

    analogWrite(PIN_IN3, _speedHard);
    digitalWrite(PIN_IN4, LOW);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::turnRightHard() {
    // 좌측 전진, 우측 후진 → 제자리 우회전
    analogWrite(PIN_IN1, _speedHard);
    digitalWrite(PIN_IN2, LOW);

    analogWrite(PIN_IN3, 0);
    analogWrite(PIN_IN4, _speedHard);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::uTurnRight() {
    // 우회전 U턴: 좌측 전진, 우측 후진을 조금 더 강하게/오래
    analogWrite(PIN_IN1, _speedHard);
    digitalWrite(PIN_IN2, LOW);

    analogWrite(PIN_IN3, 0);
    analogWrite(PIN_IN4, _speedHard);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

void MotorController::stop() {
    // 모든 PWM 0, 모든 방향핀 LOW
    analogWrite(PIN_IN1, 0);
    analogWrite(PIN_IN2, 0);
    analogWrite(PIN_IN3, 0);
    analogWrite(PIN_IN4, 0);

    digitalWrite(PIN_IN1, LOW);
    digitalWrite(PIN_IN2, LOW);
    digitalWrite(PIN_IN3, LOW);
    digitalWrite(PIN_IN4, LOW);

    analogWrite(PIN_ENA, 0);
    analogWrite(PIN_ENB, 0);
}

// ============================================================
//  센서 읽기
// ============================================================

void MotorController::readSensors(int& s1, int& s2, int& s3, int& s4, int& s5) {
    s1 = digitalRead(PIN_S1);
    s2 = digitalRead(PIN_S2);
    s3 = digitalRead(PIN_S3);
    s4 = digitalRead(PIN_S4);
    s5 = digitalRead(PIN_S5);
}

// ============================================================
//  속도 설정
// ============================================================

void MotorController::setSpeed(int forward, int soft, int hard) {
    _speedForward = constrain(forward, 0, 255);
    _speedSoft = constrain(soft, 0, 255);
    _speedHard = constrain(hard, 0, 255);
    Serial.printf("[MotorController] 속도 설정: FW=%d, Soft=%d, Hard=%d\n",
                  _speedForward, _speedSoft, _speedHard);
}
