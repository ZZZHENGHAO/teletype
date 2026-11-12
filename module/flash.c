#include "flash.h"

#include <string.h>

// asf
#include "flashc.h"
#include "gpio.h"
#include "init_teletype.h"
#include "print_funcs.h"

// this
#include "teletype.h"

#define NVRAM_FORMAT 0x54543801UL

static grid_data_t grid_data;

#if defined(__AVR32__)
typedef char nvram_fits_reserved_flash
    [(sizeof(nvram_data_t) <= FLASH_NVRAM_KIB * 1024UL) ? 1 : -1];
static __attribute__((__section__(".flash_nvram"))) nvram_data_t f;
#else
nvram_data_t f;
#endif

static void write_scene(uint8_t preset_no, scene_state_t *scene,
                        char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]);
static void pack_grid(scene_state_t *scene);
static void unpack_grid(scene_state_t *scene);

u8 is_flash_fresh() {
    return f.format != NVRAM_FORMAT;
}

void flash_prepare() {
    // if it's not empty return
    if (is_flash_fresh()) {
        int confirm = 1;
        uint32_t counter = 0;
        int toggle = 0;
#define TIMEOUT 100000
        while (confirm == 1 && (++counter < TIMEOUT)) {
            confirm = gpio_get_pin_value(NMI);
            if ((counter % 1000) == 0) {
                if (++toggle % 2)
                    gpio_set_pin_low(B11);
                else
                    gpio_set_pin_high(B11);
            }
            print_dbg_ulong(confirm);
        }
        gpio_set_pin_low(B11);
        if (counter >= TIMEOUT) return;

        print_dbg("\r\n:::: first run, clearing flash");
        print_dbg("\r\nflash size: ");
        print_dbg_ulong(sizeof(f));

        // initialize_module has already initialized the working scene.
        for (uint8_t i = 0; i < SCENE_SLOTS; i++) {
            write_scene(i, &scene_state, &scene_text);
        }

        cal_data_t blank_cal_data;
        init_cal_data(&blank_cal_data);
        flashc_memcpy((void *)&f.cal, &blank_cal_data, sizeof(blank_cal_data),
                      true);
        device_config_t device_config = { .flip = 0 };
        flashc_memcpy((void *)&f.device_config, &device_config,
                      sizeof(device_config), true);
        flashc_memset8((void *)&f.last_scene, 0, 1, true);
        flashc_memset8((void *)&f.last_mode, M_LIVE, sizeof(f.last_mode), true);
        const uint32_t format = NVRAM_FORMAT;
        flashc_memcpy((void *)&f.format, &format, sizeof(format), true);
    }
}

void flash_write(uint8_t preset_no, scene_state_t *scene,
                 char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    if (preset_no >= SCENE_SLOTS || is_flash_fresh()) return;
    write_scene(preset_no, scene, text);
}

static void write_scripts(nvram_scene_t *slot, scene_state_t *scene) {
    nvram_script_t stored[EDITABLE_SCRIPT_COUNT] = { 0 };
    for (size_t i = 0; i < EDITABLE_SCRIPT_COUNT; i++) {
        const scene_script_t *script = &scene->scripts[i];
        nvram_script_t *packed_script = &stored[i];
        packed_script->length = script->l;
        memcpy(packed_script->every, script->every,
               sizeof(packed_script->every));
        for (size_t j = 0; j < packed_script->length; j++) {
            const tele_command_t *command = &script->c[j];
            nvram_command_t *packed = &packed_script->commands[j];
            packed->length = command->length;
            packed->separator = command->separator;
            packed->comment = command->comment;
            for (size_t word = 0; word < packed->length; word++) {
                packed->tags[word] = command->data[word].tag;
                packed->values[word] = command->data[word].value;
            }
        }
    }
    flashc_memcpy((void *)slot->scripts, stored, sizeof(stored), true);
}

static void read_scripts(const nvram_scene_t *slot, scene_state_t *scene) {
    for (size_t i = 0; i < EDITABLE_SCRIPT_COUNT; i++) {
        const nvram_script_t *stored = &slot->scripts[i];
        scene_script_t *script = &scene->scripts[i];
        memset(script, 0, sizeof(*script));
        script->l = stored->length <= SCRIPT_MAX_COMMANDS ? stored->length : 0;
        memcpy(script->every, stored->every, sizeof(script->every));
        for (size_t j = 0; j < script->l; j++) {
            const nvram_command_t *packed = &stored->commands[j];
            tele_command_t *command = &script->c[j];
            if (packed->length > COMMAND_MAX_LENGTH) continue;
            command->length = packed->length;
            command->separator = packed->separator;
            command->comment = packed->comment;
            for (size_t word = 0; word < command->length; word++) {
                command->data[word].tag = packed->tags[word];
                command->data[word].value = packed->values[word];
            }
        }
    }
}

