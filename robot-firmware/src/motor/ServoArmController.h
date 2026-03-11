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

    const int PIN_ARM = 17;
    const int PIN_GRIPPER = 16;

    // 360도 팔 모터 제어값 및 시간
    const int ARM_STOP = 90;
    const int ARM_FORWARD = 0;     // 팔 내리기 방향
    const int ARM_BACKWARD = 180;  // 팔 올리기 방향
    const int ARM_MOVE_TIME = 780; // 180도 회전 시 필요한 시간 (ms)

    // 180도 그리퍼 제어값 (각도)
    const int GRIPPER_OPEN = 60;   // 열림
    const int GRIPPER_CLOSE = 0;   // 닫힘 (화분 파지)

public:
    ServoArmController() {}

    /**
     * @brief 서보 핀 및 초기 상태 설정
     */
    void init() {
        // ESP32PWM 타이머 할당 및 주파수 설정
        ESP32PWM::allocateTimer(0);
        _armServo.setPeriodHertz(50);
        _gripperServo.setPeriodHertz(50);

        _armServo.attach(PIN_ARM, 500, 2400);
        _gripperServo.attach(PIN_GRIPPER, 500, 2400);

        // 초기 자세: 팔 정지, 그리퍼 열림
        _armServo.write(ARM_STOP);
        _gripperServo.write(GRIPPER_OPEN);
        
        Serial.println("[ServoArmController] 초기화 완료 (Arm: 17, Gripper: 16)");
    }

    /**
     * @brief [1단계] 화분을 집기 위해 그리퍼를 열고 팔을 180도 내린 후 대기합니다.
     */
    void pickReady() {
        Serial.println("[ServoArm] 픽업 준비: 그리퍼 열기 -> 팔 내리기");
        
        // 1. 그리퍼 열어두기
        _gripperServo.write(GRIPPER_OPEN);
        delay(500);

        // 2. 팔 180도 내리기
        _armServo.write(ARM_FORWARD);
        delay(ARM_MOVE_TIME);
        
        // 3. 팔 정지 및 흔들림 안정화 대기
        _armServo.write(ARM_STOP);
        delay(500); 
    }

    /**
     * @brief [2단계] 화분을 꽉 잡은 후 팔을 다시 180도 들어 등 위에 적재합니다.
     * @note pickReady() 이후 로봇이 후진하여 화분과 밀착한 뒤 호출되어야 합니다.
     */
    void pickExecute() {
        Serial.println("[ServoArm] 픽업 실행: 그리퍼 닫기 -> 팔 올리기");

        // 1. 그리퍼 닫아서 화분 꽉 잡기
        _gripperServo.write(GRIPPER_CLOSE);
        delay(1000); // 확실히 닫힐 때까지 1초 대기

        // 2. 등 위로 팔 들어올리기 (180도)
        _armServo.write(ARM_BACKWARD);
        delay(ARM_MOVE_TIME);
        
        // 3. 팔 정지
        _armServo.write(ARM_STOP);
        Serial.println("[ServoArm] 픽업 적재 완료!");
    }

    /**
     * @brief 화분을 내려놓는 동작 (필요 시 호출)
     */
    void dropPot() {
        Serial.println("[ServoArm] 내려놓기 시작");
        
        // 1. 적재된 화분을 목적지 바닥으로 내리기 (180도)
        _armServo.write(ARM_FORWARD);
        delay(ARM_MOVE_TIME);
        _armServo.write(ARM_STOP);
        delay(500);

        // 2. 그리퍼 열어 화분 놓기
        _gripperServo.write(GRIPPER_OPEN);
        delay(1000);

        // 3. 빈 팔을 다시 등 위로 복귀시키기 (180도)
        _armServo.write(ARM_BACKWARD);
        delay(ARM_MOVE_TIME);
        _armServo.write(ARM_STOP);
        
        Serial.println("[ServoArm] 내려놓기 완료!");
    }
};

#endif // SERVO_ARM_CONTROLLER_H
