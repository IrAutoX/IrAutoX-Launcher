import json
import os
import secrets
import socket
import struct
import threading
import time
from pathlib import Path

from flask import Flask, jsonify, request, send_from_directory, session

ROOT = Path(__file__).resolve().parent
IRAUTOX_HOST = os.environ.get("IRAUTOX_HOST", "irautox.ir")
IRAUTOX_PORT = int(os.environ.get("IRAUTOX_PORT", "6768"))
SOCKET_TIMEOUT = float(os.environ.get("IRAUTOX_SOCKET_TIMEOUT", "6"))
SESSION_TTL = int(os.environ.get("IRAUTOX_SESSION_TTL", "21600"))

app = Flask(__name__, static_folder=None)
app.secret_key = os.environ.get("IRAUTOX_WEB_SECRET") or secrets.token_hex(32)
app.config.update(
    SESSION_COOKIE_HTTPONLY=True,
    SESSION_COOKIE_SAMESITE="Lax",
    SESSION_COOKIE_SECURE=os.environ.get("IRAUTOX_COOKIE_SECURE", "0") == "1",
    MAX_CONTENT_LENGTH=64 * 1024,
)

_session_lock = threading.Lock()
_sessions = {}


class IrAutoXProtocolError(RuntimeError):
    pass


def _encode(command, data=None):
    payload = json.dumps({"cmd": command, "data": data or {}}, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return struct.pack("!I", len(payload)) + payload


def _recv_exact(sock, size):
    chunks = []
    remaining = size
    while remaining:
        chunk = sock.recv(remaining)
        if not chunk:
            raise IrAutoXProtocolError("اتصال سرور بسته شد.")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _recv_message(sock):
    header = _recv_exact(sock, 4)
    length = struct.unpack("!I", header)[0]
    if length <= 0 or length > 16 * 1024 * 1024:
        raise IrAutoXProtocolError("اندازه پاسخ سرور نامعتبر است.")
    payload = _recv_exact(sock, length)
    try:
        data = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise IrAutoXProtocolError("پاسخ JSON سرور نامعتبر است.") from exc
    if not isinstance(data, dict):
        raise IrAutoXProtocolError("ساختار پاسخ سرور نامعتبر است.")
    return data


def _connect():
    sock = socket.create_connection((IRAUTOX_HOST, IRAUTOX_PORT), timeout=SOCKET_TIMEOUT)
    sock.settimeout(SOCKET_TIMEOUT)
    return sock


def _send_and_wait(sock, command, data=None, expected_key=None):
    sock.sendall(_encode(command, data))
    deadline = time.monotonic() + SOCKET_TIMEOUT
    while time.monotonic() < deadline:
        message = _recv_message(sock)
        status = str(message.get("status", ""))
        if status == "error":
            raise IrAutoXProtocolError(str(message.get("msg") or "خطای سرور IrAutoX"))
        if status == "ok" and (expected_key is None or expected_key in message):
            return message
    raise IrAutoXProtocolError("پاسخ سرور به موقع دریافت نشد.")


def _login_socket(sock, username, password):
    return _send_and_wait(sock, "login", {"username": username, "password": password}, "user")


def _cleanup_sessions():
    now = time.time()
    with _session_lock:
        expired = [key for key, value in _sessions.items() if now - value["updated"] > SESSION_TTL]
        for key in expired:
            _sessions.pop(key, None)


def _create_session(username, password, user):
    _cleanup_sessions()
    token = secrets.token_urlsafe(32)
    with _session_lock:
        _sessions[token] = {
            "username": username,
            "password": password,
            "user": user,
            "updated": time.time(),
        }
    session.clear()
    session["sid"] = token


def _current_session():
    token = session.get("sid")
    if not token:
        return None
    _cleanup_sessions()
    with _session_lock:
        value = _sessions.get(token)
        if value:
            value["updated"] = time.time()
            return dict(value)
    session.clear()
    return None


def _destroy_session():
    token = session.get("sid")
    if token:
        with _session_lock:
            _sessions.pop(token, None)
    session.clear()


@app.after_request
def security_headers(response):
    response.headers["X-Content-Type-Options"] = "nosniff"
    response.headers["Referrer-Policy"] = "same-origin"
    response.headers["X-Frame-Options"] = "DENY"
    response.headers["Cache-Control"] = "no-store" if request.path.startswith("/api/") else "public, max-age=300"
    return response


@app.get("/")
def index():
    return send_from_directory(ROOT, "index.html")


@app.get("/<path:filename>")
def static_file(filename):
    return send_from_directory(ROOT, filename)


@app.post("/api/login")
def api_login():
    body = request.get_json(silent=True) or {}
    username = str(body.get("username", "")).strip()
    password = str(body.get("password", ""))
    if len(username) < 3 or len(password) < 4:
        return jsonify({"message": "نام کاربری یا رمز عبور نامعتبر است."}), 400
    try:
        with _connect() as sock:
            result = _login_socket(sock, username, password)
    except (OSError, IrAutoXProtocolError) as exc:
        return jsonify({"message": str(exc)}), 502
    user = result.get("user") or {}
    _create_session(username, password, user)
    return jsonify({"ok": True, "user": user, "username": user.get("username", username)})


@app.post("/api/logout")
def api_logout():
    _destroy_session()
    return jsonify({"ok": True})


@app.get("/api/me")
def api_me():
    current = _current_session()
    if not current:
        return jsonify({"authenticated": False}), 401
    user = dict(current.get("user") or {})
    user.pop("password", None)
    return jsonify({"authenticated": True, "user": user})


@app.get("/api/games")
def api_games():
    current = _current_session()
    try:
        with _connect() as sock:
            if current:
                _login_socket(sock, current["username"], current["password"])
            result = _send_and_wait(sock, "get_games", {}, "games")
    except (OSError, IrAutoXProtocolError) as exc:
        if not current:
            return jsonify({"message": str(exc), "games": []}), 401
        return jsonify({"message": str(exc), "games": []}), 502
    return jsonify({"games": result.get("games") or []})


if __name__ == "__main__":
    host = os.environ.get("IRAUTOX_WEB_HOST", "127.0.0.1")
    port = int(os.environ.get("IRAUTOX_WEB_PORT", "8080"))
    app.run(host=host, port=port, debug=False, threaded=True)
