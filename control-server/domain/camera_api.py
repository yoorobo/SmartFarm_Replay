"""
카메라 스트리밍 API
- ESP32-CAM MJPEG 스트림, 스냅샷, 상태 확인
- 카메라 ID별 스트림 지원 (로봇=1, 육묘장=10)
"""
from flask import Blueprint, Response, jsonify
from network.udp_camera_server import (
    get_camera_frame, wait_for_frame, clear_frame_event,
    latest_camera_frames, camera_fps, _camera_frame_locks
)

camera_bp = Blueprint('camera', __name__)

# 카메라 ID 상수
CAM_ROBOT   = 1
CAM_NURSERY = 10


def _make_stream(camera_id):
    """카메라 ID별 MJPEG 스트림 제너레이터"""
    boundary = "frame"
    while True:
        wait_for_frame(camera_id, timeout=2.0)
        frame = get_camera_frame(camera_id)
        yield (
            f"--{boundary}\r\n"
            f"Content-Type: image/jpeg\r\n"
            f"Content-Length: {len(frame)}\r\n\r\n"
        ).encode()
        yield frame
        yield b"\r\n"
        clear_frame_event(camera_id)


@camera_bp.route('/api/camera/stream/<int:camera_id>')
def camera_stream(camera_id):
    """MJPEG 스트림 (카메라 ID 지정) - /api/camera/stream/1, /api/camera/stream/10"""
    return Response(
        _make_stream(camera_id),
        mimetype="multipart/x-mixed-replace; boundary=frame",
        headers={"Cache-Control": "no-cache"},
    )


@camera_bp.route('/api/camera/stream')
def camera_stream_default():
    """하위 호환용 기본 스트림 (로봇 카메라 = ID 1)"""
    return camera_stream(CAM_ROBOT)


@camera_bp.route('/api/camera/snapshot/<int:camera_id>')
def camera_snapshot(camera_id):
    """특정 카메라의 최신 프레임 단건 반환"""
    with _camera_frame_locks.get(camera_id, __import__('threading').Lock()):
        frame = latest_camera_frames.get(camera_id)
    if not frame:
        return Response("카메라 연결 대기 중", status=503, mimetype="text/plain")
    return Response(frame, mimetype="image/jpeg")


@camera_bp.route('/api/camera/status')
def camera_status():
    """카메라 연결/FPS 상태 (로봇, 육묘장 모두)"""
    robot_frame   = latest_camera_frames.get(CAM_ROBOT)
    nursery_frame = latest_camera_frames.get(CAM_NURSERY)
    return jsonify({
        "ok": True,
        "robot": {
            "connected": robot_frame is not None,
            "fps": round(camera_fps.get(CAM_ROBOT, 0), 1),
        },
        "nursery": {
            "connected": nursery_frame is not None,
            "fps": round(camera_fps.get(CAM_NURSERY, 0), 1),
        },
    })
