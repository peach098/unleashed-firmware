// FNFC-01.10: how much frame-length error does the PM3 reader decoder tolerate?
// The ISO15693 tag-answer decoder of armsrc/iso15693.c (pm3_decoder.inc, extracted verbatim by extract.sh)
// is fed with an idealised sample stream: one sample per 64 fc (as GetIso15693AnswerFromTag), amplitude =
// A x (fraction of the sample covered by subcarrier modulation) + gaussian noise. The 323-byte reply
// (00 + dump + CRC) is stretched by <ppm> and started at a random phase within one sample.
// Prints the fraction of correctly decoded frames per ppm.
// usage: sim <hex frame> <ppm from> <ppm to> <step> [trials] [noise sigma]
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RAMFUNC
#define LED_C_ON()
#define LED_C_OFF()
#include "pm3_decoder.inc"

#define MAXI 8192
static double ivs[MAXI][2];
static int niv;
static void mod(double* t, double len, bool on, double scale) {
    if(on) {
        if(niv && fabs(ivs[niv - 1][1] - *t) < 1e-9) ivs[niv - 1][1] = *t + len * scale; // merge
        else { ivs[niv][0] = *t; ivs[niv][1] = *t + len * scale; niv++; }
    }
    *t += len * scale;
}
static uint64_t rs = 88172645463325252ULL;
static double urand(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (rs >> 11) * (1.0 / 9007199254740992.0); }
static double grand(void) { double u = urand() + 1e-12, v = urand(); return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

int main(int argc, char** argv) {
    uint8_t frame[1100]; int n = 0; unsigned v; int k; const char* h = argv[1];
    while(sscanf(h, " %2x%n", &v, &k) == 1) frame[n++] = v, h += k;
    const double p0 = atof(argv[2]), p1 = atof(argv[3]), st = atof(argv[4]);
    const int trials = argc > 5 ? atoi(argv[5]) : 200;
    const double sigma = argc > 6 ? atof(argv[6]) : 20.0, A = 1000.0;
    for(double ppm = p0; ppm <= p1 + 1e-9; ppm += st) {
        int ok = 0;
        for(int tr = 0; tr < trials; tr++) {
            const double scale = 1.0 + ppm * 1e-6;
            double t = 3000.0 + urand() * 64.0;
            niv = 0;
            mod(&t, 768, true, scale); mod(&t, 256, false, scale); mod(&t, 256, true, scale); // SOF
            for(int i = 0; i < n; i++)
                for(int b = 0; b < 8; b++) {
                    const bool one = frame[i] >> b & 1;
                    mod(&t, 256, !one, scale); mod(&t, 256, one, scale);
                }
            mod(&t, 256, true, scale); mod(&t, 256, false, scale); mod(&t, 768, true, scale); // EOF
            const int last = (int)(t / 64) + 200;
            uint8_t out[1100]; DecodeTag_t dt = {0};
            dt.output = out; dt.max_len = sizeof(out) - 1; dt.posCount = 0; dt.state = STATE_TAG_SOF_LOW;
            dt.previous_amplitude = MAX_PREVIOUS_AMPLITUDE;
            int iv = 0; bool done = false;
            for(int s = 0; s < last && !done; s++) {
                const double a = s * 64.0, b = a + 64.0;
                double cov = 0;
                while(iv < niv && ivs[iv][1] <= a) iv++;
                for(int j = iv; j < niv && ivs[j][0] < b; j++) cov += fmin(b, ivs[j][1]) - fmax(a, ivs[j][0]);
                double amp = A * cov / 64.0 + sigma * grand();
                if(amp < 0) amp = 0;
                done = Handle15693SamplesFromTag((uint16_t)amp, &dt, true);
            }
            if(done && dt.len == n && memcmp(out, frame, n) == 0) ok++;
        }
        printf("%+7.1f ppm  drift %+6.2f us  decoded %3d/%d\n", ppm, ppm * 1e-6 * ((2560.0 + 4096.0 * n) / 13.56), ok, trials);
    }
    return 0;
}
