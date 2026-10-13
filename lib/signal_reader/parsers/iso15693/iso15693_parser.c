#include "iso15693_parser.h"

#include <toolbox/bit_buffer.h>

#include <furi/furi.h>

#define ISO15693_PARSER_SIGNAL_READER_BUFF_SIZE (2)
// The signal reader ISR appends one 1-of-4 symbol (8 samples, ~75.5 us) per event and wakes the
// worker thread every ISO15693_PARSER_BITSTREAM_CHUNK symbols. The buffer holds twice that, so
// symbols that arrive while the thread is still consuming a chunk are kept instead of being lost
// or written out of bounds.
#define ISO15693_PARSER_BITSTREAM_CHUNK         (32)
#define ISO15693_PARSER_BITSTREAM_BUFF_SIZE     (2 * ISO15693_PARSER_BITSTREAM_CHUNK)
#define ISO15693_PARSER_BITRATE_F64MHZ          (603U)
// 1-of-256: a data byte is 256 slots of 2 half slots, one sample per half slot, i.e. 64 sample bytes
#define ISO15693_PARSER_1OF256_BYTE_SYMBOLS     (64U)

#define TAG "Iso15693Parser"

typedef enum {
    Iso15693ParserStateParseSoF,
    Iso15693ParserStateParseFrame,
    Iso15693ParserStateFail,
} Iso15693ParserState;

typedef enum {
    Iso15693ParserMode1OutOf4,
    Iso15693ParserMode1OutOf256,

    Iso15693ParserModeNum,
} Iso15693ParserMode;

struct Iso15693Parser {
    Iso15693ParserState state;
    Iso15693ParserMode mode;
    bool detect_mode;

    SignalReader* signal_reader;

    uint8_t bitstream_buff[ISO15693_PARSER_BITSTREAM_BUFF_SIZE];
    size_t bitstream_idx;
    uint8_t last_byte;

    bool signal_detected;
    bool bit_offset_calculated;
    uint8_t bit_offset;
    volatile size_t bytes_to_process; // written by the ISR, consumed by the thread
    size_t symbols_received; // data symbols (1-of-256: sample bytes) since SOF (ISR)

    uint8_t next_byte;
    uint16_t next_byte_part;
    bool pause_found; // 1-of-256: the pause of the current byte has been seen

    BitBuffer* parsed_frame;
    volatile bool eof_received; // set by the ISR
    bool frame_parsed;

    Iso15693ParserCallback callback;
    void* context;
};

typedef enum {
    Iso15693ParserCommandProcessed,
    Iso15693ParserCommandWaitData,
    Iso15693ParserCommandFail,
    Iso15693ParserCommandSuccess,
} Iso15693ParserCommand;

typedef Iso15693ParserCommand (*Iso15693ParserStateHandler)(Iso15693Parser* instance);

// 1-of-256 sample byte (4 slots, bit = sample, LSB first) holding the pause of the byte: one pause
// in the second half of a slot, the same patterns as the 1-of-4 symbols
static const uint8_t iso15693_parser_1_out_of_256_pause_patterns[] = {0x02, 0x08, 0x20, 0x80};

Iso15693Parser* iso15693_parser_alloc(const GpioPin* pin, size_t max_frame_size) {
    Iso15693Parser* instance = malloc(sizeof(Iso15693Parser));
    instance->detect_mode = true;
    instance->parsed_frame = bit_buffer_alloc(max_frame_size);

    instance->signal_reader = signal_reader_alloc(pin, ISO15693_PARSER_SIGNAL_READER_BUFF_SIZE);
    signal_reader_set_sample_rate(
        instance->signal_reader, SignalReaderTimeUnit64Mhz, ISO15693_PARSER_BITRATE_F64MHZ);
    signal_reader_set_pull(instance->signal_reader, GpioPullDown);
    signal_reader_set_polarity(instance->signal_reader, SignalReaderPolarityNormal);
    signal_reader_set_trigger(instance->signal_reader, SignalReaderTriggerRisingFallingEdge);

    return instance;
}

void iso15693_parser_free(Iso15693Parser* instance) {
    furi_assert(instance);

    bit_buffer_free(instance->parsed_frame);
    signal_reader_free(instance->signal_reader);
    free(instance);
}

