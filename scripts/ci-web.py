#!/usr/bin/env python3
"""Test için küçük HTTP sunucusu: bir dizini sunar, PUT ile gelen dosyaları yukarı/ altına kaydeder.
   scripts/ci-web.py <dizin> <port>
QEMU'daki AxsOS 10.0.2.2:<port> adresinden paket deposunu okur ve ekran görüntülerini geri yükler."""
import http.server, os, sys

ROOT = os.path.abspath(sys.argv[1])
PORT = int(sys.argv[2])


class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

    def do_PUT(self):
        name = os.path.basename(self.path.split("?")[0]) or "dosya"
        os.makedirs(os.path.join(ROOT, "yukari"), exist_ok=True)
        n = int(self.headers.get("Content-Length", 0))
        with open(os.path.join(ROOT, "yukari", name), "wb") as f:
            left = n
            while left > 0:
                chunk = self.rfile.read(min(left, 1 << 20))
                if not chunk:
                    break
                f.write(chunk)
                left -= len(chunk)
        self.send_response(201)
        self.end_headers()

    def log_message(self, *a):
        pass


http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
