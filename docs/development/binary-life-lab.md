<p align="right"><a href="binary-life-lab.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Binary Life Laboratory: model and operation

This branch is a **standalone artificial-life research terminal**, not the baseline hardware diagnostic menu or a virtual pet. It is a deliberately simplified, reproducible **agent-based evolutionary simulation**, not evidence of biological life, consciousness or abiogenesis.

## Experimental model

- Habitat: 16 × 16 square sites with at most one digital organism per site.
- Genome: 16-bit hereditary string. Bits 0–3 tune uptake/maintenance; 4–7 influence conversion/maintenance; 8–11 set the reproduction threshold; 12–15 set lifespan.
- Resources: local nutrient stock replenishes each generation, optionally with a periodic low-supply phase. Uptake feeds energetic maintenance.
- Selection: organisms with sufficient energy reproduce into one of eight vacant neighboring sites; reproduction halves parental energy. Starvation, aging and lack of nearby free space create selection pressures.
- Mutation: **one randomly chosen genomic bit is flipped per reproduction event**, at a configurable probability of 0, 1/512, 1/128 or 1/32. Offspring are not processed again during the birth generation.
- Statistics: generation count, current population, number of distinct 16-bit genotypes, energy, age, cumulative births/deaths and mutation count.
- Random generator: Xorshift32 stored inside the world state. An identical seed, settings, and operation sequence produce identical results (host-tested). The seed shown on device is useful to label experiment runs; the settings screen supports restarting from the same seed.

**Limitations:** The genome maps to fixed traits; it does not encode a Turing-complete executable program. The observations concern this model only. There is no network synchronization or experimental data export yet.

## Three-button UI

UP/DOWN: cycle *Ecology / Genome / Settings* pages. The ecology page shows colored occupancy and resource grid. OK pauses/resumes the experiment; while paused the current state is saved. The genome page shows one organism's 16-bit sequence and decoded traits; OK advances the sample. On Settings, OK changes mutation probability, long UP toggles supply cycles, long DOWN restarts the **same-seed** experiment, and long OK starts an experiment with a **new random seed**.

The display uses a dedicated dark research-dashboard interface with explicit LVGL CJK 16px font selection and optional Montserrat fallback. Battery state appears in the top-right corner when available.

## Storage and verification

A versioned, hashed NVS blob stores resource field, organisms, experiment seed, PRNG state, counters and controls. Saved every 120 simulated generations (about 30 seconds at 250 ms/step) and immediately when settings/pause change. Power loss **before the next checkpoint may lose recent steps**; flash durability limits prohibit saving every frame. Do not erase the device NVS if restoration is desired.

Run `./tools/validate.sh --static` and `./tools/validate.sh --firmware` in an ESP-IDF 5.5.3 environment. On device, verify Chinese glyphs, three buttons, battery indicator, redraw performance, memory headroom, restored state and sample switching. A successful build is not an on-device result.
