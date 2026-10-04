<p align="right"><strong>English</strong> · <a href="worldcam-mjpeg-test.zh_CN.md">简体中文</a></p>

# AI Passport MJPEG experiment test

Branch: `feature/worldcam-mjpeg`

This branch does not modify `feature/worldcam`. It adds an isolated MJPEG experiment path alongside the existing snapshot mode.

## 1. Start the deterministic test stream

Put the computer and Passport on the same LAN, then run from the repository root:

```bash
python3 tools/mjpeg_test_server.py --fps 5
```

The terminal prints an address similar to:

```text
MJPEG test stream: http://192.168.1.23:8080/stream.mjpg
```

The server uses standard `multipart/x-mixed-replace` and alternates two 240×160 baseline JPEG frames. It has no OpenCV, FFmpeg, or Pillow dependency.

## 2. Test on Passport

1. Long-press OK to open WorldCam network setup.
2. Connect the phone to the Passport AP and open `192.168.4.1`.
3. Enter the 2.4 GHz Wi-Fi credentials.
4. Put the printed `http://.../stream.mjpg` URL in the MJPEG test field.
5. Save. After Wi-Fi connects, the device enters the MJPEG live test automatically.
6. The status should advance as `LIVE · MJPEG · frame N` while the two frames alternate.
7. Long-press OK to leave the stream test and return to the existing WorldCam map.

## Current limits

- Goal: validate sustained MJPEG receive/decode on ESP32-C3 using a real long-lived connection.
- HTTP/HTTPS, redirects, multipart MJPEG, and concatenated JPEG byte streams are accepted.
- Only baseline JPEG is supported; progressive JPEG reports an explicit error.
- Compressed frame size is capped at 512 KiB.
- This POC still uses a full RGB565 framebuffer. If real-device testing is stable, the next optimization is strip/DMA direct display to lower peak RAM.
- A broken MJPEG connection never masquerades as a fresh frame; the last good frame remains visible and OK reconnects.
