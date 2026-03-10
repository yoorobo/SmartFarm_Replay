"""
sfam_protocol.py
================
SFAM Serial Packet Protocol Specification v1.0 파이썬 구현체
CRC16-CCITT 계산, 패킷 파싱, 조립을 담당.
"""
import struct

# Constants
SOF = 0xAA
PKT_HDR_SIZE = 6
MAX_PAYLOAD = 64

# MSG_TYPE
MSG_HEARTBEAT_REQ   = 0x01
MSG_HEARTBEAT_ACK   = 0x02
MSG_AGV_TELEMETRY   = 0x10
MSG_AGV_TASK_CMD    = 0x11
MSG_AGV_TASK_ACK    = 0x12
MSG_AGV_STATUS_RPT  = 0x13
MSG_AGV_EMERGENCY   = 0x14
MSG_SENSOR_BATCH    = 0x20
MSG_ACTUATOR_CMD    = 0x21
MSG_ACTUATOR_ACK    = 0x22
MSG_CTRL_STATUS     = 0x23
MSG_RFID_EVENT      = 0x24
MSG_DOCK_REQ        = 0x30
MSG_DOCK_ACK        = 0x31
MSG_ERROR_REPORT    = 0xF0
MSG_ACK             = 0xFE
MSG_NAK             = 0xFF

# Device ID
ID_SERVER    = 0x00
ID_BROADCAST = 0xFF

def calc_crc16(data: bytes) -> int:
    """CRC16-CCITT (Poly: 0x1021, Init: 0xFFFF)"""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc

def build_packet(msg_type: int, src_id: int, dst_id: int, seq: int, payload: bytes) -> bytes:
    """바이너리 패킷 조립"""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"Payload length {len(payload)} exceeds MAX_PAYLOAD ({MAX_PAYLOAD})")
    
    header = struct.pack('!BBBBBB', SOF, msg_type, src_id, dst_id, seq, len(payload))
    data = header + payload
    crc = calc_crc16(data)
    crc_bytes = struct.pack('!H', crc)
    return data + crc_bytes

class SfamParser:
    """State Machine based packet parser for continuous stream"""
    def __init__(self):
        self.reset()
        
    def reset(self):
        self.state = 'WAIT_SOF'
        self.buf = bytearray()
        self.payLen = 0
        
    def process_byte(self, b: int):
        """1바이트씩 처리하여 완성된 패킷 반환. 완료되지 않았으면 None 반환."""
        if self.state == 'WAIT_SOF':
            if b == SOF:
                self.buf = bytearray([b])
                self.state = 'HEADER'
        elif self.state == 'HEADER':
            self.buf.append(b)
            if len(self.buf) == PKT_HDR_SIZE:
                self.payLen = self.buf[5]
                if self.payLen > MAX_PAYLOAD:
                    self.reset()
                elif self.payLen > 0:
                    self.state = 'PAYLOAD'
                else:
                    self.state = 'CRC_HI'
        elif self.state == 'PAYLOAD':
            self.buf.append(b)
            if len(self.buf) == PKT_HDR_SIZE + self.payLen:
                self.state = 'CRC_HI'
        elif self.state == 'CRC_HI':
            self.buf.append(b)
            self.state = 'CRC_LO'
        elif self.state == 'CRC_LO':
            self.buf.append(b)
            
            # 파싱 완료, 구조 추출 및 검증
            pkt_len = PKT_HDR_SIZE + self.payLen
            data_to_crc = self.buf[:pkt_len]
            calCrc = calc_crc16(data_to_crc)
            rxCrc = (self.buf[-2] << 8) | self.buf[-1]
            
            res = None
            if rxCrc == calCrc:
                res = {
                    'msg_type': self.buf[1],
                    'src_id': self.buf[2],
                    'dst_id': self.buf[3],
                    'seq': self.buf[4],
                    'len': self.buf[5],
                    'payload': bytes(self.buf[6:6+self.payLen])
                }
            
            self.reset()
            return res
        return None