void iso15693_parser_reset(Iso15693Parser* instance) {
    furi_assert(instance);

    instance->state = Iso15693ParserStateParseSoF;
    if(instance->detect_mode) instance->mode = Iso15693ParserMode1OutOf4;
    memset(instance->bitstream_buff, 0x00, sizeof(instance->bitstream_buff));
    instance->bitstream_idx = 0;

    instance->next_byte = 0;
    instance->next_byte_part = 0;

    instance->bit_offset = 0;
    instance->bytes_to_process = 0;
    instance->symbols_received = 0;
    instance->signal_detected = false;
    instance->bit_offset_calculated = false;

    instance->last_byte = 0x00;
    instance->pause_found = false;
    instance->eof_received = false;

    bit_buffer_reset(instance->parsed_frame);
    instance->frame_parsed = false;
}

// ISR context
static void iso15693_parser_push_symbol(Iso15693Parser* instance, uint8_t symbol) {
    const size_t n = instance->bytes_to_process;
    if(n < ISO15693_PARSER_BITSTREAM_BUFF_SIZE) {
        instance->bitstream_buff[n] = symbol;
        instance->bytes_to_process = n + 1;
        instance->symbols_received++;
        if(n + 1 == ISO15693_PARSER_BITSTREAM_CHUNK) {
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        }
    } else {
        // The thread is a whole chunk behind: drop the frame, the thread restarts the parser
        instance->state = Iso15693ParserStateFail;
        instance->callback(Iso15693ParserEventDataReceived, instance->context);
    }
}

// Thread context: drop the first `count` symbols, keep the ones the ISR appended meanwhile
static void iso15693_parser_consume(Iso15693Parser* instance, size_t count) {
    FURI_CRITICAL_ENTER();
    const size_t rest = instance->bytes_to_process - count;
    if(rest) memmove(instance->bitstream_buff, &instance->bitstream_buff[count], rest);
    instance->bytes_to_process = rest;
    FURI_CRITICAL_EXIT();
}

static void signal_reader_callback(SignalReaderEvent event, void* context) {
    furi_assert(context);
    furi_assert(event.data->data);
    furi_assert(event.data->len == ISO15693_PARSER_SIGNAL_READER_BUFF_SIZE / 2);

    Iso15693Parser* instance = context;
    furi_assert(instance->callback);

    const uint8_t sof_1_out_of_4 = 0x21;
    const uint8_t sof_1_out_of_256 = 0x81;
    const uint8_t eof_single = 0x01;
    const uint8_t eof = 0x04;
    const uint8_t no_pause = 0x00;

    // The frame has ended: the reader may keep sampling until the thread stops it,
    // ignore the idle carrier
    if(instance->eof_received) return;

    if(instance->state == Iso15693ParserStateParseSoF) {
        if(event.data->data[0] == sof_1_out_of_4) {
            if(instance->detect_mode) instance->mode = Iso15693ParserMode1OutOf4;
            instance->state = Iso15693ParserStateParseFrame;
        } else if(event.data->data[0] == sof_1_out_of_256) {
            if(instance->detect_mode) instance->mode = Iso15693ParserMode1OutOf256;
            instance->state = Iso15693ParserStateParseFrame;
        } else if(event.data->data[0] == eof_single) {
            instance->eof_received = true;
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        } else {
            instance->state = Iso15693ParserStateFail;
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        }
    } else {
        const bool frame_1_out_of_4 = (instance->state == Iso15693ParserStateParseFrame) &&
                                      (instance->mode == Iso15693ParserMode1OutOf4);
        const bool frame_1_out_of_256 = (instance->state == Iso15693ParserStateParseFrame) &&
                                        (instance->mode == Iso15693ParserMode1OutOf256);
        if(frame_1_out_of_4 && (event.data->data[0] == eof)) {
            instance->eof_received = true;
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        } else if(
            frame_1_out_of_4 && (event.data->data[0] == no_pause) &&
            (instance->symbols_received > 0) && (instance->symbols_received % 4 == 0)) {
            // Every 1-of-4 symbol has exactly one pause, so a symbol slot without one after a
            // whole byte means the reader has stopped modulating: its EOF pause was not sampled.
            // End the frame here; the CRC check of the frame still applies.
            instance->eof_received = true;
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        } else if(
            frame_1_out_of_256 && (event.data->data[0] == eof) &&
            (instance->symbols_received % ISO15693_PARSER_1OF256_BYTE_SYMBOLS == 0)) {
            // 1-of-256 EOF can only follow a whole byte (a pause in the first half of a slot is
            // invalid data). End the frame here: the thread is only woken every
            // ISO15693_PARSER_BITSTREAM_CHUNK sample bytes (2.4 ms in 1-of-256), far beyond t1.
            instance->eof_received = true;
            instance->callback(Iso15693ParserEventDataReceived, instance->context);
        } else {
            iso15693_parser_push_symbol(instance, event.data->data[0]);
        }
    }
}

