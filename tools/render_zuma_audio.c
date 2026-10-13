/* Host preview of the exact firmware synthesizer. Build with main/zuma_synth.c. */
#include "zuma_synth.h"
#include <stdint.h>
#include <stdio.h>

static void le16(FILE *f, uint16_t v) {
    fputc(v & 255, f);
    fputc(v >> 8, f);
}
static void le32(FILE *f, uint32_t v) {
    le16(f, v & 65535u);
    le16(f, v >> 16);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s output.wav\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) { perror(argv[1]); return 1; }
    enum { SECONDS = 16, FRAMES = ZUMA_SYNTH_RATE * SECONDS, CHUNK = 256 };
    uint32_t bytes = FRAMES * sizeof(int16_t);
    fwrite("RIFF", 1, 4, f); le32(f, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, f); le32(f, 16);
    le16(f, 1); le16(f, 1); le32(f, ZUMA_SYNTH_RATE);
    le32(f, ZUMA_SYNTH_RATE * 2); le16(f, 2); le16(f, 16);
    fwrite("data", 1, 4, f); le32(f, bytes);

    zuma_synth_t synth;
    zuma_synth_init(&synth);
    int16_t pcm[CHUNK];
    for (int frame = 0; frame < FRAMES; frame += CHUNK) {
        if (frame == 2 * ZUMA_SYNTH_RATE) zuma_synth_cue(&synth, ZUMA_SOUND_SHOOT);
        if (frame == 4 * ZUMA_SYNTH_RATE) zuma_synth_cue(&synth, ZUMA_SOUND_MATCH);
        if (frame == 6 * ZUMA_SYNTH_RATE) zuma_synth_cue(&synth, ZUMA_SOUND_COMBO);
        if (frame == 10 * ZUMA_SYNTH_RATE) zuma_synth_cue(&synth, ZUMA_SOUND_SWAP);
        if (frame == 14 * ZUMA_SYNTH_RATE) zuma_synth_cue(&synth, ZUMA_SOUND_WIN);
        zuma_synth_render(&synth, pcm, CHUNK);
        if (fwrite(pcm, sizeof(pcm), 1, f) != 1) { perror("write"); fclose(f); return 1; }
    }
    if (fclose(f) != 0) { perror("close"); return 1; }
    return 0;
}
