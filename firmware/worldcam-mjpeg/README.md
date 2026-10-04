<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# WorldCam real-live firmware

Branch: `feature/worldcam-mjpeg`

Firmware: `FoloToy-AI-Passport-worldcam-real-live.bin`

This is the merged ESP32-C3 image. Flash it at offset `0x0`.

It autoplays the curated public MJPEG live-source list after Wi-Fi connects,
supports UP/DOWN source switching, OK reconnect, automatic source failover,
and preserves the original WorldCam snapshot catalogue.

The file is built from this branch's source; branch CI regenerates it.
Verify it against `SHA256SUMS.txt` before flashing.

## Wi-Fi setup

On first boot, connect your phone to the `WorldCam-XXXX` hotspot shown on the
device. The setup hotspot is open and requires no password. Open
`http://192.168.4.1` and enter your 2.4 GHz Wi-Fi network name and password.
Hold OK to open setup again later.

Flashing this merged image replaces the firmware and clears saved NVS settings,
including Wi-Fi configuration.
