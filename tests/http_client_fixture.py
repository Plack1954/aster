#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import time


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def send_bytes(self, status, body, extra_headers=()):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        for name, value in extra_headers:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/hello":
            self.send_bytes(200, b"hello\x00aster", (("X-Fixture", "yes"),))
        elif self.path == "/redirect":
            self.send_bytes(302, b"", (("Location", "/hello"),))
        elif self.path == "/large":
            self.send_bytes(200, b"0123456789abcdef")
        elif self.path == "/slow":
            time.sleep(0.15)
            self.send_bytes(200, b"slow")
        elif self.path == "/stream":
            body = bytes((index % 251 for index in range(200000)))
            self.send_bytes(200, body, (("X-Stream", "yes"),))
        elif self.path == "/endless":
            self.send_response(200)
            self.send_header("Content-Length", "1000000")
            self.send_header("Connection", "close")
            self.end_headers()
            try:
                for _ in range(100):
                    self.wfile.write(b"x" * 10000)
                    self.wfile.flush()
                    time.sleep(0.01)
            except (BrokenPipeError, ConnectionResetError):
                pass
        else:
            self.send_bytes(404, b"missing")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        if self.path != "/echo" or self.headers.get_content_type() != "text/plain":
            self.send_bytes(400, b"bad request")
            return
        self.send_bytes(201, body)


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
print(server.server_address[1], flush=True)
server.serve_forever()
