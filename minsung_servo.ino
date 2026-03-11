#include <ESP32Servo.h>

Servo armServo;  // 17번 핀: 로봇 팔 (360도 연속 회전 모터)

const int armPin = 17;

// ==========================================
// 360도 모터 제어값 (각도가 아닌 방향과 속도를 의미함)
// ==========================================
const int MOTOR_STOP = 90;       // 완벽한 정지
const int DIR_FORWARD = 0;       // 한쪽 방향으로 회전 (예: 등 뒤로 넘기기)
const int DIR_BACKWARD = 180;    // 반대 방향으로 회전 (예: 정면으로 돌아오기)

// ★ 핵심 튜닝 포인트: 반 바퀴(180도)를 도는 데 걸리는 '시간' (밀리초)
// 모터에 아무것도 안 달려있을 때와 기구가 달려있을 때 속도가 다릅니다.
// 이 숫자를 1000, 1500, 2000 등으로 바꿔가며 딱 반 바퀴만 돌도록 맞추세요!
const int MOVE_TIME = 780; // 현재 1.5초로 설정됨

void setup() {
  Serial.begin(115200);
  
  ESP32PWM::allocateTimer(0);
  armServo.setPeriodHertz(50);
  armServo.attach(armPin, 500, 2400);

  Serial.println("=== 17번 핀(360도 모터) 왕복 테스트 시작 ===");
  armServo.write(MOTOR_STOP); // 처음에는 반드시 정지 상태로 대기
  delay(3000);
}

void loop() {
  // [동작 1] 한쪽으로 반 바퀴 이동
  Serial.println("정방향 회전 (이동 중...)");
  armServo.write(DIR_FORWARD);
  delay(MOVE_TIME);             // 설정한 시간만큼만 돌리기
  
  // [동작 2] 도착 후 정지
  Serial.println("도착 및 정지");
  armServo.write(MOTOR_STOP);   // ★ 부하 방지를 위해 반드시 정지 명령을 내려야 함
  delay(2000);                  // 2초간 머무름

  // [동작 3] 반대쪽으로 반 바퀴 복귀
  Serial.println("역방향 회전 (복귀 중...)");
  armServo.write(DIR_BACKWARD);
  delay(MOVE_TIME);             // 갔던 시간만큼 똑같이 돌아오기

  // [동작 4] 원위치 도착 후 정지
  Serial.println("원위치 도착 및 정지");
  armServo.write(MOTOR_STOP);
  
  Serial.println("--- 2초 후 왕복 다시 시작 ---");
  delay(2000);
}