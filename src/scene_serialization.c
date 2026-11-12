#include "scene_serialization.h"

#include "helpers.h"
#include "teletype.h"
#include "util.h"

#define STATE_DESC 0
#define STATE_POUND 1
#define STATE_POUND_IGNORE 2
#define STATE_SCRIPT 3
#define STATE_PATTERNS 4
#define STATE_GRID 5
#define PATTERN_HEADER_ROWS 4
#define LEGACY_PATTERN_COUNT 4

uint8_t grid_state = 0;
uint16_t grid_count = 0;
uint8_t grid_num = 0;

// internal test functions to make sure serializer struct is filled out
bool check_serializer(tt_serializer_t* stream);
bool check_deserializer(tt_deserializer_t* stream);

// internal helper functions for grid data serialization
void serialize_grid(tt_serializer_t* stream, scene_state_t* scene);
void deserialize_grid(tt_deserializer_t* stream, scene_state_t* scene, char c);


static int serialized_pattern_count(scene_state_t* scene) {
    // Retain the legacy file format when the extra patterns are empty.
    for (int channel = LEGACY_PATTERN_COUNT; channel < PATTERN_COUNT;
         channel++) {
        if (ss_get_pattern_len(scene, channel) ||
            ss_get_pattern_wrap(scene, channel) != 1 ||
            ss_get_pattern_start(scene, channel) ||
            ss_get_pattern_end(scene, channel) != PATTERN_LENGTH - 1) {
            return PATTERN_COUNT;
        }
        for (int step = 0; step < PATTERN_LENGTH; step++) {
            if (ss_get_pattern_val(scene, channel, step)) return PATTERN_COUNT;
        }
    }
    return LEGACY_PATTERN_COUNT;
}

void serialize_scene(tt_serializer_t* stream, scene_state_t* scene,
                     char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    if (!check_serializer(stream)) { return; }

    char blank = 0;
    for (int l = 0; l < SCENE_TEXT_LINES; l++) {
        size_t line_length = strlen((*text)[l]);
        if (line_length > 0) {
            stream->write_buffer(stream->data, (uint8_t*)((*text)[l]),
                                 line_length);
            stream->write_char(stream->data, '\n');
            blank = 0;
        }
        else if (!blank) {
            stream->write_char(stream->data, '\n');
            blank = 1;
        }
    }

    char input[36];
    for (int s = 0; s < EDITABLE_SCRIPT_COUNT; s++) {
        stream->write_char(stream->data, '\n');
        stream->write_char(stream->data, '\n');
        stream->write_char(stream->data, '#');
        if (s == METRO_SCRIPT)
            stream->write_char(stream->data, 'M');
        else if (s == INIT_SCRIPT)
            stream->write_char(stream->data, 'I');
        else
            stream->write_char(stream->data, s + 49);

        for (int l = 0; l < ss_get_script_len(scene, s); l++) {
            stream->write_char(stream->data, '\n');
            print_command(ss_get_script_command(scene, s, l), input);
            stream->write_buffer(stream->data, (uint8_t*)input, strlen(input));
        }
    }

    stream->write_char(stream->data, '\n');
    stream->write_char(stream->data, '\n');
    stream->write_char(stream->data, '#');
    stream->write_char(stream->data, 'P');
    stream->write_char(stream->data, '\n');

    int pattern_count = serialized_pattern_count(scene);
    for (int row = 0; row < PATTERN_HEADER_ROWS + PATTERN_LENGTH; row++) {
        if (row == PATTERN_HEADER_ROWS) stream->write_char(stream->data, '\n');
        for (int b = 0; b < pattern_count; b++) {
            int16_t value;
            switch (row) {
                case 0: value = ss_get_pattern_len(scene, b); break;
                case 1: value = ss_get_pattern_wrap(scene, b); break;
                case 2: value = ss_get_pattern_start(scene, b); break;
                case 3: value = ss_get_pattern_end(scene, b); break;
                default:
                    value =
                        ss_get_pattern_val(scene, b, row - PATTERN_HEADER_ROWS);
                    break;
            }
            itoa(value, input, 10);
            stream->write_buffer(stream->data, (uint8_t*)input, strlen(input));
            stream->write_char(stream->data,
                               b == pattern_count - 1 ? '\n' : '\t');
        }
    }

    // serialize grid

    char fvalue[36];

    stream->write_char(stream->data, '\n');
    stream->write_char(stream->data, '#');
    stream->write_char(stream->data, 'G');
    stream->write_char(stream->data, '\n');
    for (uint16_t i = 0; i < GRID_BUTTON_COUNT; i++) {
        stream->write_char(stream->data, '0' + scene->grid.button[i].state);
        if ((i & 15) == 15) stream->write_char(stream->data, '\n');
    }
    stream->write_char(stream->data, '\n');
    for (uint16_t i = 0; i < GRID_FADER_COUNT; i++) {
        itoa(scene->grid.fader[i].value, fvalue, 10);
        stream->write_buffer(stream->data, (uint8_t*)fvalue, strlen(fvalue));
        stream->write_char(stream->data, (i & 15) == 15 ? '\n' : '\t');
    }
}

