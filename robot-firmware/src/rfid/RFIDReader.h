/**
 * RFIDReader.h
 * ============
 * ESP32 로봇용 MFRC522 RFID 리더 헤더 파일.
 *
 * 역할:
 *   - MFRC522 RFID 리더기 초기화 (커스텀 SPI 핀)
 *   - RFID 태그 UID 읽기
 *   - 식물 ID 식별
 *
 * 핀 설정: config/PinConfig.h 참조
 */

#ifndef RFID_READER_H
#define RFID_READER_H

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include "../config/PinConfig.h"

class RFIDReader {
public:
    RFIDReader();

    /**
     * @brief RFID 리더기 초기화.
     *        setup()에서 한 번 호출해야 함.
     */
    void init();

    /**
     * @brief RFID 태그 읽기 시도.
     * @return 태그를 성공적으로 읽었으면 true
     */
    bool readTag();

    /**
     * @brief 마지막으로 읽은 태그 UID 반환.
     * @return 16진수 문자열 (예: "A1B2C3D4"), 없으면 빈 문자열
     */
    String getLastTagUID() const { return _lastUID; }

    /**
     * @brief 새 태그가 감지되었는지 여부.
     * @return 새 태그가 감지되었으면 true
     */
    bool hasNewTag() const { return _hasNewTag; }

    /**
     * @brief 새 태그 플래그 초기화.
     */
    void clearNewTagFlag() { _hasNewTag = false; }

    /**
     * @brief 현재 태그가 감지 범위 내에 있는지 여부.
     * @return 태그가 있으면 true
     */
    bool isTagPresent() const { return _tagPresent; }

private:
    /**
     * @brief UID 바이트 배열을 16진수 문자열로 변환.
     */
    String uidToString(byte* uid, byte size);

    MFRC522* _mfrc522;      // MFRC522 인스턴스 포인터

    String _lastUID;        // 마지막으로 읽은 UID
    String _previousUID;    // 이전에 읽은 UID (중복 감지용)
    bool _hasNewTag;        // 새 태그 감지 플래그
    bool _tagPresent;       // 태그 존재 여부
};

#endif // RFID_READER_H
