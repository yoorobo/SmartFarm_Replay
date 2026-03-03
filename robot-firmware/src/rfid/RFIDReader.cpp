/**
 * RFIDReader.cpp
 * ==============
 * ESP32 로봇용 MFRC522 RFID 리더 구현 파일.
 */

#include "RFIDReader.h"

// ============================================================
//  생성자
// ============================================================

RFIDReader::RFIDReader()
    : _mfrc522(nullptr)
    , _lastUID("")
    , _previousUID("")
    , _hasNewTag(false)
    , _tagPresent(false)
{
}

// ============================================================
//  초기화
// ============================================================

void RFIDReader::init() {
    // MFRC522가 사용하는 전역 SPI를 진우님 핀 맵으로 초기화
    SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);

    // MFRC522 인스턴스 생성
    _mfrc522 = new MFRC522(RFID_SS_PIN, RFID_RST_PIN);

    // MFRC522 초기화
    _mfrc522->PCD_Init();
    delay(100);

    // 안테나 게인 최대로 설정 (읽기 거리 향상)
    _mfrc522->PCD_SetAntennaGain(_mfrc522->RxGain_max);

    Serial.println("[RFIDReader] 초기화 완료");

    // 펌웨어 버전 출력 (디버깅용)
    byte version = _mfrc522->PCD_ReadRegister(_mfrc522->VersionReg);
    Serial.printf("[RFIDReader] MFRC522 펌웨어 버전: 0x%02X\n", version);

    if (version == 0x00 || version == 0xFF) {
        Serial.println("[RFIDReader] ⚠️ MFRC522 통신 실패 - 배선 확인 필요");
    }
}

// ============================================================
//  태그 읽기
// ============================================================

bool RFIDReader::readTag() {
    // 새 카드가 있는지 확인
    if (!_mfrc522->PICC_IsNewCardPresent()) {
        // 태그가 범위를 벗어났는지 확인
        if (_tagPresent) {
            _tagPresent = false;
            Serial.println("[RFIDReader] 태그 범위 이탈");
        }
        return false;
    }

    // 카드 UID 읽기
    if (!_mfrc522->PICC_ReadCardSerial()) {
        return false;
    }

    _tagPresent = true;

    // UID를 문자열로 변환
    String currentUID = uidToString(_mfrc522->uid.uidByte, _mfrc522->uid.size);

    // 새로운 태그인지 확인
    if (currentUID != _previousUID) {
        _lastUID = currentUID;
        _previousUID = currentUID;
        _hasNewTag = true;

        Serial.printf("[RFIDReader] 🏷️ 새 태그 감지: %s\n", _lastUID.c_str());
    }

    // 카드 통신 종료
    _mfrc522->PICC_HaltA();
    _mfrc522->PCD_StopCrypto1();

    return true;
}

// ============================================================
//  UID를 문자열로 변환
// ============================================================

String RFIDReader::uidToString(byte* uid, byte size) {
    String result = "";

    for (byte i = 0; i < size; i++) {
        if (uid[i] < 0x10) {
            result += "0";
        }
        result += String(uid[i], HEX);
    }

    result.toUpperCase();
    return result;
}
