/**
 * wifi_config.h
 * =============
 * Wi-Fi 및 서버 설정 - 한 곳에서 관리
 *
 * robot-firmware, esp32-cam 공통 사용.
 * 이 파일만 수정하면 모든 디바이스에 적용됩니다.
 *
 * (esp32-cam/, robot-firmware/ 내 wifi_config.h 는 이 파일로의 심볼릭 링크)
 */

#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

// ═══════════════════════════════════════════════════════════════
//  Wi-Fi 설정
// ═══════════════════════════════════════════════════════════════

#define WIFI_SSID     "addinedu_201class_3-2.4G"
#define WIFI_PASSWORD "201class3!"

// ═══════════════════════════════════════════════════════════════
//  서버(노트북) IP
// ═══════════════════════════════════════════════════════════════
// 노트북 IP 확인: Linux `hostname -I` / Windows `ipconfig`

#define SERVER_IP "192.168.0.12"

// ═══════════════════════════════════════════════════════════════
//  서버 포트
// ═══════════════════════════════════════════════════════════════

#define SERVER_TCP_PORT 8080   // 로봇 TCP (robot-firmware)
#define SERVER_UDP_PORT 7070   // ESP32-CAM 이미지 수신

#endif // WIFI_CONFIG_H
