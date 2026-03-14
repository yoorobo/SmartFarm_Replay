"""
db_manager.py
=============
AWS EC2 MySQL(MariaDB) 데이터베이스 연결을 관리하는 모듈.
pymysql 라이브러리를 사용하여 DB 연결/해제 및 쿼리 실행을 담당한다.
"""

import pymysql
from pymysql.cursors import DictCursor


class DatabaseManager:
    """
    MySQL/MariaDB 데이터베이스 연결을 관리하는 클래스.

    - 연결(connect) / 해제(disconnect) 기능 제공
    - 조회 결과를 딕셔너리(dict) 형태로 반환
    - 컨텍스트 매니저(with문) 지원 (고속 통신을 위해 세션 유지 방식 채택)
    """

    # ──────────────────────────────────────────────
    #  DB 접속 정보 (팀 전용 Private 레포 – 하드코딩)
    # ──────────────────────────────────────────────
    DB_CONFIG = {
        "host": "3.35.24.94",
        "user": "root",
        "password": "Sung!10292748",
        "database": "smart_farm_v2",
        "charset": "utf8mb4",           # 한글 등 멀티바이트 문자 안전 처리
        "cursorclass": DictCursor,       # 조회 결과를 딕셔너리로 반환
        "connect_timeout": 5,            # 연결 시도 타임아웃 설정 (초)
    }

    def __init__(self):
        """DatabaseManager 초기화. 연결 객체를 None으로 세팅."""
        self.connection = None

    # ──────────── 연결 ────────────
    def connect(self):
        """
        DB에 연결을 시도한다.
        이미 연결되어 있거나 유효한 경우 기존 연결을 유지한다.
        """
        try:
            # 기존 연결이 있고 살아있는지 확인 (재사용 로직)
            if self.connection and self.connection.open:
                self.connection.ping(reconnect=True)
                return

            self.connection = pymysql.connect(**self.DB_CONFIG)
            print("=" * 50)
            print("✅ [DB 연결 성공] AWS EC2 MySQL 서버에 연결되었습니다.")
            print(f"   Host : {self.DB_CONFIG['host']}")
            print(f"   DB   : {self.DB_CONFIG['database']}")
            print("=" * 50)
        except pymysql.MySQLError as e:
            print("=" * 50)
            print(f"❌ [DB 연결 실패] 오류 발생")
            print(f"   메시지 : {e}")
            print("=" * 50)
            self.connection = None

    # ──────────── 해제 ────────────
    def disconnect(self):
        """
        DB 연결을 안전하게 해제한다.
        """
        if self.connection and self.connection.open:
            self.connection.close()
            print("🔌 [DB 연결 해제] 데이터베이스 연결이 종료되었습니다.")
        else:
            print("ℹ️  이미 연결이 해제된 상태입니다.")

    # ──────────── 쿼리 실행 (SELECT) ────────────
    def execute_query(self, query: str, params: tuple | None = None) -> list[dict] | None:
        """
        SELECT 쿼리를 실행하고 결과를 딕셔너리 리스트로 반환한다.
        """
        if not self.connection or not self.connection.open:
            self.connect()  # 연결이 없으면 재시도
            if not self.connection: return None

        try:
            # 실행 전 연결 상태 체크 및 자동 재연결
            self.connection.ping(reconnect=True)
            with self.connection.cursor() as cursor:
                cursor.execute(query, params)
                result = cursor.fetchall()
                return result
        except pymysql.MySQLError as e:
            print(f"❌ [쿼리 실행 오류] {e}")
            return None

    # ──────────── 쿼리 실행 (INSERT / UPDATE / DELETE) ────────────
    def execute_update(self, query: str, params: tuple | None = None) -> int:
        """
        변경 쿼리를 실행하고 영향받은 행 수를 반환한다.
        """
        if not self.connection or not self.connection.open:
            self.connect()
            if not self.connection: return -1

        try:
            # 실행 전 연결 상태 체크 및 자동 재연결
            self.connection.ping(reconnect=True)
            with self.connection.cursor() as cursor:
                affected_rows = cursor.execute(query, params)
                self.connection.commit()  # 변경 사항 커밋
                return affected_rows
        except pymysql.MySQLError as e:
            print(f"❌ [쿼리 실행 오류] {e}")
            self.connection.rollback()    # 오류 발생 시 롤백
            return -1

    # ──────────── 컨텍스트 매니저 지원 ────────────
    def __enter__(self):
        """with 문 진입 시 연결을 확보한다."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """
        실시간 서버의 성능을 위해 with 문 종료 시 즉시 연결을 해제하지 않습니다.
        대신 명시적으로 disconnect()를 호출할 때 종료하거나 서버 종료 시 소멸되도록 합니다.
        """
        # 고속 패킷 처리를 위해 disconnect() 호출을 주석 처리함
        # self.disconnect() 
        pass