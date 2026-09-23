#!/usr/bin/env python3
"""AxsOS masaüstünü tarayıcıda göstermek için küçük sunucu.

Tek port üzerinden:
  /            -> noVNC istemcisi (statik dosyalar)
  /websockify  -> WebSocket <-> QEMU VNC (TCP) köprüsü

Yalnızca Python standart kütüphanesi kullanır. Codespaces port yönlendirmesi ve
Google Cloud Shell "Web Önizleme" ile uyumludur (tek HTTP portu yeterli).

Kullanım: novnc-server.py <novnc_dizini> <http_port> <vnc_host:port>
"""
import base64
import hashlib
import http.server
import os
import socket
import socketserver
import struct
import sys
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
NOVNC, PORT, VNC = sys.argv[1], int(sys.argv[2]), sys.argv[3]
VNC_HOST, VNC_PORT = VNC.rsplit(":", 1)
VNC_PORT = int(VNC_PORT)


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise ConnectionError
        buf += chunk
    return buf


def ws_to_tcp(ws, tcp):
    """İstemciden gelen (maskeli) WebSocket çerçevelerini VNC'ye aktar."""
    try:
        while True:
            h = recv_exact(ws, 2)
            op = h[0] & 0x0F
            ln = h[1] & 0x7F
            if ln == 126:
                ln = struct.unpack(">H", recv_exact(ws, 2))[0]
            elif ln == 127:
                ln = struct.unpack(">Q", recv_exact(ws, 8))[0]
            mask = recv_exact(ws, 4) if h[1] & 0x80 else b"\0\0\0\0"
            data = bytearray(recv_exact(ws, ln))
            for i in range(ln):
                data[i] ^= mask[i % 4]
            if op == 8:  # kapat
                break
            if op == 9:  # ping -> pong
                ws.sendall(bytes([0x8A, len(data)]) + bytes(data))
                continue
            if op in (0, 1, 2):
                tcp.sendall(bytes(data))
    except (ConnectionError, OSError):
        pass
    finally:
        for s in (tcp, ws):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


def tcp_to_ws(tcp, ws):
    """VNC'den gelen baytları ikili WebSocket çerçevesi olarak gönder."""
    try:
        while True:
            data = tcp.recv(65536)
            if not data:
                break
            n = len(data)
            if n < 126:
                hdr = bytes([0x82, n])
            elif n < 65536:
                hdr = bytes([0x82, 126]) + struct.pack(">H", n)
            else:
                hdr = bytes([0x82, 127]) + struct.pack(">Q", n)
            ws.sendall(hdr + data)
    except OSError:
        pass
    finally:
        for s in (tcp, ws):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=NOVNC, **kw)

    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.split("?")[0].rstrip("/").endswith("websockify") and \
                self.headers.get("Upgrade", "").lower() == "websocket":
            key = self.headers["Sec-WebSocket-Key"]
            acc = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
            self.send_response(101, "Switching Protocols")
            self.send_header("Upgrade", "websocket")
            self.send_header("Connection", "Upgrade")
            self.send_header("Sec-WebSocket-Accept", acc)
            proto = self.headers.get("Sec-WebSocket-Protocol")
            if proto:
                self.send_header("Sec-WebSocket-Protocol", "binary" if "binary" in proto else proto.split(",")[0])
            self.end_headers()
            self.wfile.flush()
            try:
                tcp = socket.create_connection((VNC_HOST, VNC_PORT))
            except OSError:
                return
            ws = self.connection
            t = threading.Thread(target=tcp_to_ws, args=(tcp, ws), daemon=True)
            t.start()
            ws_to_tcp(ws, tcp)
            t.join()
            self.close_connection = True
            return
        if self.path in ("/", "/index.html"):
            self.send_response(302)
            self.send_header("Location", "/vnc.html?autoconnect=1&resize=scale&path=websockify&reconnect=1")
            self.end_headers()
            return
        super().do_GET()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


if __name__ == "__main__":
    Server(("0.0.0.0", PORT), Handler).serve_forever()
