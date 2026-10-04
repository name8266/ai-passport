#!/usr/bin/env python3
"""Dependency-free MJPEG test server for the AI Passport experiment branch.

It alternates two tiny baseline-JPEG frames at a configurable FPS so the
Passport can validate long-lived multipart/x-mixed-replace parsing without
depending on any public webcam.
"""
import argparse
import base64
import socket
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

FRAME_A = base64.b64decode(
    "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"
)
FRAME_B = base64.b64decode(
    "/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAoHBwgHBgoICAgLCgoLDhgQDg0NDh0VFhEYIx8lJCIfIiEmKzcvJik0KSEiMEExNDk7Pj4+JS5ESUM8SDc9Pjv/2wBDAQoLCw4NDhwQEBw7KCIoOzs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozs7Ozv/wAARCACgAPADASIAAhEBAxEB/8QAGAABAQEBAQAAAAAAAAAAAAAAAAYFBwT/xAA1EAEAAAMDCgYBAwQDAAAAAAAAAQIEAwXTBgcRFRZVVnSRkhIhNjexwiITFEExUXGBIzNC/8QAGAEBAQEBAQAAAAAAAAAAAAAAAAIDAQX/xAAlEQEAAgEBBwUBAAAAAAAAAAAAAQISUhETMlFhcaEDMUGBsSH/2gAMAwEAAhEDEQA/AJ8Fxd935MUORdDfF8XbbVFpb2k1nNNZWk2mMfFPo8vFCGjRK9m98XoWtihxZazze7ir++OKazze7ir++OKneTplzPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNFlrPN7uKv744prPN7uKv744pvJ0yZ9JRostZ5vdxV/fHFNZ5vdxV/fHFN5OmTPpKNF/T0OSF9XJe9Tdl01Fja0NNNPCa2tJvKbwzRljD846fOX+UAql8tv89na22iyvP2nujnJvm1RqyvP2nujnJvm1T6nvXu5f47o0BqsAAAAAAAAAAAAAAAAAAAAAAABZZFem8qeT+lqjVlkV6byp5P6WqNZU47fX4ivFIsrz9p7o5yb5tUasrz9p7o5yb5tT1Pevcv8d0aA1WAAAAAAAAAAAAAAAAAAAAAAAAssivTeVPJ/S1RqyyK9N5U8n9LVGsqcdvr8RXikWV5+090c5N82qNWV5+090c5N82p6nvXuX+O6NAarAAAAAAAAAAAAAAAAAAAAAAAAWWRXpvKnk/pao1ZZFem8qeT+lqjWVOO31+IrxSLK8/ae6Ocm+bVGrK8/ae6Ocm+bU9T3r3L/AB3RoDVYAAAADfuGSNRct40UIaZqu2sbOSH95/DazSw/3NLCH+02nZG1yZ2RtYAor6pbS8bWis6KELX9ChjLZyy+c1pLJbzyQ0Q/mOjz/wAQi9lXeVlST2tRGot42U171c/6NlCEZKiH/H5TR0w8o/4j5RinPo5kkRX2klvGoo4z2tbGx/aWP6clpZR/bQtP28PB+UZtEfz8P8f10sm+bKslu2htq60tZ7aae1kjCos9FpLo8EdEYxjGMYfl5adH8/3Ivtki21jCryrhaQkrv1reqnjG8Y/pyVNlGXwy/n/1x8UdMv8ATT5Q/wDKUVW2UbXaztjaAKdAAAAAAAAAAWWRXpvKnk/pao1ZZFem8qeT+lqjWVOO31+IrxSLK8/ae6Ocm+bVGrK8/ae6Ocm+bU9T3r3L/HdGgNVgAAAAAAAAAAAAAAAAAAAAAAALLIr03lTyf0tUassivTeVPJ/S1RrKnHb6/EV4pFleftPdHOTfNqjVleftPdHOTfNqep717l/jujQGqwAAAAAAAAAAAAAAAAAAAAAAAFlkV6byp5P6WqNWWRXpvKnk/pao1lTjt9fiK8UiyvP2nujnJvm1RqyvP2nujnJvm1PU969y/wAd0aA1WAAAAAAAAAAAAAAAAAAAAAAAAssivTeVPJ/S1RqyyK9N5U8n9LVGsqcdvr8RXikWV5+090c5N82qNWV5+090c5N82p6nvXuX+O6NAarAAAAAAAAAAAAAAAAAAAAAAAAWWRXpvKnk/pao1ZZFem8qeT+lqjWVOO31+IrxSL+hp7rvrIC7rsqb8pKC1sbae1mhaTyxmh+VpDRGWM0NHlNpQAq9Mtn92O2rtWWxVycZ0HSTENirk4zoOkmIjROF9XiHMbc1lsVcnGdB0kxDYq5OM6DpJiI0ML6vEGNuay2KuTjOg6SYhsVcnGdB0kxEaGF9XiDG3NZbFXJxnQdJMQ2KuTjOg6SYiNDC+rxBjbmstirk4zoOkmIbFXJxnQdJMRGhhfV4gxtzWWxVycZ0HSTENirk4zoOkmIjQwvq8QY25rLYq5OM6DpJiGxVycZ0HSTERoYX1eIMbc1lsVcnGdB0kxDYq5OM6DpJiI0ML6vEGNuay2KuTjOg6SYhsVcnGdB0kxEaGF9XiDG3NZbFXJxnQdJMQ2KuTjOg6SYiNDC+rxBjbmstirk4zoOkmIbFXJxnQdJMRGhhfV4gxtzWWxVycZ0HSTENirk4zoOkmIjQwvq8QY25rLYq5OM6DpJiGxVycZ0HSTERoYX1eIMbc3RaGhufJ7J+/LKyyjoq20rKWaWWWWeSWOmEk+iEIeKOmMfE50DtKY7Zmdu12tdny//Z"
)

class Handler(BaseHTTPRequestHandler):
    fps = 5.0

    def log_message(self, fmt, *args):
        print("%s - %s" % (self.address_string(), fmt % args))

    def do_GET(self):
        if self.path.startswith("/stream.mjpg"):
            self.send_response(200)
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=frame")
            self.end_headers()
            frames = (FRAME_A, FRAME_B)
            index = 0
            try:
                while True:
                    frame = frames[index % 2]
                    index += 1
                    self.wfile.write(b"--frame\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(("Content-Length: %d\r\n\r\n" % len(frame)).encode())
                    self.wfile.write(frame)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
                    time.sleep(1.0 / self.fps)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return

        if self.path.startswith("/snapshot.jpg"):
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(FRAME_A)))
            self.end_headers()
            self.wfile.write(FRAME_A)
            return

        body = (
            "AI Passport MJPEG test server\n"
            "Stream: /stream.mjpg\n"
            "Snapshot: /snapshot.jpg\n"
        ).encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def local_ip():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.connect(("8.8.8.8", 80))
        return sock.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--fps", type=float, default=5.0)
    args = parser.parse_args()
    Handler.fps = max(0.5, min(args.fps, 10.0))
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print("MJPEG test stream: http://%s:%d/stream.mjpg" % (local_ip(), args.port))
    print("Keep the computer and Passport on the same LAN.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
