#!/usr/bin/env python3
import http.server
import os
import pathlib
import ssl
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, _format, *_args):
        pass

    def _reply(self, status, body):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            pass

    def do_GET(self):
        if self.path == "/ok":
            body = b"fixture-ok"
            self.send_response(200)
            self.send_header("X-ErEsUlT", "42")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/slow":
            time.sleep(2.0)
            self._reply(200, b"late")
        elif self.path == "/large":
            self._reply(200, b"x" * 4096)
        elif self.path == "/large-headers":
            self.send_response(200)
            for index in range(65):
                self.send_header(f"X-Fixture-{index}", "bounded")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/ok")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self._reply(404, b"missing")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        marker = self.headers.get("X-Playden-Test", "").encode("ascii")
        self._reply(201, marker + b":" + body)


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: http_fixture_server.py TEST_BINARY FIXTURE_DIR")
    fixture_dir = pathlib.Path(sys.argv[2]).resolve()
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixture_dir / "server.pem", fixture_dir / "server-key.pem")
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        base_url = f"https://localhost:{server.server_port}"
        environment = os.environ.copy()
        environment["HTTPS_PROXY"] = "http://127.0.0.1:1"
        environment["ALL_PROXY"] = "http://127.0.0.1:1"
        environment["NO_PROXY"] = ""
        result = subprocess.run(
            [sys.argv[1], base_url, str(fixture_dir / "ca.pem"),
             str(fixture_dir / "wrong-ca.pem")],
            env=environment,
            check=False,
        )
        return result.returncode
    finally:
        server.shutdown()
        server.server_close()
        thread.join()


if __name__ == "__main__":
    raise SystemExit(main())
