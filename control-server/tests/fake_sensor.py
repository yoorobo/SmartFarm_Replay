"""
가상 육묘 센서 (HTTP 5001)
- 3초마다 온도, 습도, 조도 데이터를 JSON 형태로 중앙 서버에 전송합니다.
"""
import time
import requests
import random

SERVER_URL = "http://127.0.0.1:5001/data"
NODE_ID = "S11"
CONTROLLER_ID = "CTRL-NURSERY-01"

def send_fake_sensor_data():
    print(f"🌱 [가상 센서] 시작됨 (대상: {SERVER_URL})")
    while True:
        data = {
            "controller_id": CONTROLLER_ID,
            "node_id": NODE_ID,
            "temperature": round(random.uniform(20.0, 30.0), 1),
            "humidity": round(random.uniform(40.0, 70.0), 1),
            "light": random.randint(100, 1000)
        }
        try:
            res = requests.post(SERVER_URL, json=data, timeout=3)
            print(f"📤 [전송] {data} -> 응답: {res.status_code}")
        except Exception as e:
            print(f"❌ [에러] 서버 연결 실패: {e}")
        time.sleep(3)

if __name__ == "__main__":
    send_fake_sensor_data()
