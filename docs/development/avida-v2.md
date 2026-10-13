<p align="right"><a href="avida-v2.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# V2 executable digital-life laboratory: implementation and reproducibility

**Status:** An Avida-inspired embedded experimental implementation, not the official Avida executable, not a claim of bit-for-bit replication of Ofria & Wilke's published results.

## Primary published reference

Charles Ofria and Claus O. Wilke (2004), **Avida: A Software Platform for Research in Computational Evolutionary Biology**, *Artificial Life* 10(2), 191–229. DOI [10.1162/106454604773563612](https://doi.org/10.1162/106454604773563612).

Engineering references: [Avida development guide: Life Cycle](https://github-wiki-see.page/m/devosoft/avida/wiki/Development---Tutorial---Life-Cycle), and [original 2004 article](https://authors.library.caltech.edu/records/2h3br-gvn73).

The core architectural idea is **digital programs that execute their own instructions to allocate, copy, and divide**. The model does not auto-birth organisms based on numerical energy thresholds.

## VM specification

- One genotype is a variable-length sequence of **10–64** executable instructions; 26 opcode names from the Avida HEADS instruction set.
- A bounded virtual CPU for each individual: instruction head, read/write/flow heads, three 32-bit registers, two bounded stacks, copied daughter instruction buffer and executed-instruction tracking.
- The starting population contains 16 copies of the **published 15-instruction HEADS ancestor** (Appendix A.3): `h-alloc, h-search, nop-C, nop-A, mov-head, nop-C, h-search, h-copy, if-label, nop-C, nop-A, h-divide, mov-head, nop-A, nop-B`.
- The copied template is located via complement matching `A→B, B→C, C→A`. `h-copy` writes precisely one instruction per execution; `if-label` recognizes the final complementary label; `h-divide` releases a daughter only after passing size, copied and executed thresholds.
- Instruction substitutions occur **during copying**, while random single-opcode insertions/deletions occur during division. No individual is declared viable before it actually self-replicates.
- A 16×16 habitat limits population to 256 concurrently live programs. Execution is deterministic round-robin, and offspring prefer free adjacent cells; otherwise they replace a neighbor. Faster self-replicators spread through competition for slots.
- Mutation probabilities per copy: 0, 1/1024, 1/256, 1/64; insertion/deletion probabilities per divide: 0, 1/4096, 1/1024, 1/256. These are **configuration choices**, not reported constants from the paper.
- A Xorshift32 random state makes runs reproducible with the same seed, settings and sequence of interactions.

## Observable evolution

- **Ecology:** 16×16 real-time population; color corresponds to inherited lineage. Bright white marks the most recent replication/mutation event. Optional copy-progress and code-length views.
- **Genome:** inspect one living organism's CPU instruction head, next six executable instructions, parent ID, lineage ID, genome length and daughter copy progress.
- **Statistics:** instruction count, births, neighboring replacements, per-copy mutations, insertion/deletion events, genotype diversity and dominant genotype abundance.
- **Controls:** UP/DOWN cycle screens. Ecology OK pauses/resumes. Genome OK inspects next organism. Control OK cycles mutation rates; LONG UP restarts from seed, LONG DOWN switches ecology view and LONG OK initializes a new seed. LONG OK outside controls writes an immediate snapshot.
- Experimental records can also be streamed through USB serial; the provenance of both the firmware build and seed should be retained.

## Persistence, safety and storage boundaries

The firmware changes the V1 partition table by reducing the factory app to `0x7B0000` and reserving the last `0x40000` bytes of 8 MB Flash at `0x7C0000` as `life_data` (type=data, subtype=0x40). Two disjoint `0x20000` slots use erase-then-write, monotonic version/sequence and FNV-1a checksum to recover the latest valid state if power fails part-way through a save. All 256 individuals, partial daughters, heads, registers, statistics, event log and RNG state are stored. Autosave is every 10 minutes, plus pause/control/manual actions. Flash has finite erase cycles. Unsaved recent steps are lost after sudden power loss. **Full-image flashing can destroy existing NVS and life_data; verify intended effects before flashing.** No OTA guarantee or automatic migration of V1 snapshots.

## Difference from original Avida (do not overstate fidelity)

This is a **bounded and adapted HEADS interpreter**, not a full source-code port. It uses 64-instruction fixed per-agent buffers, eight-neighbor competition, simplified CPU scheduling and some simplified opcode/IO semantics. The full Avida environment includes task-based rewards/merit, precise instruction semantics, more complete mutation controls and measurement protocols. Consequently, trajectories and fitness values from this app cannot be presented as identical to a paper's results without a separate reference-implementation comparison.

## Scientific validation protocol

1. Compile and pass host tests including zero-mutation ancestral self-replication, unbiased seed replay, mutated offspring, interpreter execution and corrupted snapshot rejection.
2. Record exact build commit, IDF/LVGL versions, VM version, ancestor program, all mutation settings, PRNG seed and update counts.
3. For a proposed experiment, run at least 30 independent seeds per group with a predefined fixed instruction budget; always include a mutation-off control.
4. Analyze replication rate, survival, genome length, genotype richness and competition replacements; report uncertainty and negative results.
5. Compare interpreter behavior (especially templates, heads, h-copy, and division) against official Avida or independently calculated fixtures before claiming scientific replication.
6. Check on-device typography, memory headroom, restarts, USB logs, flash data retention and CPU responsiveness. **Successful CI does not constitute a device test.**
