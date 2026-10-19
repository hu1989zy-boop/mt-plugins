#!/usr/bin/env python3
"""Local management host. Versioned module API, atomic SDK configuration files."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import mimetypes
import os
from pathlib import Path
import secrets
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit

from schema import (MAX_BYTES, PLATFORMS, ValidationError, decode_json, default_config,
                    fields, preview, require, validate_config)

WEB_ROOT = Path(__file__).resolve().parent / "web"
API_PREFIX = "/api/plugins/dynamic-leverage/"


class ConflictError(Exception):
    pass


def encode(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")


def revision(raw):
    return hashlib.sha256(raw).hexdigest() if raw is not None else "missing"


def replace_file(source, destination):
    # A Windows Server DLL may briefly hold a read handle without delete
    # sharing. Keep the old JSON intact and retry the atomic rename rather
    # than failing an otherwise valid save or overwriting the file in place.
    for attempt in range(6):
        try:
            os.replace(source, destination)
            return
        except PermissionError as exc:
            if getattr(exc, "winerror", None) not in (5, 32, 33) or attempt == 5:
                raise
            time.sleep(0.01 * (2 ** attempt))


class Store:
    def __init__(self, directory):
        self.directory = Path(directory).resolve()
        self.directory.mkdir(parents=True, exist_ok=True)
        self.lock = threading.RLock()

    def path(self, platform):
        require(platform in PLATFORMS, "不支持的平台")
        path = self.directory / f"{platform}.rules.json"
        require(not path.is_symlink(), "配置文件不能是符号链接")
        return path

    def read_raw(self, platform):
        path = self.path(platform)
        try:
            with path.open("rb") as stream:
                raw = stream.read(MAX_BYTES + 1)
        except FileNotFoundError:
            return None
        require(len(raw) <= MAX_BYTES, "配置文件超过 1 MiB")
        return raw

    def load(self, platform):
        with self.lock:
            raw = self.read_raw(platform)
            config = default_config(platform) if raw is None else validate_config(decode_json(raw), platform)
            return {"config": config, "revision": revision(raw), "path": str(self.path(platform)), "exists": raw is not None}

    def save(self, platform, document, expected_revision):
        config = validate_config(document, platform)
        raw = encode(config)
        require(len(raw) <= MAX_BYTES, "配置文件超过 1 MiB")
        with self.lock:
            current = self.read_raw(platform)
            if expected_revision != revision(current):
                raise ConflictError("配置已被其他窗口修改，请重新加载后再编辑")
            path = self.path(platform)
            temp_path = None
            try:
                with tempfile.NamedTemporaryFile(dir=self.directory, prefix=f".{platform}-", suffix=".tmp", delete=False) as stream:
                    temp_path = Path(stream.name)
                    stream.write(raw)
                    stream.flush()
                    os.fsync(stream.fileno())
                replace_file(temp_path, path)
                temp_path = None
            finally:
                if temp_path is not None:
                    temp_path.unlink(missing_ok=True)
            response = {"config": config, "revision": revision(raw), "path": str(path), "exists": True}
            try:
                self.append_audit({"time": datetime.now(timezone.utc).isoformat(timespec="seconds"),
                                   "action": "保存规则配置", "platform": platform,
                                   "revision": response["revision"], "rule_count": len(config["rules"])})
            except (OSError, ValidationError):
                response["warning"] = "配置已保存，但管理操作记录写入失败"
            return response

    def append_audit(self, entry):
        path = self.directory / "management.audit.jsonl"
        require(not path.is_symlink(), "操作记录不能是符号链接")
        if path.exists() and path.stat().st_size > MAX_BYTES:
            os.replace(path, self.directory / "management.audit.previous.jsonl")
        with path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(entry, ensure_ascii=False) + "\n")

    def audit(self, platform):
        with self.lock:
            path = self.directory / "management.audit.jsonl"
            require(not path.is_symlink(), "操作记录不能是符号链接")
            if not path.exists():
                return []
            # Bounded log tail; malformed interrupted entries do not hide the rest.
            with path.open("rb") as stream:
                size = stream.seek(0, os.SEEK_END)
                stream.seek(max(0, size - MAX_BYTES))
                raw = stream.read(MAX_BYTES)
            entries = []
            for line in raw.splitlines():
                try:
                    item = json.loads(line)
                    if isinstance(item, dict) and item.get("platform") == platform:
                        entries.append(item)
                except (ValueError, UnicodeError):
                    pass
            return list(reversed(entries[-100:]))


class DirectoryLease:
    """Prevent two local management processes from racing the same revision files."""
    def __init__(self, directory):
        path = Path(directory).resolve()
        path.mkdir(parents=True, exist_ok=True)
        self.stream = (path / ".management.lock").open("a+b")
        try:
            if os.name == "nt":
                import msvcrt
                if self.stream.seek(0, os.SEEK_END) == 0:
                    self.stream.write(b"\0")
                    self.stream.flush()
                self.stream.seek(0)
                msvcrt.locking(self.stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            self.stream.close()
            raise RuntimeError("此配置目录已有管理服务运行") from exc

    def close(self):
        self.stream.close()


class ManagementServer(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, address, directory):
        require(address[0] == "127.0.0.1", "管理服务仅允许绑定本机 127.0.0.1")
        self.store = Store(directory)
        self.csrf_token = secrets.token_urlsafe(32)
        super().__init__(address, Handler)


class Handler(BaseHTTPRequestHandler):
    server_version = "DynamicLeverageManagement/0.2"

    def do_GET(self):
        self.dispatch("GET")

    def do_PUT(self):
        self.dispatch("PUT")

    def do_POST(self):
        self.dispatch("POST")

    def json_response(self, code, value):
        self.send_content(code, encode(value), "application/json; charset=utf-8")

    def send_content(self, code, content, content_type):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(content)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "same-origin")
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        self.end_headers()
        self.wfile.write(content)

    def allowed_host(self):
        port = self.server.server_address[1]
        return self.headers.get("Host") in (f"127.0.0.1:{port}", f"localhost:{port}")

    def authorize_write(self):
        token = self.headers.get("X-CSRF-Token", "")
        if not secrets.compare_digest(token, self.server.csrf_token):
            return False
        origin = self.headers.get("Origin")
        if origin is not None and origin != f"http://{self.headers.get('Host')}":
            return False
        return self.headers.get("Sec-Fetch-Site") not in ("cross-site",)

    def read_document(self):
        require(self.headers.get("Content-Type", "").split(";")[0].strip().lower() == "application/json", "请求格式必须为 application/json")
        require(not self.headers.get("Transfer-Encoding"), "不支持分块请求")
        value = self.headers.get("Content-Length", "")
        require(value.isascii() and value.isdecimal(), "缺少有效的请求长度")
        size = int(value)
        require(0 < size <= MAX_BYTES, "请求体必须介于 1 字节与 1 MiB 之间")
        self.connection.settimeout(10)
        raw = self.rfile.read(size)
        require(len(raw) == size, "请求体不完整")
        return decode_json(raw)

    def dispatch(self, method):
        try:
            if not self.allowed_host():
                self.json_response(403, {"error": "不允许此 Host"})
                return
            if method != "GET" and not self.authorize_write():
                self.json_response(403, {"error": "会话已失效或请求来源不被允许，请刷新页面"})
                return
            path = urlsplit(self.path).path
            if method == "GET" and path == "/api/session":
                self.json_response(200, {"csrf_token": self.server.csrf_token, "mode": "local-config",
                    "plugins": [{"id": "dynamic-leverage", "name": "动态杠杆", "available": True}],
                    "version": "0.2.0", "data_dir": str(self.server.store.directory)})
                return
            if path.startswith(API_PREFIX):
                parts = path[len(API_PREFIX):].split("/")
                if len(parts) != 2 or parts[0] not in PLATFORMS:
                    self.json_response(404, {"error": "接口不存在"})
                    return
                platform, action = parts
                if method == "GET" and action == "config":
                    self.json_response(200, self.server.store.load(platform))
                    return
                if method == "PUT" and action == "config":
                    expected = self.headers.get("If-Match", "").strip('"')
                    require(bool(expected), "保存时必须提供配置版本 If-Match")
                    self.json_response(200, self.server.store.save(platform, self.read_document(), expected))
                    return
                if method == "POST" and action == "preview":
                    document = self.read_document()
                    fields(document, ("config", "login", "group", "lots"), "匹配预览")
                    self.json_response(200, preview(document["config"], platform, document["login"], document["group"], document["lots"]))
                    return
                if method == "GET" and action == "audit":
                    self.json_response(200, {"entries": self.server.store.audit(platform)})
                    return
                self.json_response(404, {"error": "接口不存在"})
                return
            if method == "GET" and not path.startswith("/api/"):
                relative = "index.html" if path == "/" else path.lstrip("/")
                file = (WEB_ROOT / relative).resolve()
                if WEB_ROOT not in file.parents or file.suffix not in (".html", ".css", ".js", ".mjs", ".svg", ".ico") or not file.is_file():
                    self.json_response(404, {"error": "文件不存在"})
                    return
                content_type = "text/javascript" if file.suffix == ".mjs" else (mimetypes.guess_type(str(file))[0] or "application/octet-stream")
                self.send_content(200, file.read_bytes(), content_type + "; charset=utf-8")
                return
            self.json_response(404, {"error": "接口不存在"})
        except ConflictError as exc:
            self.json_response(409, {"error": str(exc)})
        except ValidationError as exc:
            self.json_response(400, {"error": str(exc)})
        except (OSError, TimeoutError) as exc:
            self.json_response(500, {"error": f"本地配置读写失败：{exc}"})
        except Exception:
            self.json_response(500, {"error": "管理服务发生错误，请查看服务端日志"})
            raise

    def log_message(self, fmt, *args):
        sys.stderr.write(f"[{self.log_date_time_string()}] {fmt % args}\n")


def main():
    parser = argparse.ArgumentParser(description="动态杠杆本地管理服务（与 Server DLL 配置目录共享）")
    parser.add_argument("--data-dir", type=Path, default=Path(__file__).resolve().parents[1] / "runtime")
    parser.add_argument("--port", type=int, default=8765)
    arguments = parser.parse_args()
    lease = DirectoryLease(arguments.data_dir)
    try:
        server = ManagementServer(("127.0.0.1", arguments.port), arguments.data_dir)
        print(f"动态杠杆管理：http://127.0.0.1:{server.server_address[1]}", flush=True)
        print(f"配置目录：{server.store.directory}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
    finally:
        lease.close()


if __name__ == "__main__":
    main()
