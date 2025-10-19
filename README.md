# Two-Level Branch Predictor Simulator

A full implementation of a configurable **two-level branch predictor** (BTB + history + 2-bit FSMs), supporting both **local** and **global** history and tables, with optional PC-sharing (LSB or MID).  
Implements the `bp_api.h` interface and runs with the provided `bp_main.c` harness.

---

## 📘 Overview

- **BTB (direct-mapped)** indexed by `PC[2 + log2(BTB_SIZE):2]`, storing a TAG from higher PC bits.  
  On a BTB miss, the predictor assumes **Not Taken** and predicts `dst = PC + 4`.  
- **FSMs (2-bit saturating)** — global or per-BTB-entry. Each counter initializes to the configured state (`SNT`, `WNT`, `WT`, `ST`).  
- **History** — either **global** (shared among all entries) or **local** (stored per BTB entry).  
- **Sharing modes** — optional XOR of history with slices of the PC (LSB or MID bits).  
- **Update logic** — FSM updates use the *previous* history; mispredictions counted when predicted target ≠ actual.  
- **Size calculation** — the code computes the predictor size in bits according to BTB fields, histories, and FSMs.

---

## 📂 Project Structure

```
branch-predictor-l2/
├─ src/
│  ├─ bp.c            # main predictor implementation
│  ├─ bp_main.c       # harness (reads trace and runs predictor)
│  └─ bp_api.h        # predictor API definitions
├─ tools/
│  └─ Makefile        # build script
├─ .gitignore
├─ LICENSE
└─ README.md
```


---

## ⚙️ Build Instructions

```bash
make        # builds bp_main executable
make clean  # removes object files and binary
```
---

## ▶️ Run Example
The harness reads a trace file that specifies the configuration and branch events.
### Example run:
```bash
./bp_main input_examples/config1.txt
```
### Trace format:
```
<btbSize> <historyBits> <tagBits> <initFsmState> <local|global_history> <local|global_tables> <not_using_share|using_share_lsb|using_share_mid>
<pc> <T|N> <targetPc>
...
```
### Output example:
```
flush_num: 26, br_num: 600, size: 14400b
```
---
## 🧱 Implementation Details
- ### Indexing and Tags:
  Each BTB entry uses the PC index bits and a tag to differentiate entries with the same index.

- ### Global vs Local FSM Tables:
  - Global: one FSM table of size 2^historyBits.
  - Local: each BTB entry has its own FSM table of size 2^historyBits.

- ### History Update:
  Histories shift left and insert 1 or 0 depending on branch outcome.

- ### Sharing (gshare/lshare):
  The history value is XORed with PC slices:
  - LSB mode: bits [2 + historyBits - 1 : 2]
  - MID mode: bits [16 + historyBits - 1 : 16]

- ### Stats:
  - flush_num — number of mispredictions
  - br_num — number of branches
  - size — total bits used by predictor

---

## 🚫 Not Included
- Course assignment PDF (CompArch-hw1.pdf)
- Input traces (input_examples/)
