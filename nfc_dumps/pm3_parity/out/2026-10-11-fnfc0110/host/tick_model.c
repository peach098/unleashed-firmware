// FNFC-01.10 host test: tick arithmetic of lib/digital_signal (compiled unchanged from the tree).
// For every frame, the sum of (ARR + 1) that digital_sequence_transmit() puts into the DMA ring is
// compared with the ideal duration requested by the signal builder (10 ps units / 1562.5 per tick).
//   1. boundary merge: equal levels at a joint must give the sum of both periods
//   2. ISO15693 frames 0..323 B (+ 1027 B), both data rates: |frame error| <= 1 tick
//   3. ISO14443-A frames 1..18 B and short frames: |frame error| <= 1 tick
//   4. random signals / sequences (mixed joints, small periods): |error| <= 1 tick at every edge
// Exit status = number of failed checks.
#include <furi.h>
#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_tim.h>
#include "digital_signal/digital_signal.c"
#include "digital_signal/digital_sequence.c"
#include "tick_harness.h"

#define digital_signal_add_period            rec_add_period
#define digital_signal_add_period_with_level rec_add_period_with_level
#include "digital_signal/presets/nfc/iso15693_signal.c"
#undef BITS_IN_BYTE
#include "digital_signal/presets/nfc/iso14443_3a_signal.c"
#undef digital_signal_add_period
#undef digital_signal_add_period_with_level

static GPIO_TypeDef port;
static const GpioPin pin = {.port = &port, .pin = 1};
static int failures;

static uint32_t rnd_state = 0x2026A11U;
static uint32_t rnd(void) { // xorshift32, deterministic
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}

static void expect(bool ok, const char* what) {
    if(!ok) {
        failures++;
        printf("FAIL: %s\n", what);
    }
}

/* 1. two signals with equal levels at the joint == the same periods sent separately */
static void test_merge(void) {
    rec_reset();
    // every period an exact multiple of the tick (3125 x 10 ps = 2 ticks): no rounding at all
    DigitalSignal* a = digital_signal_alloc(8);
    DigitalSignal* b = digital_signal_alloc(8);
    DigitalSignal* c = digital_signal_alloc(8);
    digital_signal_set_start_level(a, true);
    for(int i = 0; i < 3; i++) rec_add_period(a, 3125 * 2); // H L H, 4 ticks each, ends high
    digital_signal_set_start_level(b, true);
    for(int i = 0; i < 2; i++) rec_add_period(b, 3125 * 3); // H L, 6 ticks each: joint a|b high-high
    digital_signal_set_start_level(c, true);
    rec_add_period(c, 3125 * 2); // H after L: no merge; c's only period is the held one
    DigitalSequence* seq = digital_sequence_alloc(8, &pin);
    digital_sequence_register_signal(seq, 0, a);
    digital_sequence_register_signal(seq, 1, b);
    digital_sequence_register_signal(seq, 2, c);
    digital_sequence_add_signal(seq, 0);
    digital_sequence_add_signal(seq, 1);
    digital_sequence_add_signal(seq, 2);
    cap_transmit(seq);
    TickResult r = tick_compare(seq);
    printf(
        "merge: periods %u (expected %u), ticks %" PRIu64 " (expected 24: 4+4+(4+6)+6)\n",
        r.periods_actual,
        r.periods_ideal,
        r.actual_ticks);
    expect(r.periods_actual == r.periods_ideal, "merge: period count");
    expect(r.actual_ticks == 24, "merge: merged period != sum of both");
    digital_sequence_free(seq);
    digital_signal_free(a);
    digital_signal_free(b);
    digital_signal_free(c);
}

/* 2. ISO15693 */
static void iso15693_frame(Iso15693Signal* sig, Iso15693SignalDataRate rate, const uint8_t* d, size_t n, TickResult* out) {
    iso15693_signal_encode(sig, rate, d, n);
    cap_transmit(sig->tx_sequence);
    *out = tick_compare(sig->tx_sequence);
}

