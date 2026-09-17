"""Local-only HTTP/TLS fixture wrapper. Starts servers only when invoked as a test."""
import argparse
import gzip
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import time


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, address):
        # Deliberate cancellations/short bodies close connections mid-response.
        pass


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def handle_request(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length > 2 * 1024 * 1024:
            self.send_error(413)
            return
        body = self.rfile.read(length) if length else b""
        status, headers, output = 200, [], b"hello \xc3\xa9"
        path = self.path.split("?", 1)[0]
        if path == "/binary":
            output = b"A\0\xffZ"
        elif path == "/echo":
            output = body
            headers += [("X-Method", self.command), ("X-Request-Type", self.headers.get("Content-Type", ""))]
        elif path == "/method":
            output = self.command.encode() + b":" + body
        elif path == "/headers":
            output = json.dumps({k.lower(): v for k, v in self.headers.items()}, sort_keys=True).encode()
        elif path == "/status":
            status, output = 404, b"missing"
        elif path == "/large":
            output = b"x" * (2 * 1024 * 1024)
        elif path == "/gzip":
            output = gzip.compress(b"x" * (2 * 1024 * 1024))
            headers += [("Content-Encoding", "gzip")]
        elif path == "/too-many-headers":
            headers += [(f"X-Field-{i}", "value") for i in range(129)]
        elif path == "/long-header":
            headers += [("X-Long", "x" * 4096)]
        elif path == "/delay":
            time.sleep(2)
        elif path == "/short":
            self.send_response(200)
            self.send_header("Content-Length", "100")
            self.end_headers()
            self.wfile.write(b"short")
            self.wfile.flush()
            self.close_connection = True
            return
        elif path.startswith("/redirect") or path == "/downgrade":
            status = 302
            target = "/hello"
            if path == "/redirect-same":
                target = "/headers"
            elif path == "/redirect-cross":
                target = self.server.cross_url + "/headers"
            elif path == "/redirect-loop":
                target = "/redirect-loop"
            elif path == "/redirect-protocol":
                target = "file:///must-not-be-opened"
            elif path == "/redirect-307":
                status, target = 307, "/method"
            elif path == "/redirect-303":
                status, target = 303, "/method"
            elif path == "/redirect-upload-cross":
                status, target = 307, self.server.cross_url + "/method"
            elif path == "/downgrade":
                target = self.server.plain_url + "/hello"
            headers += [("Location", target)]
            output = b"redirect"
        headers += [("X-Repeated", "first"), ("X-Repeated", "second")]
        self.send_response(status)
        self.send_header("Content-Length", str(len(output)))
        for name, value in headers:
            self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(output)

    do_GET = do_HEAD = do_POST = do_PUT = do_PATCH = do_DELETE = handle_request


def certificate(root, name, ca=None, san=None):
    key, pem = root / f"{name}.key", root / f"{name}.pem"
    run = lambda args: subprocess.run(["openssl", *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if ca is None:
        run(["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key), "-out", str(pem),
             "-subj", f"/CN=Source2Root-{name}", "-days", "2", "-addext", "basicConstraints=critical,CA:TRUE"])
    else:
        csr, ext = root / f"{name}.csr", root / f"{name}.ext"
        ext.write_text(f"subjectAltName={san}\nextendedKeyUsage=serverAuth\n")
        run(["req", "-new", "-newkey", "rsa:2048", "-nodes", "-keyout", str(key), "-out", str(csr), "-subj", "/CN=localhost"])
        run(["x509", "-req", "-in", str(csr), "-CA", str(ca), "-CAkey", str(ca.with_suffix('.key')),
             "-CAcreateserial", "-out", str(pem), "-days", "2", "-extfile", str(ext)])
    return pem, key


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("test executable is required")
    Path(args.root).mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="http-", dir=args.root) as directory:
        root = Path(directory)
        ca, _ = certificate(root, "ca")
        bad_ca, _ = certificate(root, "bad-ca")
        valid, valid_key = certificate(root, "server", ca, "DNS:localhost,IP:127.0.0.1")
        wrong, wrong_key = certificate(root, "wrong", ca, "DNS:wrong.invalid")
        (root / "upload.bin").write_bytes(b"file\0\xffpayload")
        servers = [Server(("127.0.0.1", 0), Handler) for _ in range(4)]
        for server, cert, key in [(servers[2], valid, valid_key), (servers[3], wrong, wrong_key)]:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.minimum_version = ssl.TLSVersion.TLSv1_2
            context.load_cert_chain(cert, key)
            server.socket = context.wrap_socket(server.socket, server_side=True)
        urls = [("https" if i >= 2 else "http") + f"://127.0.0.1:{server.server_port}" for i, server in enumerate(servers)]
        threads = []
        try:
            for server in servers:
                server.cross_url, server.plain_url = urls[1], urls[0]
                thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.05}, daemon=True)
                thread.start()
                threads.append(thread)
            env = dict(os.environ, SR_HTTP_URL=urls[0], SR_HTTP_TLS_URL=urls[2], SR_HTTP_WRONG_NAME_URL=urls[3],
                       SR_HTTP_CA=str(ca), SR_HTTP_BAD_CA=str(bad_ca), SR_HTTP_FIXTURE_FILES=str(root),
                       http_proxy="http://127.0.0.1:1", https_proxy="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1",
                       no_proxy="", NO_PROXY="")
            return subprocess.run(command, env=env, timeout=50).returncode
        finally:
            for server in servers:
                server.shutdown()
                server.server_close()
            for thread in threads:
                thread.join()


if __name__ == "__main__":
    raise SystemExit(main())
