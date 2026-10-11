#define NOISE_THRESHOLD          80                   // don't try to correlate noise
#define MAX_PREVIOUS_AMPLITUDE   (-1 - NOISE_THRESHOLD)

typedef struct {
    enum {
        STATE_TAG_SOF_LOW,
        STATE_TAG_SOF_RISING_EDGE,
        STATE_TAG_SOF_HIGH,
        STATE_TAG_SOF_HIGH_END,
        STATE_TAG_RECEIVING_DATA,
        STATE_TAG_EOF,
        STATE_TAG_EOF_TAIL
    } state;
    int bitCount;
    int posCount;
    enum {
        LOGIC0,
        LOGIC1,
        SOF_PART1,
        SOF_PART2
    } lastBit;
    uint16_t shiftReg;
    uint16_t max_len;
    uint16_t len;
    int sum1;
    int sum2;
    int threshold_sof;
    int threshold_half;
    uint16_t previous_amplitude;
    bool signal_activity;  // set when signal detected but decode failed (collision hint)
    uint8_t *output;
} DecodeTag_t;

//-----------------------------------------------------------------------------
// DEMODULATE tag answer
//-----------------------------------------------------------------------------
static RAMFUNC int Handle15693SamplesFromTag(uint16_t amplitude, DecodeTag_t *tag, bool recv_speed) {

    switch (tag->state) {

        case STATE_TAG_SOF_LOW: {
            // waiting for a rising edge
            if (amplitude > NOISE_THRESHOLD + tag->previous_amplitude) {
                if (tag->posCount > 10) {
                    tag->threshold_sof = amplitude - tag->previous_amplitude; // to be divided by 2
                    tag->threshold_half = 0;
                    tag->state = STATE_TAG_SOF_RISING_EDGE;
                } else {
                    tag->posCount = 0;
                }
            } else {
                tag->posCount++;
                tag->previous_amplitude = amplitude;
            }
            break;
        }

        case STATE_TAG_SOF_RISING_EDGE: {
            if (amplitude > tag->threshold_sof + tag->previous_amplitude) { // edge still rising
                if (amplitude > tag->threshold_sof + tag->threshold_sof) { // steeper edge, take this as time reference
                    tag->posCount = 1;
                } else {
                    tag->posCount = 2;
                }
                tag->threshold_sof = (amplitude - tag->previous_amplitude) / 2;
            } else {
                tag->posCount = 2;
                tag->threshold_sof = tag->threshold_sof / 2;
            }
            tag->state = STATE_TAG_SOF_HIGH;
            break;
        }

        case STATE_TAG_SOF_HIGH: {
            // waiting for 10 times high. Take average over the last 8
            if (amplitude > tag->threshold_sof) {
                tag->posCount++;
                if (tag->posCount > 2) {
                    tag->threshold_half += amplitude; // keep track of average high value
                }
                if (tag->posCount == (recv_speed ? 10 : 40)) {
                    tag->threshold_half >>= 2; // (4 times 1/2 average)
                    tag->state = STATE_TAG_SOF_HIGH_END;
                }
            } else { // high phase was too short
                tag->signal_activity = true; // signal detected but SOF failed (possible collision)
                tag->posCount = 1;
                tag->previous_amplitude = amplitude;
                tag->state = STATE_TAG_SOF_LOW;
            }
            break;
        }

        case STATE_TAG_SOF_HIGH_END: {
            // check for falling edge
            if (tag->posCount == (recv_speed ? 13 : 52) && amplitude < tag->threshold_sof) {
                tag->lastBit = SOF_PART1;  // detected 1st part of SOF (12 samples low and 12 samples high)
                tag->shiftReg = 0;
                tag->bitCount = 0;
                tag->len = 0;
                tag->sum1 = amplitude;
                tag->sum2 = 0;
                tag->posCount = 2;
                tag->state = STATE_TAG_RECEIVING_DATA;
                LED_C_ON();
            } else {
                tag->posCount++;
                if (tag->posCount > (recv_speed ? 13 : 52)) { // high phase too long
                    tag->signal_activity = true; // signal detected but SOF failed (possible collision)
                    tag->posCount = 0;
                    tag->previous_amplitude = amplitude;
                    tag->state = STATE_TAG_SOF_LOW;
                    LED_C_OFF();
                }
            }
            break;
        }

        case STATE_TAG_RECEIVING_DATA: {
            if (tag->posCount == 1) {
                tag->sum1 = 0;
                tag->sum2 = 0;
            }

            if (tag->posCount <= (recv_speed ? 4 : 16)) {
                tag->sum1 += amplitude;
            } else {
                tag->sum2 += amplitude;
            }

            if (tag->posCount == (recv_speed ? 8 : 32)) {
                if (tag->sum1 > tag->threshold_half && tag->sum2 > tag->threshold_half) { // modulation in both halves
                    if (tag->lastBit == LOGIC0) {  // this was already part of EOF
                        tag->state = STATE_TAG_EOF;
                    } else {
                        tag->signal_activity = true; // collision: modulation in both halves during data
                        tag->posCount = 0;
                        tag->previous_amplitude = amplitude;
                        tag->state = STATE_TAG_SOF_LOW;
                        LED_C_OFF();
                    }
                } else if (tag->sum1 < tag->threshold_half && tag->sum2 > tag->threshold_half) { // modulation in second half
                    // logic 1
                    if (tag->lastBit == SOF_PART1) { // still part of SOF
                        tag->lastBit = SOF_PART2;    // SOF completed
                    } else {
                        tag->lastBit = LOGIC1;
                        tag->shiftReg >>= 1;
                        tag->shiftReg |= 0x80;
                        tag->bitCount++;
                        if (tag->bitCount == 8) {
                            tag->output[tag->len] = tag->shiftReg & 0xFF;
                            tag->len++;

                            if (tag->len > tag->max_len) {
                                // buffer overflow, give up
                                LED_C_OFF();
                                return true;
                            }
                            tag->bitCount = 0;
                            tag->shiftReg = 0;
                        }
                    }
                } else if (tag->sum1 > tag->threshold_half && tag->sum2 < tag->threshold_half) { // modulation in first half
                    // logic 0
                    if (tag->lastBit == SOF_PART1) { // incomplete SOF
                        tag->posCount = 0;
                        tag->previous_amplitude = amplitude;
                        tag->state = STATE_TAG_SOF_LOW;
                        LED_C_OFF();
                    } else {
                        tag->lastBit = LOGIC0;
                        tag->shiftReg >>= 1;
                        tag->bitCount++;

                        if (tag->bitCount == 8) {
                            tag->output[tag->len] = (tag->shiftReg & 0xFF);
                            tag->len++;

                            if (tag->len > tag->max_len) {
                                // buffer overflow, give up
                                tag->posCount = 0;
                                tag->previous_amplitude = amplitude;
                                tag->state = STATE_TAG_SOF_LOW;
                                LED_C_OFF();
                            }
                            tag->bitCount = 0;
                            tag->shiftReg = 0;
                        }
                    }
                } else { // no modulation
                    if (tag->lastBit == SOF_PART2) { // only SOF (this is OK for iClass)
                        LED_C_OFF();
                        return true;
                    } else {
                        tag->posCount = 0;
                        tag->state = STATE_TAG_SOF_LOW;
                        LED_C_OFF();
                    }
                }
                tag->posCount = 0;
            }
            tag->posCount++;
            break;
        }

        case STATE_TAG_EOF: {
            if (tag->posCount == 1) {
                tag->sum1 = 0;
                tag->sum2 = 0;
            }

            if (tag->posCount <= (recv_speed ? 4 : 16)) {
                tag->sum1 += amplitude;
            } else {
                tag->sum2 += amplitude;
            }

            if (tag->posCount == (recv_speed ? 8 : 32)) {
                if (tag->sum1 > tag->threshold_half && tag->sum2 < tag->threshold_half) { // modulation in first half
                    tag->posCount = 0;
                    tag->state = STATE_TAG_EOF_TAIL;
                } else {
                    tag->posCount = 0;
                    tag->previous_amplitude = amplitude;
                    tag->state = STATE_TAG_SOF_LOW;
                    LED_C_OFF();
                }
            }
            tag->posCount++;
            break;
        }

        case STATE_TAG_EOF_TAIL: {
            if (tag->posCount == 1) {
                tag->sum1 = 0;
                tag->sum2 = 0;
            }

            if (tag->posCount <= (recv_speed ? 4 : 16)) {
                tag->sum1 += amplitude;
            } else {
                tag->sum2 += amplitude;
            }

            if (tag->posCount == (recv_speed ? 8 : 32)) {
                if (tag->sum1 < tag->threshold_half && tag->sum2 < tag->threshold_half) { // no modulation in both halves
                    LED_C_OFF();
                    return true;
                } else {
                    tag->posCount = 0;
                    tag->previous_amplitude = amplitude;
                    tag->state = STATE_TAG_SOF_LOW;
                    LED_C_OFF();
                }
            }
            tag->posCount++;
            break;
        }
    }

    return false;
}