static void deserialize_pattern_cell(scene_state_t* scene, uint8_t channel,
                                     uint8_t row, int32_t value) {
    if (value > INT16_MAX) value = INT16_MAX;
    switch (row) {
        case 0:
            ss_set_pattern_len(scene, channel,
                               normalise_value(0, PATTERN_LENGTH, 0, value));
            break;
        case 1: ss_set_pattern_wrap(scene, channel, value > 0); break;
        case 2:
            ss_set_pattern_start(
                scene, channel,
                normalise_value(0, PATTERN_LENGTH - 1, 0, value));
            break;
        case 3:
            ss_set_pattern_end(
                scene, channel,
                normalise_value(0, PATTERN_LENGTH - 1, 0, value));
            break;
        default:
            ss_set_pattern_val(scene, channel, row - PATTERN_HEADER_ROWS,
                               value);
    }
}

void deserialize_scene(tt_deserializer_t* stream, scene_state_t* scene,
                       char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    if (!check_deserializer(stream)) { return; }

    char c = '\n';
    uint8_t prev_cr = 0;
    uint8_t new_line = 0;
    uint8_t l = 0;
    uint8_t p = 0;
    uint8_t s = STATE_DESC, s2 = STATE_DESC;
    uint8_t script = NO_SCRIPT;
    uint8_t column = 0;
    uint16_t magnitude = 0;
    bool negative = false;
    bool has_digits = false;

    char input[32];
    memset(input, 0, sizeof(input));

    while (!stream->eof(stream->data)) {
        new_line = c == '\n';
        uint16_t raw = stream->read_char(stream->data);
        if (raw > UINT8_MAX) break;
        c = toupper(raw);
        // stream->print_dbg_char(c);

        // deal with line endings
        // DOS: \r\n, *nix: \n, Mac: \r
        if (c == '\r') {
            c = '\n';
            prev_cr = 1;
        }
        else if (c == '\n' && prev_cr) {
            prev_cr = 0;
            continue;
        }
        else { prev_cr = 0; }

        if (c == '#' && new_line) {
            s = STATE_POUND;
            continue;
        }

        // SCENE TEXT
        if (s == STATE_DESC) {
            if (c == '\n') {
                l++;
                p = 0;
            }
            else {
                if (l < SCENE_TEXT_LINES && p < SCENE_TEXT_CHARS) {
                    (*text)[l][p] = c;
                    p++;
                }
            }
            continue;
        }

        if (s == STATE_POUND) {
            if (c == 'M') {
                script = METRO_SCRIPT;
                s2 = STATE_SCRIPT;
            }
            else if (c == 'I') {
                script = INIT_SCRIPT;
                s2 = STATE_SCRIPT;
            }
            else if (c == 'P') {
                ss_patterns_init(scene);
                column = 0;
                magnitude = 0;
                negative = has_digits = false;
                s2 = STATE_PATTERNS;
            }
            else if (c == 'G') {
                grid_state = grid_count = 0;
                s2 = STATE_GRID;
            }
            else {
                script = c - 49;
                if (script < 0 || script >= EDITABLE_SCRIPT_COUNT) {
                    script = NO_SCRIPT;
                }
                s2 = STATE_SCRIPT;
            }

            l = 0;
            p = 0;
            s = STATE_POUND_IGNORE;
            continue;
        }

        if (s == STATE_POUND_IGNORE) {
            if (c == '\n') { s = s2; }
            continue;
        }

        if (s == STATE_SCRIPT) {
            if (script == NO_SCRIPT || script < 0 ||
                script >= EDITABLE_SCRIPT_COUNT)
                continue;

            if (c != '\n') {
                if (p < 32) {
                    input[p] = c;
                    p++;
                }
            }
            else {
                if (p && l < SCRIPT_MAX_COMMANDS) {
                    tele_command_t temp;
                    temp.comment = false;
                    error_t status;
                    char error_msg[TELE_ERROR_MSG_LENGTH];
                    status = parse(input, &temp, error_msg);

                    if (status == E_OK) {
                        status = validate(&temp, error_msg);

                        if (status == E_OK) {
                            ss_overwrite_script_command(scene, script, l,
                                                        &temp);
                            l++;
                        }
                        else {
                            stream->print_dbg("\r\nvalidate: ");
                            stream->print_dbg(tele_error(status));
                            stream->print_dbg(" >> ");
                            stream->print_dbg("\r\nINPUT: ");
                            stream->print_dbg(input);
                        }
                    }
                    else {
                        stream->print_dbg("\r\nERROR: ");
                        stream->print_dbg(tele_error(status));
                        stream->print_dbg(" >> ");
                        stream->print_dbg("\r\nINPUT: ");
                        stream->print_dbg(input);
                    }

                    memset(input, 0, sizeof(input));
                    p = 0;
                }
            }
            continue;
        }

        if (s == STATE_PATTERNS) {
            if (l >= PATTERN_HEADER_ROWS + PATTERN_LENGTH) continue;

            if (c == '\n' || c == '\t') {
                if (has_digits && column < PATTERN_COUNT) {
                    int32_t value = negative ? -(int32_t)magnitude : magnitude;
                    deserialize_pattern_cell(scene, column, l, value);
                }
                if (c == '\n') {
                    if (has_digits || column > 0) l++;
                    column = 0;
                }
                else if (column < PATTERN_COUNT) { column++; }
                magnitude = 0;
                negative = has_digits = false;
            }
            else if (c == '-' && !has_digits) { negative = true; }
            else if (c >= '0' && c <= '9') {
                uint32_t next = (uint32_t)magnitude * 10 + c - '0';
                magnitude = next > 32768 ? 32768 : next;
                has_digits = true;
            }
            continue;
        }

        if (s == STATE_GRID) {
            if (grid_state == 0) {
                if (c >= '0' && c <= '9') {
                    scene->grid.button[grid_count].state = c != '0';
                    if (++grid_count >= GRID_BUTTON_COUNT) {
                        grid_count = 0;
                        grid_state = 1;
                    }
                }
            }
            else if (grid_state == 1) {
                if (c >= '0' && c <= '9') {
                    grid_num = c - '0';
                    grid_state = 2;
                }
            }
            else if (grid_state == 2) {
                if (c >= '0' && c <= '9') {
                    grid_num = grid_num * 10 + c - '0';
                }
                else if (c == '\t' || c == '\n') {
                    if (grid_count < GRID_FADER_COUNT) {
                        scene->grid.fader[grid_count].value = grid_num;
                        grid_num = 0;
                        grid_count++;
                    }
                }
            }
            continue;
        }
    }
    if (s == STATE_PATTERNS && l < PATTERN_HEADER_ROWS + PATTERN_LENGTH &&
        column < PATTERN_COUNT && has_digits) {
        int32_t value = negative ? -(int32_t)magnitude : magnitude;
        deserialize_pattern_cell(scene, column, l, value);
    }
}

bool check_serializer(tt_serializer_t* stream) {
    return (stream && stream->write_char && stream->write_buffer &&
            stream->print_dbg);
}

bool check_deserializer(tt_deserializer_t* stream) {
    return (stream && stream->read_char && stream->eof && stream->print_dbg);
}
