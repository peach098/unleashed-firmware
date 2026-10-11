// FNFC-01.10 host harness: include after lib/digital_signal/digital_signal.c and digital_sequence.c.
// - captures every ARR value digital_sequence_transmit() writes into the DMA ring (stub DMA hooks);
// - records the period durations each DigitalSignal was built from (10 ps units, as requested by the
//   caller), so the ideal edge times of a whole sequence are known independently of the arithmetic
//   under test. Preset sources included after this header call the recording wrappers.
#pragma once
#include <math.h>
#include <inttypes.h>

DWT_Type host_dwt;
DMA_TypeDef host_dma1;
TIM_TypeDef host_tim2;

/* ---- ring capture ---- */
#define CAP_MAX (1U << 20)
static DigitalSequence* cap_seq;
static uint32_t cap_pos;
static uint32_t* cap_arr; // captured ARR values, in order
static uint32_t cap_n;

static void cap_drain(void) {
    if(!cap_seq) return;
    DigitalSequenceRingBuffer* ring = &cap_seq->timer_buf;
    while(cap_pos != ring->write_pos) {
        furi_check(cap_n < CAP_MAX);
        cap_arr[cap_n++] = ring->data[cap_pos];
        cap_pos = (cap_pos + 1) % DIGITAL_SEQUENCE_RING_BUFFER_SIZE;
    }
}
uint32_t host_dma_get_data_length(void) {
    cap_drain(); // everything written so far has been "sent": read position = last written entry
    const uint32_t read_pos = (cap_seq->timer_buf.write_pos + DIGITAL_SEQUENCE_RING_BUFFER_SIZE - 1) %
                              DIGITAL_SEQUENCE_RING_BUFFER_SIZE;
    return DIGITAL_SEQUENCE_RING_BUFFER_SIZE - read_pos;
}
void host_dma_stop(void) {
    cap_drain();
}
static void cap_transmit(DigitalSequence* seq) {
    if(!cap_arr) cap_arr = malloc(CAP_MAX * sizeof(uint32_t));
    cap_seq = seq;
    cap_pos = 0;
    cap_n = 0;
    host_tim2.ARR = DIGITAL_SEQUENCE_TIMER_MAX; // the finish loop sees the end marker at once
    digital_sequence_transmit(seq);
    cap_seq = NULL;
}

/* ---- recording of the requested durations ---- */
#define REC_SIGNALS 64
#define REC_PERIODS 2048
typedef struct {
    const DigitalSignal* signal;
    uint32_t n;
    uint64_t period[REC_PERIODS]; // 10 ps units
} Rec;
static Rec rec[REC_SIGNALS];
static uint32_t rec_count;

static void rec_reset(void) {
    memset(rec, 0, sizeof(rec));
    rec_count = 0;
}
static Rec* rec_get(const DigitalSignal* s) {
    for(uint32_t i = 0; i < rec_count; i++)
        if(rec[i].signal == s) return &rec[i];
    furi_check(rec_count < REC_SIGNALS);
    rec[rec_count].signal = s;
    return &rec[rec_count++];
}
static void rec_add_period(DigitalSignal* s, uint32_t t) {
    digital_signal_add_period(s, t);
    Rec* r = rec_get(s);
    furi_check(r->n < REC_PERIODS);
    r->period[r->n++] = t;
}
static void rec_add_period_with_level(DigitalSignal* s, uint32_t t, bool level) {
    const bool extend = s->size != 0 && level == (s->start_level ^ !(s->size % 2));
    digital_signal_add_period_with_level(s, t, level);
    Rec* r = rec_get(s);
    if(extend) {
        r->period[r->n - 1] += t;
    } else {
        furi_check(r->n < REC_PERIODS);
        r->period[r->n++] = t;
    }
}

/* ---- comparison ---- */
typedef struct {
    uint32_t periods_ideal, periods_actual;
    uint64_t ideal_10ps; // sum of the transmitted (non-held) periods as requested
    uint64_t actual_ticks; // sum of (ARR + 1) put into the ring
    double total_err_ticks; // actual - ideal, whole frame
    double max_edge_err_ticks; // max |actual - ideal| over every edge
    uint32_t min_arr;
} TickResult;

#define TICK_10PS (1562.5) // one 64 MHz timer tick in 10 ps units

static TickResult tick_compare(const DigitalSequence* seq) {
    static uint64_t ideal[CAP_MAX];
    uint32_t n = 0;
    bool prev_end_level = false;
    for(uint32_t k = 0; k < seq->size; k++) {
        const DigitalSignal* s = seq->signals[seq->data[k]];
        const Rec* r = rec_get(s);
        furi_check(r->n == s->size);
        for(uint32_t j = 0; j < r->n; j++) {
            if(j == 0 && k > 0 && prev_end_level == s->start_level) {
                ideal[n - 1] += r->period[0]; // equal levels at the joint: one longer period
            } else {
                furi_check(n < CAP_MAX);
                ideal[n++] = r->period[j];
            }
        }
        prev_end_level = s->start_level ^ ((s->size % 2) == 0);
    }
    n -= 1; // the last level is held, never put into the ring

    TickResult res = {.periods_ideal = n, .periods_actual = cap_n, .min_arr = UINT32_MAX};
    uint64_t ci = 0, ca = 0;
    for(uint32_t i = 0; i < n && i < cap_n; i++) {
        ci += ideal[i];
        ca += (uint64_t)cap_arr[i] + 1;
        if(cap_arr[i] < res.min_arr) res.min_arr = cap_arr[i];
        const double e = fabs((double)ca - (double)ci / TICK_10PS);
        if(e > res.max_edge_err_ticks) res.max_edge_err_ticks = e;
    }
    res.ideal_10ps = ci;
    res.actual_ticks = ca;
    res.total_err_ticks = (double)ca - (double)ci / TICK_10PS;
    return res;
}