static void test_iso15693(void) {
    rec_reset();
    Iso15693Signal* sig = iso15693_signal_alloc(&pin);
    static uint8_t d[ISO15693_3_LISTENER_BUFFER_SIZE];
    double worst = 0, worst_edge = 0;
    size_t worst_n = 0;
    int worst_rate = 0;
    uint32_t frames = 0;
    // fixed sizes first: replies of the SLIX2 listener (1/8/32/64/80 blocks) and the buffer maximum
    const size_t fixed[] = {0, 1, 7, 12, 35, 131, 259, 323, ISO15693_3_LISTENER_BUFFER_SIZE};
    for(int rate = 0; rate < Iso15693SignalDataRateNum; rate++) {
        for(size_t f = 0; f < COUNT_OF(fixed); f++) {
            for(size_t j = 0; j < fixed[f]; j++) d[j] = (uint8_t)(j * 37U + 11U);
            TickResult r;
            iso15693_frame(sig, rate, d, fixed[f], &r);
            frames++;
            const double ppm = r.ideal_10ps ? r.total_err_ticks / ((double)r.ideal_10ps / TICK_10PS) * 1e6 : 0;
            printf(
                "iso15693 rate=%s bytes=%-4zu periods=%u/%u ticks=%" PRIu64 " ideal=%.3f err=%+.3f ticks (%+.1f ppm) max_edge_err=%.3f\n",
                rate ? "lo" : "hi",
                fixed[f],
                r.periods_actual,
                r.periods_ideal,
                r.actual_ticks,
                (double)r.ideal_10ps / TICK_10PS,
                r.total_err_ticks,
                ppm,
                r.max_edge_err_ticks);
            expect(r.periods_actual == r.periods_ideal, "iso15693: period count");
            if(fabs(r.total_err_ticks) > fabs(worst)) worst = r.total_err_ticks, worst_n = fixed[f], worst_rate = rate;
            if(r.max_edge_err_ticks > worst_edge) worst_edge = r.max_edge_err_ticks;
        }
    }
    // random frames 1..323 B, both rates
    for(uint32_t i = 0; i < 2000; i++) {
        const size_t n = 1 + rnd() % 323;
        const int rate = rnd() % Iso15693SignalDataRateNum;
        for(size_t j = 0; j < n; j++) d[j] = (uint8_t)rnd();
        TickResult r;
        iso15693_frame(sig, rate, d, n, &r);
        frames++;
        expect(r.periods_actual == r.periods_ideal, "iso15693 random: period count");
        if(fabs(r.total_err_ticks) > fabs(worst)) worst = r.total_err_ticks, worst_n = n, worst_rate = rate;
        if(r.max_edge_err_ticks > worst_edge) worst_edge = r.max_edge_err_ticks;
    }
    printf(
        "iso15693: %u frames, worst frame error %+.3f ticks (%zu B, rate %s), worst edge error %.3f ticks\n",
        frames,
        worst,
        worst_n,
        worst_rate ? "lo" : "hi",
        worst_edge);
    expect(fabs(worst) <= 1.0, "iso15693: frame error > 1 tick");
    expect(worst_edge <= 1.0, "iso15693: edge error > 1 tick");
    iso15693_signal_free(sig);
}

/* 3. ISO14443-A */
static void test_iso14443a(void) {
    rec_reset();
    Iso14443_3aSignal* sig = iso14443_3a_signal_alloc(&pin);
    uint8_t d[18], par[3];
    double worst = 0, worst_edge = 0;
    uint32_t frames = 0;
    for(uint32_t i = 0; i < 2000; i++) {
        const size_t bits = (i % 4 == 0) ? 1 + rnd() % 7 : 8 * (1 + rnd() % 18);
        for(size_t j = 0; j < sizeof(d); j++) d[j] = (uint8_t)rnd();
        for(size_t j = 0; j < sizeof(par); j++) par[j] = (uint8_t)rnd();
        digital_sequence_clear(sig->tx_sequence);
        iso14443_3a_signal_encode(sig, d, par, bits);
        cap_transmit(sig->tx_sequence);
        TickResult r = tick_compare(sig->tx_sequence);
        frames++;
        expect(r.periods_actual == r.periods_ideal, "iso14443a: period count");
        if(fabs(r.total_err_ticks) > fabs(worst)) worst = r.total_err_ticks;
        if(r.max_edge_err_ticks > worst_edge) worst_edge = r.max_edge_err_ticks;
        if(i < 3)
            printf(
                "iso14443a bits=%-3zu periods=%u ticks=%" PRIu64 " ideal=%.3f err=%+.3f max_edge_err=%.3f\n",
                bits,
                r.periods_actual,
                r.actual_ticks,
                (double)r.ideal_10ps / TICK_10PS,
                r.total_err_ticks,
                r.max_edge_err_ticks);
    }
    printf("iso14443a: %u frames, worst frame error %+.3f ticks, worst edge error %.3f ticks\n", frames, worst, worst_edge);
    expect(fabs(worst) <= 1.0, "iso14443a: frame error > 1 tick");
    expect(worst_edge <= 1.0, "iso14443a: edge error > 1 tick");
    iso14443_3a_signal_free(sig);
}

/* 4. random signals: random start levels (merged and unmerged joints), periods from 2.5 to ~400
 * ticks with arbitrary fractions, some built with add_period_with_level extensions */
