#ifndef SERVO_ARM_CONTROLLER_H
#define SERVO_ARM_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @class ServoArmController
 * @brief 17번(360도 연속 회전 팔) 및 16번(180도 그리퍼) 서보모터 제어 클래스
 *
 * ★ minsung_servo 예제와 동일한 패턴 적용:
 *   - attach는 init()에서 한 번만!
 *   - detach 하지 않음! (반복 attach/detach 시 글리치 펄스로 한 방향만 회전)
 *   - 정지 = write(MOTOR_STOP), 방향전환 = write(방향값)
 *
 * 360도 연속회전 서보 제어 방식 (write 기준):
 *   write(90)  = 정지
 *   write(0)   = 시계방향 회전
 *   write(180) = 반시계방향 회전
 */
class ServoArmController {
private:
    Servo _armServo;
    Servo _gripperServo;
    bool _armEnabled = false;

    // 핀 번호 설정
    const int PIN_ARM = 17;
    const int PIN_GRIPPER = 16;

    // 360도 연속회전 모터 제어값 (minsung_servo 예제와 동일)
    const int MOTOR_STOP = 90;
    const int DIR_CW = 0;             // 시계방향
    const int DIR_CCW = 180;          // 반시계방향

    // ★ 핵심 튜닝 포인트: 반 바퀴(180도) 회전 시간 (ms)
    const int MOVE_TIME_HALF = 780;

    // 정지 후 안정화 대기 시간 (ms)
    const int STOP_WAIT_TIME = 500;

    // 180도 그리퍼 제어값
    const int GRIPPER_OPEN = 180;      // 놓기 (수정됨)
    const int GRIPPER_CLOSE = 55;      // 잡기 (수정됨)

public:
    ServoArmController() {}

    void setArmEnabled(bool enabled) { _armEnabled = enabled; }
    bool isArmEnabled() const { return _armEnabled; }

    void init() {
        if (!_armEnabled) {
            Serial.println("[ServoArmController] 초기화 생략 (arm 비활성화)");
            return;
        }
        ESP32PWM::allocateTimer(0);
        ESP32PWM::allocateTimer(1);

        // ★ minsung_servo 패턴: setup()에서 한 번 attach, 이후 절대 detach 안 함!
        _armServo.setPeriodHertz(50);
        _armServo.attach(PIN_ARM, 500, 2400);
        _armServo.write(MOTOR_STOP);   // 처음에는 반드시 정지 상태로 대기
        delay(1000);                    // 충분한 안정화 대기

        _gripperServo.setPeriodHertz(50);

        Serial.println("[ServoArmController] 초기화 완료 (암 모터 attach 유지, 그리퍼는 명령 시에만 동작)");
    }

    // ===================================================
    // 암 모터 (360도 연속회전) 제어 함수
    // ★ minsung_servo loop() 패턴 그대로:
    //   write(방향) → delay(시간) → write(STOP) → delay(대기)
    // ===================================================

    void rotateArmCW() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 암 시계방향 180도 회전");
        _armServo.write(DIR_CW);           // 시계방향 회전 시작
        delay(MOVE_TIME_HALF);             // 780ms = 180도
        _armServo.write(MOTOR_STOP);       // ★ 도착 후 정지
        delay(STOP_WAIT_TIME);             // 안정화 대기
        Serial.println(" -> 암 시계방향 완료!");
    }

    void rotateArmCCW() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 암 반시계방향 180도 회전");
        _armServo.write(DIR_CCW);          // 반시계방향 회전 시작
        delay(MOVE_TIME_HALF);             // 780ms = 180도
        _armServo.write(MOTOR_STOP);       // ★ 도착 후 정지
        delay(STOP_WAIT_TIME);             // 안정화 대기
        Serial.println(" -> 암 반시계방향 완료!");
    }

    void rotateArm180CW() {
        rotateArmCW();
    }

    void rotateArm180CCW() {
        rotateArmCCW();
    }

    // ===================================================
    // 그리퍼 (180도 서보) 제어 함수
    // ===================================================

    void grabGripper() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 그리퍼 잡기");
        _gripperServo.attach(PIN_GRIPPER, 500, 2400);
        delay(50);
        _gripperServo.write(GRIPPER_CLOSE); 
        delay(500);
        _gripperServo.detach();
    }

    void releaseGripper() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 그리퍼 놓기");
        _gripperServo.attach(PIN_GRIPPER, 500, 2400);
        delay(50);
        _gripperServo.write(GRIPPER_OPEN); 
        delay(500);
        _gripperServo.detach();
    }

    // ===================================================
    // 매크로 동작 함수
    // ===================================================

    void pickReady() {
        if (!_armEnabled) { Serial.println("[ServoArm] 픽업 준비 (비활성화)"); return; }
        Serial.println("[ServoArm] 픽업 준비: 그리퍼 열기 -> 팔 내리기");
        releaseGripper();
        rotateArmCW();
    }

    void pickExecute() {
        if (!_armEnabled) { Serial.println("[ServoArm] 픽업 실행 (비활성화)"); return; }
        Serial.println("[ServoArm] 픽업 실행: 그리퍼 닫기 -> 팔 올리기");
        grabGripper();
        delay(500);
        rotateArmCCW();
        Serial.println("[ServoArm] 픽업 적재 완료!");
    }

    void dropPot() {
        if (!_armEnabled) { Serial.println("[ServoArm] 내려놓기 (비활성화)"); return; }
        Serial.println("[ServoArm] 내려놓기 시작");
        rotateArmCW();
        releaseGripper();
        delay(500);
        rotateArmCCW();
        Serial.println("[ServoArm] 내려놓기 완료!");
    }
};

#endif // SERVO_ARM_CONTROLLER_H

