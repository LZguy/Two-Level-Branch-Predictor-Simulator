// 046267 – Computer Architecture – HW 1
// Two–level branch‑predictor simulator – improved version
// --------------------------------------------------------


#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "bp_api.h"

// ------------------------------------------------------------
//                         Data structures
// ------------------------------------------------------------

typedef struct {
    bool     valid;
    uint32_t tag;
    uint32_t target;
    uint32_t localHist;   // kept only when history is local
} BTB_Entry;

// Global simulator state (allocated once by BP_init) ----------
static BTB_Entry *BTB          = NULL;  // BTB entries array
static uint8_t   *FSM          = NULL;  // finite‑state‑machine counters
static uint32_t   GHR          = 0;     // global history register
static SIM_stats  stats        = {0};   // collected statistics

// Configuration parameters (copied from BP_init arguments) ----
static unsigned   btbSize      = 0;     // # of BTB entries (power‑of‑two)
static unsigned   histSize     = 0;     // width of every history register ∈[1,32]
static unsigned   tagSize      = 0;     // width of TAG *field* kept in BTB
static uint8_t    initState    = 0;     // initial 2‑bit FSM state (0…3)
static bool       globalHist   = false; // true ⇒ single history register (GHR)
static bool       globalTable  = false; // true ⇒ single FSM table (gshare)
static int        shareMode    = 0;     // 0 = none, 1 = lsb, 2 = mid

// Derived bit‑masks & sizes (set by BP_init) ------------------
static uint32_t   btbMask      = 0;     // selects the BTB index bits from PC>>2
static uint32_t   histMask     = 0;     // (1<<histSize)‑1   – always < 2³²
static uint32_t   tagMask      = 0;     // (1<<tagSize)‑1    –  "   "
static unsigned   fsmEntries   = 0;     // |FSM| entries – used for bounds check

// ------------------------------------------------------------
//                        Utility helpers
// ------------------------------------------------------------

static inline unsigned log2_pow2(unsigned n)
{
    /* works for every power‑of‑two n ∈ [1,32] without builtin intrinsics */
    unsigned l = 0;
    while ((1u << l) < n) ++l;
    return l;
}

// Extract BTB tag bits from PC — see Figure 4 in the assignment
static inline uint32_t extract_tag(uint32_t pc)
{
    unsigned idxBits = 2 + log2_pow2(btbSize);      // 2 LSBs are always 0 (alignment)
    return (pc >> idxBits) & tagMask;
}

// Collect *histSize* consecutive bits starting at bit *start* of PC.
// When the requested span exceeds the available 32‑bit range we fall
// back to TAG bits, as required by the specification (page 2 bottom).
static inline uint32_t slice_pc_or_tag(uint32_t pc, unsigned start)
{
    unsigned available = (start >= 32) ? 0 : 32 - start;
    if (histSize <= available) {
        return (pc >> start) & histMask;
    }
    // Need extra bits – take them from the TAG field (starting right after BTB index)
    uint64_t val = ((uint64_t)pc >> start);
    unsigned missing = histSize - available;
    uint64_t tagPart = ((uint64_t)pc >> (2 + log2_pow2(btbSize))) & ((1ull << missing) - 1);
    val |= (tagPart << available);
    return (uint32_t)(val & histMask);
}

/* Returns the index into the FSM table */
static inline uint32_t make_index(uint32_t hist, uint32_t pc)
{
    uint32_t chooser = 0;

    /* sharing is only relevant when using a global table */
    if (globalTable) {
        if (shareMode == 1) {      // using share_lsb
            chooser = slice_pc_or_tag(pc, 2);
        } else if (shareMode == 2) { // using share_mid
            chooser = slice_pc_or_tag(pc, 16);
        }
    }

    return (hist ^ chooser) & histMask;
}


// 2‑bit saturating counter transition -------------------------
static inline uint8_t next_state(uint8_t cur, bool taken)
{
    return taken ? (cur == 3 ? 3 : cur + 1)
                 : (cur == 0 ? 0 : cur - 1);
}

// Map {pc,hist} to an absolute index inside the *FSM* array ----------
static inline uint32_t fsm_offset(uint32_t btbIdx, uint32_t histIdx)
{
    return globalTable ? histIdx : (btbIdx << histSize) | histIdx;
}

// ------------------------------------------------------------
//                          API implementation
// ------------------------------------------------------------

