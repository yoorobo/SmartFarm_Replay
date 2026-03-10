"""
카메라 스트리밍 API
- ESP32-CAM MJPEG 스트림, 스냅샷, 상태 확인
- 원본: server/app.py → /api/camera/* 라우트
"""
from flask import Blueprint, Response, jsonify
from network.udp_camera_server import (
    get_camera_frame, wait_for_frame, clear_frame_event,
    latest_camera_frame, camera_fps, _camera_frame_lock
)

camera_bp = Blueprint('camera', __name__)


@camera_bp.route('/api/camera/stream')
def camera_stream():
    """MJPEG 스트림 (ESP32-CAM 실시간 영상)"""
    def generate():
        boundary = "frame"
        while True:
            wait_for_frame(timeout=2.0)
            frame = get_camera_frame()
            yield (
                f"--{boundary}\r\n"
                f"Content-Type: image/jpeg\r\n"
                f"Content-Length: {len(frame)}\r\n\r\n"
            ).encode()
            yield frame
            yield b"\r\n"
            clear_frame_event()

    return Response(
        generate(),
        mimetype="multipart/x-mixed-replace; boundary=frame",
        headers={"Cache-Control": "no-cache"},
    )


@camera_bp.route('/api/camera/snapshot')
def camera_snapshot():
    """최신 프레임 단건 반환"""
    with _camera_frame_lock:
        frame = latest_camera_frame
    if not frame:
        return Response("카메라 연결 대기 중", status=503, mimetype="text/plain")
    return Response(frame, mimetype="image/jpeg")


@camera_bp.route('/api/camera/status')
def camera_status():
    """카메라 연결/FPS 상태"""
    with _camera_frame_lock:
        has_frame = latest_camera_frame is not None
    return jsonify({
        "ok": True,
        "connected": has_frame,
        "fps": round(camera_fps, 1) if has_frame else 0,
    })
