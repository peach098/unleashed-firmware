// FNFC-01.14 host test of lib/signal_reader/parsers/iso15693/iso15693_parser.c (compiled unchanged).
//
// signal_reader is stubbed: signal_reader_start() keeps the callback, the test plays the ISR and hands
// it one sample byte per event (8 samples of fc/128 = 75.5 us, bit i = sample i, 1 = pause), exactly
// what the DMA half-buffer ISR passes with ISO15693_PARSER_SIGNAL_READER_BUFF_SIZE 2. The "worker
// thread" runs iso15693_parser_run() once per parser callback, `lag` sample bytes after it (the HAL
// waits on a thread flag), like furi_hal_nfc_iso15693_wait_event(); RxEnd is when run() returns true.
// After a restart (Fail) the stub drops idle (0x00) sample bytes until the next edge, like the
// rising/falling edge trigger of the real reader.
//
// Sample byte streams follow what probe v2 captured on the device for PM3 `hf 15 raw -2`
// (p2_probe_log_*.txt): 1-of-256 SOF 0x81, a data byte v = 64 sample bytes with the pause in
// sample byte v/4, bit (v%4)*2+1, EOF 0x04 on the byte boundary; 1-of-4 SOF 0x21, symbols
// 0x02/0x08/0x20/0x80 LSB dibit first, EOF 0x04.
#include <furi.h>
#include <toolbox/bit_buffer.h>
#include <signal_reader/parsers/iso15693/iso15693_parser.h>

int host_critical_depth = 0;

// ---- signal_reader stub ----
struct SignalReader {
    SignalReaderCallback callback;
    void* context;
    bool running;
    bool armed; // waiting for the first edge after start
    int starts;
};
static SignalReader host_reader;

SignalReader* signal_reader_alloc(const GpioPin* gpio_pin, uint32_t size) {
    UNUSED(gpio_pin);
    furi_check(size == 2);
    memset(&host_reader, 0, sizeof(host_reader));
    return &host_reader;
}
void signal_reader_free(SignalReader* instance) {
    UNUSED(instance);
}
void signal_reader_set_pull(SignalReader* instance, GpioPull pull) {
    UNUSED(instance);
    UNUSED(pull);
}
void signal_reader_set_polarity(SignalReader* instance, SignalReaderPolarity polarity) {
    UNUSED(instance);
    UNUSED(polarity);
}
void signal_reader_set_sample_rate(SignalReader* instance, SignalReaderTimeUnit u, uint32_t t) {
    UNUSED(instance);
    UNUSED(u);
    UNUSED(t);
}
void signal_reader_set_trigger(SignalReader* instance, SignalReaderTrigger trigger) {
    UNUSED(instance);
    UNUSED(trigger);
}
void signal_reader_start(SignalReader* instance, SignalReaderCallback callback, void* context) {
    instance->callback = callback;
    instance->context = context;
    instance->running = true;
    instance->armed = true;
    instance->starts++;
}
void signal_reader_stop(SignalReader* instance) {
    instance->running = false;
}

// ---- harness ----
#define MAX_FRAME (1024)
static int pending_wakeups; // parser callbacks not yet served by the thread
static void parser_callback(Iso15693ParserEvent event, void* context) {
    UNUSED(context);
    if(event == Iso15693ParserEventDataReceived) pending_wakeups++;
}

typedef struct {
    uint8_t* s;
    size_t n, cap;
    size_t eof_idx; // index of the EOF sample byte
} Stream;

static void st_put(Stream* st, uint8_t b) {
    if(st->n == st->cap) {
        st->cap = st->cap ? st->cap * 2 : 1024;
        st->s = realloc(st->s, st->cap);
    }
    st->s[st->n++] = b;
}

static void encode_1of4(Stream* st, const uint8_t* d, size_t n) {
    st_put(st, 0x21);
    for(size_t i = 0; i < n; i++)
        for(int k = 0; k < 4; k++)
            st_put(st, 0x02 << (2 * ((d[i] >> (2 * k)) & 3)));
    st->eof_idx = st->n;
    st_put(st, 0x04);
}