int BP_init(unsigned _btbSize, unsigned _histSize, unsigned _tagSize,
            unsigned fsmState, bool isGlobalHist, bool isGlobalTable, int isShare)
{
    if (_btbSize == 0 || (_btbSize & (_btbSize - 1))) {
        return -1;                                       // BTB size must be power‑of‑two
    }

    // Copy configuration
    btbSize     = _btbSize;
    histSize    = _histSize == 0 ? 1 : _histSize;        // guard against 0 (undefined)
    tagSize     = _tagSize;
    initState   = (uint8_t)(fsmState & 3);
    globalHist  = isGlobalHist;
    globalTable = isGlobalTable;
    shareMode   = isShare;                               // 0 / 1 / 2

    // Derived constants (all *safe* even when width == 32)
    btbMask   = btbSize - 1u;
    histMask  = histSize == 32 ? 0xFFFFFFFFu : ((1u << histSize) - 1u);
    tagMask   = tagSize  == 32 ? 0xFFFFFFFFu : ((1u << tagSize)  - 1u);

    fsmEntries = globalTable ? (1u << histSize) : btbSize * (1u << histSize);

    // Allocate resources (calloc ⇒ zeroed)
    BTB = calloc(btbSize, sizeof(BTB_Entry));
    FSM = calloc(fsmEntries, sizeof(uint8_t));
    if (!BTB || !FSM) return -1;

    // All FSM counters start at *initState*
    for (unsigned i = 0; i < fsmEntries; ++i) FSM[i] = initState;

    GHR = 0;
    memset(&stats, 0, sizeof(stats));
    return 0;
}

// ------------------------------------------------------------
bool BP_predict(uint32_t pc, uint32_t *dst)
{
    stats.br_num++;

    uint32_t btbIdx = (pc >> 2) & btbMask;  // PC is word‑aligned ⇒ ignore 2 LSBs
    BTB_Entry *entry = &BTB[btbIdx];

    if (!entry->valid || entry->tag != extract_tag(pc)) {
        *dst = pc + 4;                       // BTB miss ⇒ predict *not taken*
        return false;
    }

    uint32_t hist = globalHist ? GHR : entry->localHist;
    uint32_t fi   = make_index(hist, pc);
    uint8_t  fsm  = FSM[fsm_offset(btbIdx, fi)];

    bool predictTaken = (fsm >= 2);
    *dst = predictTaken ? entry->target : pc + 4;
    return predictTaken;
}

// ------------------------------------------------------------
void BP_update(uint32_t pc, uint32_t targetPc, bool taken, uint32_t pred_dst)
{
    uint32_t btbIdx = (pc >> 2) & btbMask;
    BTB_Entry *entry = &BTB[btbIdx];
    bool hit = entry->valid && entry->tag == extract_tag(pc);

    // Allocate / replace BTB entry when needed ----------------
    if (!hit) {
        entry->valid     = true;
        entry->tag       = extract_tag(pc);
        entry->target    = targetPc;
        if (!globalHist) entry->localHist = 0;

        // Reset per‑entry FSM slice when tables are local
        if (!globalTable) {
            unsigned base = btbIdx << histSize;
            for (unsigned i = 0; i < (1u << histSize); ++i) {
                FSM[base + i] = initState;
            }
        }
    } else {
        entry->target = targetPc;           // always keep last **computed** target
    }

    // Use *previous* history value to update the FSM ----------
    uint32_t hist      = globalHist ? GHR : entry->localHist;
    uint32_t histIndex = make_index(hist, pc);
    uint32_t fsmIdx    = fsm_offset(btbIdx, histIndex);
    FSM[fsmIdx]        = next_state(FSM[fsmIdx], taken);

    // Shift‑in the *actual* outcome into the (global / local) history
    if (globalHist) {
        GHR = ((GHR << 1) | (taken ? 1u : 0u)) & histMask;
    } else {
        entry->localHist = ((hist << 1) | (taken ? 1u : 0u)) & histMask;
    }

    // Count pipeline flushes (mispredictions) -----------------
    uint32_t correctDst = taken ? targetPc : pc + 4;
    if (pred_dst != correctDst) stats.flush_num++;
}

// ------------------------------------------------------------
void BP_GetStats(SIM_stats *out)
{
    // Predictor *size* in bits (theoretical)
    unsigned btbBits = btbSize * (1          /*valid*/
                                 + tagSize  /*tag*/
                                 + 30       /*target PC*/
                                 + (globalHist ? 0 : histSize));

    unsigned histBits = globalHist ? histSize : 0;
    unsigned fsmBits  = (globalTable ? 1 : btbSize) * (1u << histSize) * 2; // 2 bits per counter

    stats.size = btbBits + histBits + fsmBits;

    *out = stats;

    // Free dynamic memory exactly once (as guaranteed by the main)
    free(BTB);  BTB = NULL;
    free(FSM);  FSM = NULL;
}