static void iso15693_parser_start_signal_reader(Iso15693Parser* instance) {
    iso15693_parser_reset(instance);
    signal_reader_start(instance->signal_reader, signal_reader_callback, instance);
}

void iso15693_parser_start(
    Iso15693Parser* instance,
    Iso15693ParserCallback callback,
    void* context) {
    furi_assert(instance);
    furi_assert(callback);

    instance->callback = callback;
    instance->context = context;
    iso15693_parser_start_signal_reader(instance);
}

void iso15693_parser_stop(Iso15693Parser* instance) {
    furi_assert(instance);

    signal_reader_stop(instance->signal_reader);
}

static Iso15693ParserCommand iso15693_parser_parse_1_out_of_4(Iso15693Parser* instance) {
    Iso15693ParserCommand command = Iso15693ParserCommandWaitData;
    const uint8_t bit_patterns_1_out_of_4[] = {0x02, 0x08, 0x20, 0x80};
    // EOF first: once it is seen, the ISR has already appended every symbol of the frame
    const bool eof_received = instance->eof_received;
    const size_t count = instance->bytes_to_process;

    for(size_t i = 0; i < count; i++) {
        // Check next pattern
        size_t j = 0;
        for(j = 0; j < COUNT_OF(bit_patterns_1_out_of_4); j++) {
            if(instance->bitstream_buff[i] == bit_patterns_1_out_of_4[j]) {
                instance->next_byte |= j << (instance->next_byte_part * 2);
                instance->next_byte_part++;
                if(instance->next_byte_part == 4) {
                    instance->next_byte_part = 0;
                    // Overlong frame (noise): drop it instead of overflowing parsed_frame
                    if(bit_buffer_get_size_bytes(instance->parsed_frame) >=
                       bit_buffer_get_capacity_bytes(instance->parsed_frame))
                        return Iso15693ParserCommandFail;
                    bit_buffer_append_byte(instance->parsed_frame, instance->next_byte);
                    instance->next_byte = 0;
                }
                break;
            }
        }
        if(j == COUNT_OF(bit_patterns_1_out_of_4)) {
            command = Iso15693ParserCommandFail;
            break;
        }
    }

    if(command != Iso15693ParserCommandFail) {
        if(eof_received) {
            command = Iso15693ParserCommandSuccess;
            instance->frame_parsed = true;
        }
    }

    // 1-of-4: a finished or failed frame is dropped by the parser restart, only an incomplete one
    // hands the buffer back (keeps the critical section off the request EOF -> reply path)
    if(command == Iso15693ParserCommandWaitData) iso15693_parser_consume(instance, count);

    return command;
}

