from flask import Blueprint, render_template, request, redirect, url_for, session, jsonify

auth_bp = Blueprint('auth', __name__)

# 하드코딩된 관리자 계정 정보
ADMIN_USERS = {
    "admin": "admin123",
    "user1": "pass1"
}

@auth_bp.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        
        if username in ADMIN_USERS and ADMIN_USERS[username] == password:
            session['logged_in'] = True
            session['username'] = username
            return redirect(url_for('index'))
        else:
            return render_template('login.html', error='아이디 또는 비밀번호가 올바르지 않습니다.')
            
    # GET 요청 시 이미 로그인되어 있으면 메인으로
    if session.get('logged_in'):
        return redirect(url_for('index'))
        
    return render_template('login.html')

@auth_bp.route('/logout')
def logout():
    session.clear()
    return redirect(url_for('auth.login'))

@auth_bp.route('/api/status/auth')
def auth_status():
    """로그인 상태 확인 API (JS에서 호출)"""
    return jsonify({
        'logged_in': session.get('logged_in', False),
        'username': session.get('username')
    })