static void write_scene(uint8_t preset_no, scene_state_t *scene,
                        char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS]) {
    write_scripts(&f.scenes[preset_no], scene);
    flashc_memcpy((void *)&f.scenes[preset_no].patterns, ss_patterns_ptr(scene),
                  ss_patterns_size(), true);
    pack_grid(scene);
    flashc_memcpy((void *)&f.scenes[preset_no].grid_data, &grid_data,
                  sizeof(grid_data_t), true);
    flashc_memcpy((void *)&f.scenes[preset_no].text, text,
                  SCENE_TEXT_LINES * SCENE_TEXT_CHARS, true);
}

void flash_read(uint8_t preset_no, scene_state_t *scene,
                char (*text)[SCENE_TEXT_LINES][SCENE_TEXT_CHARS],
                uint8_t init_pattern, uint8_t init_grid,
                uint8_t init_i2c_op_address) {
    if (preset_no >= SCENE_SLOTS || is_flash_fresh()) return;
    read_scripts(&f.scenes[preset_no], scene);
    if (init_pattern) {
        memcpy(ss_patterns_ptr(scene), &f.scenes[preset_no].patterns,
               ss_patterns_size());
    }
    if (init_grid) {
        memcpy(&grid_data, &f.scenes[preset_no].grid_data, sizeof(grid_data_t));
        unpack_grid(scene);
    }
    memcpy(text, &f.scenes[preset_no].text,
           SCENE_TEXT_LINES * SCENE_TEXT_CHARS);
    // need to reset timestamps
    uint32_t ticks = get_ticks();
    for (size_t i = 0; i < TOTAL_SCRIPT_COUNT; i++)
        scene->scripts[i].last_time = ticks;
    scene->variables.time = 0;

    if (init_i2c_op_address) scene->i2c_op_address = -1;
    ss_midi_init(scene);
}

uint8_t flash_last_saved_scene() {
    return is_flash_fresh() || f.last_scene >= SCENE_SLOTS ? 0 : f.last_scene;
}

void flash_update_last_saved_scene(uint8_t preset_no) {
    if (preset_no >= SCENE_SLOTS || is_flash_fresh()) return;
    flashc_memset8((void *)&f.last_scene, preset_no, 1, true);
}

const char *flash_scene_text(uint8_t preset_no, size_t line) {
    if (is_flash_fresh() || preset_no >= SCENE_SLOTS ||
        line >= SCENE_TEXT_LINES)
        return "";
    return f.scenes[preset_no].text[line];
}

tele_mode_t flash_last_mode() {
    return is_flash_fresh() ? M_LIVE : f.last_mode;
}

void flash_update_last_mode(tele_mode_t mode) {
    // flashc_memset8((void *)&f.last_mode, mode, sizeof(tele_mode_t), true);
}

void flash_update_cal(cal_data_t *cal) {
    if (is_flash_fresh()) return;
    flashc_memcpy((void *)&f.cal, cal, sizeof(cal_data_t), true);
}

void flash_get_cal(cal_data_t *cal) {
    if (is_flash_fresh())
        init_cal_data(cal);
    else
        *cal = f.cal;
}

void flash_update_device_config(device_config_t *device_config) {
    if (is_flash_fresh()) return;
    flashc_memcpy((void *)&f.device_config, device_config,
                  sizeof(device_config_t), true);
}

void flash_get_device_config(device_config_t *device_config) {
    if (is_flash_fresh())
        device_config->flip = 0;
    else
        *device_config = f.device_config;
}

static void pack_grid(scene_state_t *scene) {
    uint8_t byte = 0;
    uint8_t byte_count = 0;
    for (uint16_t i = 0; i < GRID_BUTTON_COUNT; i++) {
        byte |= (scene->grid.button[i].state != 0) << (i & 7);
        if ((i & 7) == 7) {
            grid_data.button_states[byte_count] = byte;
            byte = 0;
            if (++byte_count >= BUTTON_STATE_SIZE) break;
        }
    }
    for (uint16_t i = 0; i < GRID_FADER_COUNT; i++)
        grid_data.fader_states[i] = scene->grid.fader[i].value;
}

static void unpack_grid(scene_state_t *scene) {
    for (uint16_t i = 0; i < GRID_BUTTON_COUNT; i++) {
        scene->grid.button[i].state =
            0 != (grid_data.button_states[i >> 3] & (1 << (i & 7)));
    }
    for (uint16_t i = 0; i < GRID_FADER_COUNT; i++)
        scene->grid.fader[i].value = grid_data.fader_states[i];
}
