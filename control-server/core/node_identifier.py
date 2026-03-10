"""
노드 식별 모듈
- ESP32 컨트롤러 ID를 기반으로 노드 ID와 제어기 ID를 할당합니다.
- 원본: integrated_server.py → identify_node()
"""
import re


def identify_node(raw_id):
    """
    ESP32에서 전송된 원시 ID를 파싱하여 (clean_id, node_id, controller_id) 튜플을 반환합니다.
    
    ID 매핑 규칙:
      - 10     → I10, CTRL-INBOUND-00     (입고장)
      - 99     → O99, CTRL-OUTBOUND-99    (출고장)
      - 11~13  → S11~S13, CTRL-NURSERY-xx (센서 구역)
      - 14~16  → R14~R16, CTRL-NURSERY-xx (재배 구역)
      - 기타   → nXX, CTRL-UNKNOWN-XX
    """
    try:
        clean_id = int(re.sub(r'[^0-9]', '', str(raw_id))) if re.sub(r'[^0-9]', '', str(raw_id)) else 0
    except:
        clean_id = 0

    if clean_id == 10:
        node_id, dyn_ctrl_id = f"I{clean_id}", "CTRL-INBOUND-00"
    elif clean_id == 99:
        node_id, dyn_ctrl_id = f"O{clean_id}", "CTRL-OUTBOUND-99"
    elif 11 <= clean_id <= 13:
        node_id, dyn_ctrl_id = f"S{clean_id}", f"CTRL-NURSERY-{clean_id-10:02d}"
    elif 14 <= clean_id <= 16:
        node_id, dyn_ctrl_id = f"R{clean_id}", f"CTRL-NURSERY-{clean_id-10:02d}"
    else:
        node_id, dyn_ctrl_id = f"n{clean_id}", f"CTRL-UNKNOWN-{clean_id}"

    return clean_id, node_id, dyn_ctrl_id