static void encode_1of256(Stream* st, const uint8_t* d, size_t n) {
    st_put(st, 0x81);
    for(size_t i = 0; i < n; i++)
        for(int k = 0; k < 64; k++)
            st_put(st, (k == d[i] / 4) ? (uint8_t)(0x02 << (2 * (d[i] % 4))) : 0x00);
    st->eof_idx = st->n;
    st_put(st, 0x04);
}

typedef struct {
    bool rx_end;
    size_t rx_end_idx; // sample byte index at which run() returned true
    size_t len;
    uint8_t data[MAX_FRAME];
    int restarts;
    bool is_1outof256; // iso15693_parser_is_1outof256() at RxEnd (selects the FDT compensation)
} Result;

// Play `st` (followed by `idle` pause-less sample bytes) into the parser
static Result play(Iso15693Parser* p, const Stream* st, size_t idle, int lag, unsigned seed) {
    Result r = {0};
    pending_wakeups = 0;
    const int starts0 = host_reader.starts;
    iso15693_parser_start(p, parser_callback, NULL);
    int countdown = -1;
    srand(seed);
    for(size_t i = 0; i < st->n + idle && !r.rx_end; i++) {
        const uint8_t b = i < st->n ? st->s[i] : 0x00;
        if(host_reader.running && !(host_reader.armed && b == 0x00)) {
            host_reader.armed = false;
            uint8_t buf[1] = {b};
            SignalReaderEventData ed = {.data = buf, .len = 1};
            SignalReaderEvent ev = {.type = SignalReaderEventTypeHalfBufferFilled, .data = &ed};
            furi_check(host_critical_depth == 0); // the ISR never runs inside the critical section
            host_reader.callback(ev, host_reader.context);
        }
        // thread: serve wake-ups `lag` sample bytes later (lag < 0: random 0..-lag)
        if(pending_wakeups && countdown < 0) countdown = lag >= 0 ? lag : rand() % (1 - lag);
        if(countdown == 0) {
            while(pending_wakeups) {
                pending_wakeups--;
                if(iso15693_parser_run(p)) {
                    r.rx_end = true;
                    r.rx_end_idx = i;
                    break;
                }
            }
            countdown = -1;
        } else if(countdown > 0) {
            countdown--;
        }
    }
    if(r.rx_end) {
        size_t bits = 0;
        r.len = iso15693_parser_get_data_size_bytes(p);
        iso15693_parser_get_data(p, r.data, sizeof(r.data), &bits);
        furi_check(bits == r.len * 8);
        r.is_1outof256 = iso15693_parser_is_1outof256(p);
    }
    iso15693_parser_stop(p);
    r.restarts = host_reader.starts - starts0 - 1;
    return r;
}

static int fails = 0, checks = 0;
#define CHECK(cond, ...)                         \
    do {                                         \
        checks++;                                \
        if(!(cond)) {                            \
            fails++;                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                 \
            printf("\n");                        \
        }                                        \
    } while(0)

static const uint8_t rd1[] = {0x22, 0x20, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0, 0x00, 0xB5, 0xC1};
static const uint8_t rdm8[] =
    {0x22, 0x23, 0xAC, 0x83, 0x9A, 0x01, 0x09, 0x01, 0x04, 0xE0, 0x00, 0x07, 0xBC, 0x4B};

// Good frame in both codings: same bytes; RxEnd latency after the EOF sample byte in sample bytes
static void check_good(Iso15693Parser* p, const uint8_t* d, size_t n, int lag, const char* name, size_t* lat4, size_t* lat256) {
    Stream s4 = {0}, s256 = {0};
    encode_1of4(&s4, d, n);
    encode_1of256(&s256, d, n);
    Result r4 = play(p, &s4, 200, lag, 1);
    Result r256 = play(p, &s256, 200, lag, 2);
    CHECK(r4.rx_end && r4.len == n && !memcmp(r4.data, d, n), "%s n=%zu lag=%d: 1-of-4 rx_end=%d len=%zu", name, n, lag, r4.rx_end, r4.len);
    CHECK(r256.rx_end && r256.len == n && !memcmp(r256.data, d, n), "%s n=%zu lag=%d: 1-of-256 rx_end=%d len=%zu", name, n, lag, r256.rx_end, r256.len);
    CHECK(r4.restarts == 0 && r256.restarts == 0, "%s: restarts %d/%d", name, r4.restarts, r256.restarts);
    CHECK(!r4.is_1outof256 && r256.is_1outof256, "%s: coding reported %d/%d", name, r4.is_1outof256, r256.is_1outof256);
    if(r4.rx_end && lat4) *lat4 = r4.rx_end_idx - s4.eof_idx;
    if(r256.rx_end && lat256) *lat256 = r256.rx_end_idx - s256.eof_idx;
    free(s4.s);
    free(s256.s);
}