// 1-of-256: a byte is 64 sample bytes with exactly one pause, in the second half of slot `value`.
// Each sample byte is either empty or one 1-of-4 pattern; anything else, a second pause or a byte
// without a pause fails the frame. EOF is detected by the ISR on a byte boundary.
static Iso15693ParserCommand iso15693_parser_parse_1_out_of_256(Iso15693Parser* instance) {
    Iso15693ParserCommand command = Iso15693ParserCommandWaitData;
    // EOF first: once it is seen, the ISR has already appended every symbol of the frame
    const bool eof_received = instance->eof_received;
    const size_t count = instance->bytes_to_process;

    for(size_t i = 0; i < count; i++) {
        const uint8_t symbol = instance->bitstream_buff[i];
        if(symbol != 0x00) {
            size_t j = 0;
            for(j = 0; j < COUNT_OF(iso15693_parser_1_out_of_256_pause_patterns); j++) {
                if(symbol == iso15693_parser_1_out_of_256_pause_patterns[j]) break;
            }
            if(instance->pause_found ||
               (j == COUNT_OF(iso15693_parser_1_out_of_256_pause_patterns))) {
                command = Iso15693ParserCommandFail;
                break;
            }
            instance->next_byte = instance->next_byte_part * 4 + j;
            instance->pause_found = true;
        }
        instance->next_byte_part++;
        if(instance->next_byte_part == ISO15693_PARSER_1OF256_BYTE_SYMBOLS) {
            // Byte without a pause, or overlong frame (noise): drop it
            if(!instance->pause_found || (bit_buffer_get_size_bytes(instance->parsed_frame) >=
                                          bit_buffer_get_capacity_bytes(instance->parsed_frame))) {
                command = Iso15693ParserCommandFail;
                break;
            }
            bit_buffer_append_byte(instance->parsed_frame, instance->next_byte);
            instance->next_byte = 0;
            instance->next_byte_part = 0;
            instance->pause_found = false;
        }
    }

    if((command != Iso15693ParserCommandFail) && eof_received) {
        command = Iso15693ParserCommandSuccess;
        instance->frame_parsed = true;
    }

    // As in 1-of-4, only an incomplete frame hands the buffer back
    if(command == Iso15693ParserCommandWaitData) iso15693_parser_consume(instance, count);

    return command;
}

static const Iso15693ParserStateHandler iso15693_parser_state_handlers[Iso15693ParserModeNum] = {
    [Iso15693ParserMode1OutOf4] = iso15693_parser_parse_1_out_of_4,
    [Iso15693ParserMode1OutOf256] = iso15693_parser_parse_1_out_of_256,
};

bool iso15693_parser_run(Iso15693Parser* instance) {
    if(instance->state == Iso15693ParserStateFail) {
        iso15693_parser_stop(instance);
        iso15693_parser_start_signal_reader(instance);
    } else if((instance->state == Iso15693ParserStateParseSoF) && (instance->eof_received)) {
        instance->frame_parsed = true;
    } else if(instance->bytes_to_process || instance->eof_received) {
        Iso15693ParserCommand command = Iso15693ParserCommandProcessed;
        while(command == Iso15693ParserCommandProcessed) {
            command = iso15693_parser_state_handlers[instance->mode](instance);
        }

        if(command == Iso15693ParserCommandFail) {
            iso15693_parser_stop(instance);
            iso15693_parser_start_signal_reader(instance);
            FURI_LOG_D(TAG, "Frame parse failed");
        }
    }

    return instance->frame_parsed;
}

size_t iso15693_parser_get_data_size_bytes(Iso15693Parser* instance) {
    furi_assert(instance);

    return bit_buffer_get_size_bytes(instance->parsed_frame);
}

void iso15693_parser_get_data(
    Iso15693Parser* instance,
    uint8_t* buff,
    size_t buff_size,
    size_t* data_bits) {
    furi_assert(instance);
    furi_assert(buff);
    furi_assert(data_bits);

    bit_buffer_write_bytes(instance->parsed_frame, buff, buff_size);
    *data_bits = bit_buffer_get_size(instance->parsed_frame);
}

void iso15693_parser_detect_mode(Iso15693Parser* instance) {
    furi_assert(instance);

    instance->detect_mode = true;
}

void iso15693_parser_force_1outof4(Iso15693Parser* instance) {
    furi_assert(instance);

    instance->detect_mode = false;
    instance->mode = Iso15693ParserMode1OutOf4;
}

bool iso15693_parser_is_1outof256(Iso15693Parser* instance) {
    furi_assert(instance);

    return instance->mode == Iso15693ParserMode1OutOf256;
}

void iso15693_parser_force_1outof256(Iso15693Parser* instance) {
    furi_assert(instance);

    instance->detect_mode = false;
    instance->mode = Iso15693ParserMode1OutOf256;
}
