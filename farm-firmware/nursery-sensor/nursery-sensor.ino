/*
 * smart_farm_nursery_esp32.ino
 * =======================================================
 * 육묘장 센서/액추에이터 컨트롤러 (단말 노드)
 * 
 * ESP32가 Wi-Fi/TCP를 통해 메인 서버와 연결되며,
 * 내부적으로는 UART2(115200) 인터페이스를 통해 아두이노와 
 * SFAM_Protocol(바이너리) 혹은 Serial 패킷을 주고받습니다.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "DHT.h"
#include "../../robot-firmware/src/comm/SFAM_Protocol.h"

// ─────────── Wi-Fi / TCP 설정 ───────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* SERVER_IP     = "192.168.0.100";
const uint16_t SERVER_PORT = 8000;

WiFiClient _tcpClient;
unsigned long _lastReconnectAttempt = 0;
unsigned long _lastHeartbeatMs = 0;

// ─────────── 통신 및 하드웨어 ───────────
HardwareSerial ArduinoSerial(2); // UART2 (RX=16, TX=17)
const uint8_t MY_NODE_ID = 0x11; // 0x10 ~ 0x1F

#define DHTPIN 4
#define DHTTYPE DHT11
#define PHOTO_PIN 34
DHT dht(DHTPIN, DHTTYPE);

const unsigned long SENSOR_SEND_INTERVAL = 2000;
unsigned long _lastSensorMs = 0;

ProtocolRxParser _tcpParser;

// ─────────── TCP 전송 유틸리티 ───────────
void sendTcpPacket(uint8_t msgType, uint8_t dstId, const uint8_t* payload, uint8_t len) {
    if (!_tcpClient.connected()) return;

    static uint8_t seq = 0;
    uint8_t buf[PKT_MAX_FULL];
    buf[0] = SOF;
    buf[1] = msgType;
    buf[2] = MY_NODE_ID;
    buf[3] = dstId;
    buf[4] = seq++;
    buf[5] = len;

    if (len > 0 && payload != nullptr) {
        memcpy(buf + 6, payload, len);
    }

    uint8_t pktLen = makeFullPacket(buf, PKT_HDR_SIZE + len, buf);
    _tcpClient.write(buf, pktLen);
    
    Serial.printf("[TCP-TX] Type:0x%02X, Seq:%d, Len:%d\n", msgType, seq-1, len);
}

// ─────────── 센서 묶음 전송 (서버로) ───────────
void sendSensorBatch(float temp, float hum, int light) {
    uint8_t pay[10];
    pay[0] = 3; // 센서 3개
    
    int32_t t_val = (int32_t)(temp * 10);
    pay[1] = 0x01;
    pay[2] = (t_val >> 16) & 0xFF;
    pay[3] = (t_val >> 8) & 0xFF;
    pay[4] = t_val & 0xFF;

    int32_t h_val = (int32_t)(hum * 10);
    pay[5] = 0x02;
    pay[6] = (h_val >> 16) & 0xFF;
    pay[7] = (h_val >> 8) & 0xFF;
    pay[8] = h_val & 0xFF;

    int32_t l_val = light;
    pay[9] = 0x03;
    pay[10] = (l_val >> 16) & 0xFF;
    pay[11] = (l_val >> 8) & 0xFF;
    pay[12] = l_val & 0xFF;

    sendTcpPacket(MSG_SENSOR_BATCH, ID_SERVER, pay, 13);
}

// ─────────── 명령 라우팅 ───────────
void handleTcpPacket(const uint8_t* buf, uint8_t hdrPayLen) {
    uint8_t msgType = buf[1];
    uint8_t srcId   = buf[2];
    uint8_t dstId   = buf[3];
    uint8_t seq     = buf[4];
    uint8_t payLen  = buf[5];
    const uint8_t* payload = buf + 6;

    if (dstId != MY_NODE_ID && dstId != ID_BROADCAST) return;

    Serial.printf("[TCP-RX] Type:0x%02X, Seq:%d\n", msgType, seq);

    switch(msgType) {
        case MSG_HEARTBEAT_REQ: {
            uint8_t pay[2] = {1, 0}; // ONLINE
            sendTcpPacket(MSG_HEARTBEAT_ACK, srcId, pay, 2);
            break;
        }
        case MSG_ACTUATOR_CMD: {
            if (payLen >= 4) {
                uint8_t actId = payload[0];
                uint8_t stateVal = payload[1];
                Serial.printf("   -> 아두이노 연결 Actuator %d 제어 명령 전달: %d\n", actId, stateVal);
                
                // TODO: UART로 아두이노에게 전달 로직
                // ArduinoSerial.print("ACT:");
                // ArduinoSerial.println(actId);
                
                uint8_t ackPay[3] = { actId, stateVal, 1 }; 
                sendTcpPacket(MSG_ACTUATOR_ACK, srcId, ackPay, 3);
            }
            break;
        }
    }
}

void processTcpByte(uint8_t b) {
    switch (_tcpParser.state) {
        case S_WAIT_SOF:
            if (b == SOF) {
                _tcpParser.buf[0] = b;
                _tcpParser.count = 1;
                _tcpParser.state = S_HEADER;
            }
            break;
        case S_HEADER:
            _tcpParser.buf[_tcpParser.count++] = b;
            if (_tcpParser.count == PKT_HDR_SIZE) {
                _tcpParser.payLen = _tcpParser.buf[5];
                _tcpParser.state = (_tcpParser.payLen > 0) ? S_PAYLOAD : S_CRC_HI;
            }
            break;
        case S_PAYLOAD:
            _tcpParser.buf[_tcpParser.count++] = b;
            if (_tcpParser.count == PKT_HDR_SIZE + _tcpParser.payLen) {
                _tcpParser.state = S_CRC_HI;
            }
            break;
        case S_CRC_HI:
            _tcpParser.crcHi = b;
            _tcpParser.state = S_CRC_LO;
            break;
        case S_CRC_LO:
            uint16_t rxCrc = ((uint16_t)_tcpParser.crcHi << 8) | b;
            uint16_t calCrc = calcCRC16(_tcpParser.buf, PKT_HDR_SIZE + _tcpParser.payLen);
            if (rxCrc == calCrc) {
                handleTcpPacket(_tcpParser.buf, PKT_HDR_SIZE + _tcpParser.payLen);
            } else {
                Serial.println("[TCP-RX] CRC Error!");
            }
            _tcpParser.state = S_WAIT_SOF;
            _tcpParser.count = 0;
            break;
    }
}

// ─────────── 연결 관리 ───────────
void maintainConnection() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        return;
    }
    if (!_tcpClient.connected()) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= 5000) {
            _lastReconnectAttempt = now;
            Serial.println("[TCP] 재연결 시도...");
            if (_tcpClient.connect(SERVER_IP, SERVER_PORT)) {
                _tcpClient.setNoDelay(true);
                Serial.println("[TCP] 연결 성공!");
            }
        }
    }
}

// ─────────── 메인 ───────────
void setup() {
    Serial.begin(115200);   // 디버그
    ArduinoSerial.begin(115200, SERIAL_8N1, 16, 17); // 아두이노 시리얼
    
    dht.begin();
    memset(&_tcpParser, 0, sizeof(_tcpParser));
    
    Serial.println("\n[Nursery Sensor ESP32] 🌿 시작 (Wi-Fi 연동)");
    WiFi.mode(WIFI_STA);
}

void loop() {
    maintainConnection();

    // TCP 수신 처리
    while (_tcpClient.available()) {
        uint8_t b = _tcpClient.read();
        processTcpByte(b);
    }
    
    // 아두이노 UART 수신 처리
    while (ArduinoSerial.available()) {
        String dataFromArduino = ArduinoSerial.readStringUntil('\n');
        Serial.print("[From Arduino] ");
        Serial.println(dataFromArduino);
        // 필요 시 Arduino 응답을 파싱하여 서버에 전달
    }

    // 서버에 주기적 Heartbeat 전송
    unsigned long now = millis();
    if (_tcpClient.connected() && (now - _lastHeartbeatMs >= 5000)) {
        _lastHeartbeatMs = now;
        sendTcpPacket(MSG_HEARTBEAT_REQ, ID_SERVER, nullptr, 0);
    }

    // 주기적 온습도/조도 전송 (DHT 및 조도 센서)
    if (now - _lastSensorMs >= SENSOR_SEND_INTERVAL) {
        _lastSensorMs = now;
        if (_tcpClient.connected()) {
            float t = dht.readTemperature();
            float h = dht.readHumidity();
            int l = analogRead(PHOTO_PIN);
            
            if (!isnan(t) && !isnan(h)) {
                sendSensorBatch(t, h, l);
            }
        }
    }
}