static void test_random(void) {
    double worst = 0, worst_edge = 0;
    uint32_t seqs = 0;
    for(uint32_t round = 0; round < 300; round++) {
        rec_reset();
        DigitalSignal* bank[8];
        DigitalSequence* seq = digital_sequence_alloc(400, &pin);
        for(int b = 0; b < 8; b++) {
            bank[b] = digital_signal_alloc(64);
            const bool level = rnd() & 1;
            const uint32_t periods = 1 + rnd() % 20;
            const bool small = rnd() % 4 == 0;
            for(uint32_t p = 0; p < periods; p++) {
                const uint32_t t = small ? 3907 + rnd() % 3000 : 3907 + rnd() % 600000;
                if(rnd() % 5 == 0 && p > 0)
                    rec_add_period_with_level(bank[b], t, !(level ^ !((bank[b]->size) % 2))); // same level: extend
                else if(p == 0)
                    rec_add_period_with_level(bank[b], t, level);
                else
                    rec_add_period(bank[b], t);
            }
            digital_sequence_register_signal(seq, b, bank[b]);
        }
        const uint32_t len = 1 + rnd() % 400;
        for(uint32_t k = 0; k < len; k++) digital_sequence_add_signal(seq, rnd() % 8);
        cap_transmit(seq);
        TickResult r = tick_compare(seq);
        seqs++;
        expect(r.periods_actual == r.periods_ideal, "random: period count");
        if(fabs(r.total_err_ticks) > fabs(worst)) worst = r.total_err_ticks;
        if(r.max_edge_err_ticks > worst_edge) worst_edge = r.max_edge_err_ticks;
        expect(r.min_arr >= 1, "random: ARR below 1");
        digital_sequence_free(seq);
        for(int b = 0; b < 8; b++) digital_signal_free(bank[b]);
    }
    printf("random: %u sequences, worst frame error %+.3f ticks, worst edge error %.3f ticks\n", seqs, worst, worst_edge);
    expect(fabs(worst) <= 1.0, "random: frame error > 1 tick");
    expect(worst_edge <= 1.0, "random: edge error > 1 tick");
}

/* 5. floor: every signal starts with a 2-tick period (the minimum) and leaves a negative remainder, so
 * the tick that should be removed at each joint cannot be: the correction is deferred and no period
 * may drop below 2 ticks (ARR 1). The frame error is allowed to grow here (pathological input). */
static void test_floor(void) {
    rec_reset();
    DigitalSignal* s = digital_signal_alloc(2);
    digital_signal_set_start_level(s, true);
    rec_add_period(s, 3437); // 2.2 ticks -> 2, remainder +0.2
    rec_add_period(s, 4219); // 2.7 ticks (+0.2) -> 3, remainder -0.1; signal ends low
    DigitalSequence* seq = digital_sequence_alloc(64, &pin);
    digital_sequence_register_signal(seq, 0, s);
    for(int i = 0; i < 64; i++) digital_sequence_add_signal(seq, 0);
    cap_transmit(seq);
    TickResult r = tick_compare(seq);
    printf(
        "floor: periods %u/%u, min ARR %u, frame error %+.3f ticks (deferred corrections)\n",
        r.periods_actual,
        r.periods_ideal,
        r.min_arr,
        r.total_err_ticks);
    expect(r.periods_actual == r.periods_ideal, "floor: period count");
    expect(r.min_arr >= 1, "floor: period shorter than 2 ticks");
    digital_sequence_free(seq);
    digital_signal_free(s);
}

/* 6. digital_signal_add_period over the whole uint32 input range (public API, 10 ps units) */
static void test_add_period_range(void) {
    const uint32_t inputs[] = {3907, 117994, 1887905, 7551622, 530928, 100000000U, 2147483647U, 4000000000U, UINT32_MAX};
    for(size_t i = 0; i < COUNT_OF(inputs); i++) {
        DigitalSignal* s = digital_signal_alloc(1);
        digital_signal_add_period(s, inputs[i]);
        const double ideal = inputs[i] / TICK_10PS;
        const double got = (double)s->data[0] + 1;
        printf("add_period(%u): reload %.0f, ideal %.4f, err %+.4f ticks\n", inputs[i], got, ideal, got - ideal);
        expect(fabs(got - ideal) <= 0.5, "add_period: rounding error > 1/2 tick");
        digital_signal_free(s);
    }
}

/* --frame <hex>: ISO15693 high data rate prediction for one frame (compared with the on-device probe) */
static int one_frame(const char* hex) {
    static uint8_t d[ISO15693_3_LISTENER_BUFFER_SIZE];
    size_t n = 0;
    unsigned v;
    int k;
    while(n < sizeof(d) && sscanf(hex, " %2x%n", &v, &k) == 1) d[n++] = (uint8_t)v, hex += k;
    rec_reset();
    Iso15693Signal* sig = iso15693_signal_alloc(&pin);
    TickResult r;
    iso15693_frame(sig, Iso15693SignalDataRateHi, d, n, &r);
    printf(
        "frame bytes=%zu periods=%u ticks=%" PRIu64 " ideal=%.3f err=%+.3f ticks (%+.2f ppm)\n",
        n,
        r.periods_actual,
        r.actual_ticks,
        (double)r.ideal_10ps / TICK_10PS,
        r.total_err_ticks,
        r.total_err_ticks / ((double)r.ideal_10ps / TICK_10PS) * 1e6);
    iso15693_signal_free(sig);
    return 0;
}

int main(int argc, char** argv) {
    if(argc == 3 && strcmp(argv[1], "--frame") == 0) return one_frame(argv[2]);
    test_merge();
    test_add_period_range();
    test_iso15693();
    test_iso14443a();
    test_random();
    test_floor();
    printf("%s: %d failed check(s)\n", failures ? "FAILED" : "PASSED", failures);
    return failures;
}
