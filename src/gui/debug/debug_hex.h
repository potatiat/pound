#ifndef POUND_DEBUG_HEX_H
#define POUND_DEBUG_HEX_H

#include "attributes.h"
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEBUG_HEX_BYTES_PER_ROW     16
#define DEBUG_HEX_ADDRESS_INPUT_MAX 24
#define DEBUG_HEX_FIND_INPUT_MAX    128
#define DEBUG_HEX_REGION_NAME_MAX   32
#define DEBUG_HEX_NEEDLE_MAX        16
#define DEBUG_HEX_FIND_SCAN_MAX     (16ULL * 1024ULL * 1024ULL)

typedef enum
{
    DEBUG_HEX_STATUS_NONE = 0,
    DEBUG_HEX_STATUS_INVALID_ADDRESS,
    DEBUG_HEX_STATUS_OUT_OF_RANGE,
    DEBUG_HEX_STATUS_INVALID_PATTERN,
    DEBUG_HEX_STATUS_NOT_FOUND,
    DEBUG_HEX_STATUS_WRAPPED,
    DEBUG_HEX_STATUS_TRUNCATED,
} debug_hex_status_t;

typedef struct
{
    uint64_t range_start;
    uint64_t range_end;
    uint64_t view_address;
    uint64_t selected_address;
    uint64_t find_resume_address;

    char    address_input[DEBUG_HEX_ADDRESS_INPUT_MAX];
    char    find_input[DEBUG_HEX_FIND_INPUT_MAX];
    char    region_name[DEBUG_HEX_REGION_NAME_MAX];
    uint8_t find_needle[DEBUG_HEX_NEEDLE_MAX];

    uint8_t find_needle_len;
    uint8_t selected_length;

    bool    request_open;
    bool    range_valid;
    bool    has_selection;
    bool    scroll_to_selected;
    uint8_t status;
    char    pad[9];
} debug_hex_editor_t;

static_assert(256 == sizeof(debug_hex_editor_t), "Struct size mismatch");

void debug_hex_open(debug_hex_editor_t *POUND_RESTRICT editor,
                    const char *POUND_RESTRICT         region_name,
                    uint64_t                           range_start,
                    uint64_t                           range_end);

POUND_HOT void debug_hex_copy_bytes(uint64_t                range_start,
                                    uint64_t                range_end,
                                    uint64_t                address,
                                    uint8_t *POUND_RESTRICT out_bytes,
                                    uint8_t *POUND_RESTRICT out_valid,
                                    size_t                  count);

bool debug_hex_find_next(debug_hex_editor_t *POUND_RESTRICT editor);

uint64_t debug_hex_decode_u64(const uint8_t *POUND_RESTRICT bytes, size_t width, bool big_endian);

void debug_hex_render(debug_hex_editor_t *context);

#endif // POUND_DEBUG_HEX_H

/*** end of file ***/
