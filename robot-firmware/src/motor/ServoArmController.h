#ifndef SERVO_ARM_CONTROLLER_H
#define SERVO_ARM_CONTROLLER_H

#include <Arduino.h>
#include <ESP32Servo.h>

/**
 * @class ServoArmController
 * @brief 17번(360도 연속 회전 팔) 및 16번(180도 그리퍼) 서보모터 제어 클래스 
 */
class ServoArmController {
private:
    Servo _armServo;
    Servo _gripperServo;
    bool _armEnabled = true;  // false: 실제 서보 구동 안 함

    // 핀 번호 설정
    const int PIN_ARM = 17;
    const int PIN_GRIPPER = 16;

    // 모터 동작 신호 설정 (연속회전 서보 기준)
    const int ARM_STOP = 1500;       
    const int ARM_CLOCKWISE = 1000;      // 시계방향
    const int ARM_COUNTERCLOCK = 2000;   // 반시계방향

    // 회전 유지 시간 (ms)
    const int timeCW = 1800;   // 시계방향 동작 시간
    const int timeCCW = 1800;  // 반시계방향 동작 시간

    // 180도 그리퍼 제어값 (각도)
    const int GRIPPER_OPEN = 180;   // 놓기 (180도)
    const int GRIPPER_CLOSE = 55;   // 잡기 (55도)

public:
    ServoArmController() {}

    void setArmEnabled(bool enabled) { _armEnabled = enabled; }
    bool isArmEnabled() const { return _armEnabled; }

    void init() {
        if (!_armEnabled) {
            Serial.println("[ServoArmController] 초기화 생략 (arm 비활성화)");
            return;
        }
        // ESP32PWM 타이머 할당 및 주파수 설정
        ESP32PWM::allocateTimer(0);
        ESP32PWM::allocateTimer(1);
        
        // 1. 암 모터를 정지 상태로 초기화 (움직이지 않게)
        _armServo.setPeriodHertz(50);
        _armServo.writeMicroseconds(ARM_STOP); 
        _armServo.attach(PIN_ARM, 500, 2400);

        // 2. 그리퍼 모터를 기본 벌린 각도(180도)로 초기화
        _gripperServo.setPeriodHertz(50);
        _gripperServo.write(GRIPPER_OPEN); // 초기 상태는 '놓기(180도)'
        _gripperServo.attach(PIN_GRIPPER, 500, 2400);
        
        Serial.println("[ServoArmController] 초기화 완료 (Arm: 17, Gripper: 16)");
    }

    // ===================================================
    // 사용자 정의 함수 부분 (동작 단위로 분리)
    // ===================================================

    /**
     * @brief 암 시계방향 회전
     */
    void rotateArmCW() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 암 시계방향 회전 중...");
        _armServo.writeMicroseconds(ARM_CLOCKWISE); 
        delay(timeCW); 
        _armServo.writeMicroseconds(ARM_STOP); 
        Serial.println(" -> 암 회전 완료!");
    }

    /**
     * @brief 암 반시계방향 회전
     */
    void rotateArmCCW() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 암 반시계방향 회전 중...");
        _armServo.writeMicroseconds(ARM_COUNTERCLOCK); 
        delay(timeCCW); 
        _armServo.writeMicroseconds(ARM_STOP); 
        Serial.println(" -> 암 회전 완료!");
    }

    /**
     * @brief 그리퍼 잡기 (55도)
     */
    void grabGripper() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 그리퍼 잡기 (55도)");
        _gripperServo.write(GRIPPER_CLOSE); 
        delay(500);
    }

    /**
     * @brief 그리퍼 놓기 (180도)
     */
    void releaseGripper() {
        if (!_armEnabled) return;
        Serial.println(" -> [동작] 그리퍼 놓기 (180도)");
        _gripperServo.write(GRIPPER_OPEN); 
        delay(500);
    }

    // ===================================================
    // 기존 매크로 동작 함수 (상황에 맞게 수정됨)
    // ===================================================

    /**
     * @brief [1단계] 화분을 집기 위해 그리퍼를 열고 팔을 내린 후 대기합니다.
     */
    void pickReady() {
        if (!_armEnabled) {
            Serial.println("[ServoArm] 픽업 준비 (비활성화)");
            return;
        }
        Serial.println("[ServoArm] 픽업 준비: 그리퍼 열기 -> 팔 내리기");
        
        // 1. 그리퍼 열어두기
        releaseGripper();

        // 2. 팔 내리기 (시계방향 회전이라 가정)
        rotateArmCW();
        
        // 3. 흔들림 안정화 대기
        delay(500); 
    }

    /**
     * @brief [2단계] 화분을 꽉 잡은 후 팔을 다시 들어 등 위에 적재합니다.
     * @note pickReady() 이후 로봇이 후진하여 화분과 밀착한 뒤 호출되어야 합니다.
     */
    void pickExecute() {
        if (!_armEnabled) {
            Serial.println("[ServoArm] 픽업 실행 (비활성화)");
            return;
        }
        Serial.println("[ServoArm] 픽업 실행: 그리퍼 닫기 -> 팔 올리기");

        // 1. 그리퍼 닫아서 화분 꽉 잡기
        grabGripper();
        delay(500);

        // 2. 등 위로 팔 들어올리기 (반시계방향 회전이라 가정)
        rotateArmCCW();
        
        Serial.println("[ServoArm] 픽업 적재 완료!");
    }

    /**
     * @brief 화분을 내려놓는 동작 (필요 시 호출)
     */
    void dropPot() {
        if (!_armEnabled) {
            Serial.println("[ServoArm] 내려놓기 (비활성화)");
            return;
        }
        Serial.println("[ServoArm] 내려놓기 시작");
        
        // 1. 적재된 화분을 목적지 바닥으로 내리기 
        rotateArmCW();
        delay(500);

        // 2. 그리퍼 열어 화분 놓기
        releaseGripper();
        delay(500);

        // 3. 빈 팔을 다시 등 위로 복귀시키기 
        rotateArmCCW();
        
        Serial.println("[ServoArm] 내려놓기 완료!");
    }
};

#endif // SERVO_ARM_CONTROLLER_H
