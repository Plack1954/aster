#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


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
