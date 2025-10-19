# Two-Level Branch Predictor Simulator

A configurable Level-2 branch predictor (BTB + history + 2-bit FSM tables) with local/global history, local/global tables, and optional PC-sharing (LSB/MID). Implements the interface in `bp_api.h` and runs with the provided `bp_main` harness. :contentReference[oaicite:0]{index=0} :contentReference[oaicite:1]{index=1}

> **Note**  
> This repository includes **my implementation (`bp.c`) only** plus documentation.  
> Course handouts, traces, and scaffolding files remain excluded for copyright reasons. Predictor configuration and trace format follow the official assignment spec. :contentReference[oaicite:2]{index=2}

---

## Overview

- **BTB (direct-mapped)** indexed by `PC[2 + log2(BTB_SIZE) : 2]`, with a per-entry **TAG** taken from higher PC bits; on miss we predict **Not-Taken (dst = PC+4)**. :contentReference[oaicite:3]{index=3}  
- **2-bit saturating FSMs** (ST/WT/WNT/SNT) either **global** (single table) or **local** (per-BTB-entry table); all counters initialize to the configured state. :contentReference[oaicite:4]{index=4}  
- **History** can be **global** (GHR) or **local** (kept in the BTB entry). For global tables, optional **sharing** XORs history with slices of the PC (LSB from bit 2, or MID from bit 16). :contentReference[oaicite:5]{index=5}  
- **Update** always inserts/refreshes a BTB entry (even on NT) with the *computed* target; FSM is updated using the **previous** history; mispredictions counted via `pred_dst != (taken ? targetPc : PC+4)`. (See code.) :contentReference[oaicite:6]{index=6}

---

## Files

```
branch-predictor-l2/
├─ src/
│ └─ bp.c # my full implementation of the predictor
├─ tools/
│  └─ Makefile
├─ docs/
│ └─ README_assets/ (optional images if you add diagrams)
├─ .gitignore
├─ LICENSE
└─ README.md
```


> If you have legitimate access to the harness, place the **course-provided** `bp_api.h` and `bp_main.c` alongside `src/bp.c` (or adjust include paths) and build with your toolchain. Interface signatures are fixed by `bp_api.h`. :contentReference[oaicite:7]{index=7} :contentReference[oaicite:8]{index=8}

---

## Predictor Configuration (trace header)

The harness reads the first line of the trace:  
`<btbSize> <historyBits> <tagBits> <initFsmState> <local|global_history> <local|global_tables> <not_using_share|using_share_lsb|using_share_mid>` :contentReference[oaicite:9]{index=9} :contentReference[oaicite:10]{index=10}

Subsequent lines are branch events:  
`<pc> <T|N> <computed_target_pc>` (PCs are 32-bit, 4-byte aligned). :contentReference[oaicite:11]{index=11}

---

## Build & Run

If you have the harness files:

# example (GCC/Clang), adjust paths as needed
cc -O2 -std=c11 -Isrc -c src/bp.c -o bp.o
cc -O2 -std=c11 -c bp_main.c -o bp_main.o              # provided by the course
cc -O2 -std=c11 bp_main.o bp.o -o bp_main

./bp_main <trace.txt>

The harness prints a line per event with predicted outcome & destination, then a summary:
flush_num: <X>, br_num: <Y>, size: <Z>b where size is the theoretical predictor size in bits (BTB fields, histories, FSM counters).

---

## Notes on Implementation Details
- ### Indexing & tags:
  TAG is taken from bits above the BTB index (after the two LSB zero bits due to alignment). Collisions may occur when tagSize is shorter than the distinguishing width—this is by design in the spec. 

- ### Sharing (gshare/lshare style):
  When tables are global, history is XORed with slices of the PC starting at bit 2 (LSB) or 16 (MID). 

- ### Local vs Global tables:
  FSM table size is 2^historyBits if global; otherwise BTB_SIZE * 2^historyBits. Counters initialize to initFsmState. (See code.) 

- ### Stats:
  br_num counts total update events; flush_num counts mispredicted redirections; size is computed according to the bit accounting described in the assignment.

---

## Not Included
- Assignment PDF, traces, and scaffolding (CompArch-hw1.pdf, bp_main.c, bp_api.h, input_examples/, etc.) are not part of this repo. Use them only if you have proper access.