// Bad 1-of-256 stream: no RxEnd; then a good frame is received (the parser restarted)
static void check_bad_256(Iso15693Parser* p, Stream* bad, const char* name) {
    Result r = play(p, bad, 200, 0, 3);
    // after the restart the rest of the bad frame may start with 0x01, which is a single EOF (an
    // empty frame, FNFC-01.13): only a frame with data counts as accepted
    CHECK(!(r.rx_end && r.len), "%s: frame accepted (len %zu)", name, r.len);
    CHECK(r.restarts >= 1, "%s: parser not restarted", name);
    Stream good = {0};
    encode_1of256(&good, rd1, sizeof(rd1));
    Result g = play(p, &good, 200, 0, 4);
    CHECK(g.rx_end && g.len == sizeof(rd1) && !memcmp(g.data, rd1, sizeof(rd1)), "%s: next good frame lost", name);
    free(good.s);
    free(bad->s);
}

int main(void) {
    GpioPin pin = {0};
    Iso15693Parser* p = iso15693_parser_alloc(&pin, MAX_FRAME);
    iso15693_parser_detect_mode(p);
    uint8_t d[MAX_FRAME + 1];
    srand(1234);

    // 1. PM3 requests and all byte values, both codings, thread lag 0 / 1 / 31 / random
    size_t lat4 = 99, lat256 = 99, lat4_max = 0, lat256_max = 0;
    const int lags[] = {0, 1, 5, 31, -31};
    for(size_t li = 0; li < COUNT_OF(lags); li++) {
        check_good(p, rd1, sizeof(rd1), lags[li], "rd1", &lat4, &lat256);
        if(lags[li] == 0) lat4_max = MAX(lat4_max, lat4), lat256_max = MAX(lat256_max, lat256);
        check_good(p, rdm8, sizeof(rdm8), lags[li], "rdm8", &lat4, &lat256);
        if(lags[li] == 0) lat4_max = MAX(lat4_max, lat4), lat256_max = MAX(lat256_max, lat256);
    }
    printf("rxend: EOF sample byte -> RxEnd with thread lag 0: 1-of-4 max %zu, 1-of-256 max %zu sample bytes\n", lat4_max, lat256_max);
    CHECK(lat4_max == 0, "1-of-4 RxEnd %zu sample bytes after EOF", lat4_max);
    CHECK(lat256_max == 0, "1-of-256 RxEnd %zu sample bytes after EOF (EOF must be seen by the ISR)", lat256_max);

    for(int v = 0; v < 256; v++) d[v] = (uint8_t)v;
    check_good(p, d, 256, 0, "all values", NULL, NULL);
    check_good(p, d, 256, -31, "all values lag", NULL, NULL);
    for(int v = 0; v < 256; v++) d[v] = (uint8_t)(255 - v);
    check_good(p, d, 256, 3, "all values desc", NULL, NULL);

    // 2. lengths 0..40, random data and lag; long frames up to the capacity
    for(size_t n = 1; n <= 40; n++) {
        for(size_t i = 0; i < n; i++) d[i] = rand();
        check_good(p, d, n, -(int)(rand() % 32), "random", NULL, NULL); // the buffer holds 2 chunks
    }
    for(size_t i = 0; i < MAX_FRAME; i++) d[i] = rand();
    check_good(p, d, MAX_FRAME, -31, "capacity", NULL, NULL);
    {
        Stream s = {0};
        encode_1of256(&s, d, MAX_FRAME + 1);
        Result r = play(p, &s, 200, 0, 5);
        CHECK(!r.rx_end, "1-of-256 frame over the capacity accepted");
        free(s.s);
        Stream s4 = {0};
        encode_1of4(&s4, d, MAX_FRAME + 1);
        r = play(p, &s4, 200, 0, 5);
        CHECK(!r.rx_end, "1-of-4 frame over the capacity accepted");
        free(s4.s);
    }

    // 3. invalid 1-of-256 bytes -> Fail and restart
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 64 + 20] = 0x08; // byte 1: a second pause in another sample byte
        check_bad_256(p, &s, "two pauses (two sample bytes)");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 2 * 64 + 0xAC / 4] |= 0x80; // byte 2: two pauses in one sample byte
        check_bad_256(p, &s, "two pauses (one sample byte)");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 64 + 0x20 / 4] = 0x00; // byte 1 without a pause
        check_bad_256(p, &s, "no pause");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 3 * 64 + 0x83 / 4] = 0x04; // byte 3: pause in the first half of the slot
        check_bad_256(p, &s, "pause in the first half slot");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 3 * 64 + 0x83 / 4] = 0x01;
        check_bad_256(p, &s, "pause in the first half slot (bit 0)");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 4 * 64 + 0x9A / 4] = 0x00;
        s.s[1 + 4 * 64 + 63] = 0x04; // EOF pattern in the middle of a byte
        check_bad_256(p, &s, "EOF inside a byte");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.n = s.eof_idx; // the frame stops without EOF: idle carrier follows
        check_bad_256(p, &s, "missing EOF");
    }
    {
        Stream s = {0};
        st_put(&s, 0x81);
        for(int i = 0; i < 64 * 13; i++) st_put(&s, rand() & 0xFF);
        st_put(&s, 0x04);
        check_bad_256(p, &s, "noise");
    }
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 6 * 64 + 0x09 / 4] = 0xAA; // four pauses in one sample byte
        check_bad_256(p, &s, "0xAA sample byte");
    }

    // 4. the frame interpretation does not depend on the decoder state of the previous frame
    {
        Stream s = {0};
        encode_1of256(&s, rd1, sizeof(rd1));
        s.s[1 + 64 + 20] = 0x08;
        Result r = play(p, &s, 0, 0, 6); // stops mid-frame (field off / next frame)
        CHECK(!r.rx_end, "truncated bad frame accepted");
        free(s.s);
        check_good(p, rdm8, sizeof(rdm8), 0, "after a truncated bad frame", NULL, NULL);
    }

    // 5. 1-of-4 frame whose EOF pause is not sampled (pause-less symbol on the byte boundary, FNFC-01.12)
    {
        Stream s = {0};
        encode_1of4(&s, rd1, sizeof(rd1));
        s.s[s.eof_idx] = 0x00;
        Result r = play(p, &s, 200, 0, 7);
        CHECK(r.rx_end && r.len == sizeof(rd1) && !memcmp(r.data, rd1, sizeof(rd1)), "1-of-4 pause-less EOF");
        free(s.s);
    }
    // 1-of-4 invalid symbol -> Fail
    {
        Stream s = {0};
        encode_1of4(&s, rd1, sizeof(rd1));
        s.s[5] = 0x06;
        Result r = play(p, &s, 200, 0, 8);
        CHECK(!r.rx_end, "1-of-4 invalid symbol accepted");
        free(s.s);
    }

    // 6. forced modes still decode their own coding
    iso15693_parser_force_1outof256(p);
    {
        Stream s = {0};
        encode_1of256(&s, rdm8, sizeof(rdm8));
        Result r = play(p, &s, 200, 0, 9);
        CHECK(r.rx_end && r.len == sizeof(rdm8) && !memcmp(r.data, rdm8, sizeof(rdm8)), "forced 1-of-256");
        free(s.s);
    }
    iso15693_parser_force_1outof4(p);
    {
        Stream s = {0};
        encode_1of4(&s, rdm8, sizeof(rdm8));
        Result r = play(p, &s, 200, 0, 10);
        CHECK(r.rx_end && r.len == sizeof(rdm8) && !memcmp(r.data, rdm8, sizeof(rdm8)), "forced 1-of-4");
        free(s.s);
    }
    iso15693_parser_detect_mode(p);

    iso15693_parser_free(p);
    printf("%s: %d/%d checks failed\n", fails ? "FAILED" : "PASSED", fails, checks);
    return fails ? 1 : 0;
}
