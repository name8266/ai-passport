<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Brake Hero 2.1.0

[Download merged firmware](Brake-Hero-v21-full.bin) · [Source](https://github.com/name8266/ai-passport/tree/4c468d458756844ee2899ffbdfb506c835bea949) · [Manifest](Brake-Hero-v21-manifest.json)

The centered rear camera fixes the vehicle/road perspective mismatch. A detailed generated V800 body texture replaces the coarse mesh; tent opening, brake lamps, cracks, dust and debris remain animated at runtime. This is game artwork, not a manufacturer CAD model or a physical crash simulation. Chinese controls and the continuous audio worker from v2 remain included.

![Old and new native render](Brake-Hero-v21-comparison.png)

These previews use the same renderer as the firmware at 240 × 320; they are not device photographs. [All animation states](Brake-Hero-v21-preview.png). The [generated artwork, full prompt and provenance](../../assets/images/brake-hero/source/vehicle-rear-v21-prompt.txt) are retained with the source. Built-in image generation produced the body texture.

- Build: PASS — ESP-IDF 5.5.3, ESP32-C3, 8 MB, no PSRAM; merged image and matching archive verified.
- Host tests: PASS — gameplay, 10,000 simulated runs, audio continuity, centered silhouette, tent/lamp/damage animation, renderer/DMA bounds, Chinese font coverage; ASan/UBSan passed.
- Device tests: NOT RUN for v2.1. Physical audio continuity, frame rate, watchdog behavior and display/buttons require hardware acceptance.
- Complete repository gate: FAIL due to inherited `worldcam-site/README.md` English prose and missing Chinese-peer errors. The firmware build and application checks passed separately.

Full image: 962880 bytes; flash offset `0x0`. A merged flash may reset settings and scores. No v2.1 flash has been performed. Debugging ELF/MAP are retained locally and are not published here.

SHA-256: `263cee0ff1d26b0df03d0a4d95bb45c18f69538f85cc69e023f63ab3e6c8143c`

Matching ELF SHA-256: `af6a87decba67ae72ef10893bc7e9053c74f9120f22286b6b51778de926bb235`
