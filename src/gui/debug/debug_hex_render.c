#include "debug_hex.h"
#include "log.h"
#include "safe_math.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#define DEBUG_HEX_INTERPRET_RESERVE 180.0F
#define DEBUG_HEX_HIGHLIGHT_COLOR   IM_COL32(100, 149, 237, 180)

static void debug_hex_toolbar_render(debug_hex_editor_t *POUND_RESTRICT context);
static void debug_hex_grid_render(debug_hex_editor_t *POUND_RESTRICT context);
static void debug_hex_interpret_render(const debug_hex_editor_t *POUND_RESTRICT context);
static void debug_hex_format_address(char *POUND_RESTRICT buffer,
                                     size_t               buffer_size,
                                     uint64_t             address);
static void debug_hex_goto_address(debug_hex_editor_t *POUND_RESTRICT context, uint64_t address);
static void debug_hex_nudge_address(debug_hex_editor_t *POUND_RESTRICT context, int64_t delta);
static void debug_hex_print_int_cell(const uint8_t *POUND_RESTRICT bytes,
                                     const uint8_t *POUND_RESTRICT valid,
                                     size_t                        width,
                                     bool                          big_endian,
                                     bool                          is_signed);
static void debug_hex_print_float_cell(const uint8_t *POUND_RESTRICT bytes,
                                       const uint8_t *POUND_RESTRICT valid,
                                       size_t                        width,
                                       bool                          big_endian);

void
debug_hex_render(debug_hex_editor_t *context)
{
    if (POUND_UNLIKELY(NULL == context))
    {
        POUND_LOG_ERROR(&thread_logger, "Aborting function: context is NULL.");
        return;
    }

    char title[160];
    snprintf(title,
             sizeof(title),
             "Hex Editor - %s###HexEditor",
             ('\0' != context->region_name[0]) ? context->region_name : "none");

    const ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoCollapse;

    if (true == igBegin(title, NULL, window_flags))
    {
        debug_hex_toolbar_render(context);
        igSeparator();
        debug_hex_grid_render(context);
        igSeparator();
        debug_hex_interpret_render(context);
    }
    else
    {
    }

    igEnd();
}

static void
debug_hex_format_address(char *buffer, const size_t buffer_size, const uint64_t address)
{
    if (NULL == buffer || 0 == buffer_size)
    {
        return;
    }

    snprintf(buffer, buffer_size, "0x%llx", (unsigned long long)address);
}

static void
debug_hex_goto_address(debug_hex_editor_t *context, uint64_t address)
{
    if (false == context->range_valid)
    {
        return;
    }

    if (address < context->range_start)
    {
        address = context->range_start;
    }

    if (address >= context->range_end)
    {
        address = context->range_end - 1ULL;
    }

    context->view_address     = address;
    context->selected_address = address;

    uint64_t remaining = context->range_end - address;
    uint8_t  length    = 8;

    if (remaining < (uint64_t)length)
    {
        length = (uint8_t)remaining;
    }

    if (0 == length)
    {
        length = 1;
    }

    context->selected_length    = length;
    context->has_selection      = true;
    context->scroll_to_selected = true;
    debug_hex_format_address(context->address_input, sizeof(context->address_input), address);
}

static void
debug_hex_nudge_address(debug_hex_editor_t *context, const int64_t delta)
{
    if (false == context->range_valid)
    {
        return;
    }

    uint64_t next = context->selected_address;

    if (delta >= 0)
    {
        if (POUND_MATH_SUCCESS != safe_math_add_u64(next, (uint64_t)delta, &next))
        {
            next = context->range_end - 1ULL;
        }
    }
    else
    {
        const uint64_t step = (uint64_t)(-delta);

        if (next < context->range_start || (next - context->range_start) < step)
        {
            next = context->range_start;
        }
        else
        {
            next -= step;
        }
    }

    debug_hex_goto_address(context, next);
}

static void
debug_hex_toolbar_render(debug_hex_editor_t *context)
{
    const ImVec2_c button_size = { .x = 0.0f, .y = 0.0f };

    igSetNextItemWidth(140.0f);
    const bool addr_entered = igInputText("addr",
                                          context->address_input,
                                          sizeof(context->address_input),
                                          ImGuiInputTextFlags_EnterReturnsTrue,
                                          NULL,
                                          NULL);

    igSameLine(0.0f, -1.0f);

    if (true == igButton("Go", button_size) || true == addr_entered)
    {
        const char *text = context->address_input;

        while (' ' == text[0] || '\t' == text[0])
        {
            ++text;
        }

        if ('0' == text[0] && ('x' == text[1] || 'X' == text[1]))
        {
            text += 2;
        }

        char *end                       = NULL;
        errno                           = 0;
        const unsigned long long parsed = strtoull(text, &end, 16);

        while (NULL != end && (' ' == *end || '\t' == *end))
        {
            ++end;
        }

        if (NULL == end || end == text || 0 != errno || '\0' != *end)
        {
            context->status = DEBUG_HEX_STATUS_INVALID_ADDRESS;
        }
        else if (false == context->range_valid || (uint64_t)parsed < context->range_start
                 || (uint64_t)parsed >= context->range_end)
        {
            context->status = DEBUG_HEX_STATUS_OUT_OF_RANGE;
        }
        else
        {
            context->status = DEBUG_HEX_STATUS_NONE;
            debug_hex_goto_address(context, (uint64_t)parsed);
        }
    }
    else
    {
    }

    igSameLine(0.0f, -1.0f);

    if (true == igButton("-16", button_size))
    {
        debug_hex_nudge_address(context, -16);
    }
    else
    {
    }

    igSameLine(0.0f, -1.0f);

    if (true == igButton("+16", button_size))
    {
        debug_hex_nudge_address(context, 16);
    }
    else
    {
    }

    igSetNextItemWidth(180.0f);
    const bool find_entered = igInputTextWithHint("find",
                                                  "A0 1B 3F ... or ascii",
                                                  context->find_input,
                                                  sizeof(context->find_input),
                                                  ImGuiInputTextFlags_EnterReturnsTrue,
                                                  NULL,
                                                  NULL);

    igSameLine(0.0f, -1.0f);

    if (true == igButton("Find", button_size) || true == find_entered)
    {
        (void)debug_hex_find_next(context);
    }
    else
    {
    }

    const char *status_text = NULL;

    switch (context->status)
    {
        case DEBUG_HEX_STATUS_INVALID_ADDRESS:
            status_text = "invalid address";
            break;
        case DEBUG_HEX_STATUS_OUT_OF_RANGE:
            status_text = "address out of range";
            break;
        case DEBUG_HEX_STATUS_INVALID_PATTERN:
            status_text = "invalid pattern";
            break;
        case DEBUG_HEX_STATUS_NOT_FOUND:
            status_text = "not found";
            break;
        case DEBUG_HEX_STATUS_WRAPPED:
            status_text = "wrapped";
            break;
        case DEBUG_HEX_STATUS_TRUNCATED:
            status_text = "search truncated";
            break;
        default:
            break;
    }

    if (NULL != status_text)
    {
        igSameLine(0.0f, -1.0f);
        igTextUnformatted(status_text, NULL);
    }
    else
    {
    }

    igSameLine(0.0f, -1.0f);
    igText("region: %s", ('\0' != context->region_name[0]) ? context->region_name : "none");
}

static void
debug_hex_grid_render(debug_hex_editor_t *context)
{
    if (false == context->range_valid)
    {
        igText("Region unknown");
        return;
    }

    const uint64_t range_start = context->range_start;
    const uint64_t range_end   = context->range_end;
    const uint64_t selected    = context->selected_address;
    uint64_t       select_end  = selected;

    if (true == context->has_selection)
    {
        if (POUND_MATH_SUCCESS
            != safe_math_add_u64(selected, (uint64_t)context->selected_length, &select_end))
        {
            select_end = range_end;
        }
    }
    else
    {
    }

    const uint64_t span        = range_end - range_start;
    uint64_t       row_count_u = span / (uint64_t)DEBUG_HEX_BYTES_PER_ROW;

    if (0U != (span % (uint64_t)DEBUG_HEX_BYTES_PER_ROW))
    {
        uint64_t next = 0;

        if (POUND_MATH_SUCCESS == safe_math_add_u64(row_count_u, 1ULL, &next))
        {
            row_count_u = next;
        }
    }

    if (row_count_u > (uint64_t)INT_MAX)
    {
        row_count_u = (uint64_t)INT_MAX;
    }

    const int      row_count = (int)row_count_u;
    const ImVec2_c avail     = igGetContentRegionAvail();
    float          reserve   = DEBUG_HEX_INTERPRET_RESERVE;

    if (avail.y < (DEBUG_HEX_INTERPRET_RESERVE + 80.0f))
    {
        reserve = avail.y * 0.35f;
    }

    if (reserve < 72.0f)
    {
        reserve = 72.0f;
    }

    const ImVec2_c child_size = { .x = 0.0f, .y = -reserve };

    if (false == igBeginChild_Str("##hex_scroll", child_size, 0, 0))
    {
        igEndChild();
        return;
    }

    const float    line_height  = igGetTextLineHeightWithSpacing();
    const ImVec2_c child_window = igGetWindowSize();
    const bool     can_scroll   = (child_window.y > line_height);

    if (true == context->scroll_to_selected && true == can_scroll)
    {
        const uint64_t selected_offset = selected - range_start;
        const uint64_t selected_row_u  = selected_offset / (uint64_t)DEBUG_HEX_BYTES_PER_ROW;
        igSetScrollY_Float((float)selected_row_u * line_height);
    }
    else
    {
    }

    ImGuiListClipper *clipper = ImGuiListClipper_ImGuiListClipper();
    ImGuiListClipper_Begin(clipper, row_count, line_height);

    if (true == context->scroll_to_selected && true == can_scroll)
    {
        const uint64_t selected_offset = selected - range_start;
        const int      selected_row    = (int)(selected_offset / DEBUG_HEX_BYTES_PER_ROW);
        ImGuiListClipper_IncludeItemByIndex(clipper, selected_row);
        context->scroll_to_selected = false;
    }
    else
    {
    }

    const ImVec2_c cell_size = { .x = 0.0f, .y = 0.0f };

    while (true == ImGuiListClipper_Step(clipper))
    {
        int line_i = clipper->DisplayStart;

        for (; line_i < clipper->DisplayEnd; ++line_i)
        {
            const uint64_t row_offset = (uint64_t)line_i * (uint64_t)DEBUG_HEX_BYTES_PER_ROW;
            uint64_t       row_addr   = 0;

            if (POUND_MATH_SUCCESS != safe_math_add_u64(range_start, row_offset, &row_addr))
            {
                break;
            }

            uint8_t bytes[DEBUG_HEX_BYTES_PER_ROW];
            uint8_t valid[DEBUG_HEX_BYTES_PER_ROW];
            size_t  take = DEBUG_HEX_BYTES_PER_ROW;

            if (row_addr >= range_end)
            {
                take = 0;
            }
            else if ((range_end - row_addr) < (uint64_t)take)
            {
                take = (size_t)(range_end - row_addr);
            }

            debug_hex_copy_bytes(range_start, range_end, row_addr, bytes, valid, take);

            igText("0x%016llx:", (unsigned long long)row_addr);
            int byte_i = 0;

            for (; byte_i < (int)take; ++byte_i)
            {
                igSameLine(0.0f, (0 == byte_i) ? -1.0f : ((8 == byte_i) ? 12.0f : 4.0f));
                char cell[24];

                if (0 != valid[byte_i])
                {
                    snprintf(cell, sizeof(cell), "%02X##h%d_%d", bytes[byte_i], line_i, byte_i);
                }
                else
                {
                    snprintf(cell, sizeof(cell), "??##h%d_%d", line_i, byte_i);
                }

                uint64_t   cell_addr = 0;
                const bool have_addr
                    = (POUND_MATH_SUCCESS
                       == safe_math_add_u64(row_addr, (uint64_t)byte_i, &cell_addr));
                const bool is_selected = (true == context->has_selection && true == have_addr
                                          && cell_addr >= selected && cell_addr < select_end);

                if (true == is_selected)
                {
                    igPushStyleColor_U32(ImGuiCol_Header, DEBUG_HEX_HIGHLIGHT_COLOR);
                }
                else
                {
                }

                if (true == igSelectable_Bool(cell, is_selected, 0, cell_size) && true == have_addr)
                {
                    debug_hex_goto_address(context, cell_addr);
                }
                else
                {
                }

                if (true == is_selected)
                {
                    igPopStyleColor(1);
                }
                else
                {
                }
            }

            igSameLine(0.0f, 12.0f);
            char ascii[DEBUG_HEX_BYTES_PER_ROW + 1U];
            int  ascii_i = 0;

            for (; ascii_i < (int)take; ++ascii_i)
            {
                if (0 != valid[ascii_i] && bytes[ascii_i] >= 0x20 && bytes[ascii_i] <= 0x7E)
                {
                    ascii[ascii_i] = (char)bytes[ascii_i];
                }
                else
                {
                    ascii[ascii_i] = '.';
                }
            }

            ascii[take] = '\0';
            igTextUnformatted(ascii, NULL);
        }
    }

    ImGuiListClipper_End(clipper);
    ImGuiListClipper_destroy(clipper);
    igEndChild();
}

static bool
debug_hex_bytes_readable(const uint8_t *valid, const size_t width)
{
    size_t i = 0;

    for (; i < width; ++i)
    {
        if (0 == valid[i])
        {
            return false;
        }
    }

    return true;
}

static void
debug_hex_print_int_cell(const uint8_t *bytes,
                         const uint8_t *valid,
                         const size_t   width,
                         const bool     big_endian,
                         const bool     is_signed)
{
    if (false == debug_hex_bytes_readable(valid, width))
    {
        igText("N/A");
        return;
    }

    const uint64_t raw = debug_hex_decode_u64(bytes, width, big_endian);

    if (true == is_signed && 4U == width)
    {
        igText("%d", (int)(int32_t)raw);
    }
    else
    {
        igText("%llu", (unsigned long long)raw);
    }
}

static void
debug_hex_print_float_cell(const uint8_t *bytes,
                           const uint8_t *valid,
                           const size_t   width,
                           const bool     big_endian)
{
    if (false == debug_hex_bytes_readable(valid, width))
    {
        igText("N/A");
        return;
    }

    if (4U == width)
    {
        const uint32_t bits  = (uint32_t)debug_hex_decode_u64(bytes, 4U, big_endian);
        float          value = 0.0f;
        memcpy(&value, &bits, sizeof(value));
        igText("%g", (double)value);
    }
    else
    {
        const uint64_t bits  = debug_hex_decode_u64(bytes, 8U, big_endian);
        double         value = 0.0;
        memcpy(&value, &bits, sizeof(value));
        igText("%g", value);
    }
}

static void
debug_hex_interpret_render(const debug_hex_editor_t *context)
{
    uint8_t bytes[8];
    uint8_t valid[8];
    memset(bytes, 0, sizeof(bytes));
    memset(valid, 0, sizeof(valid));

    if (true == context->range_valid && true == context->has_selection)
    {
        debug_hex_copy_bytes(
            context->range_start, context->range_end, context->selected_address, bytes, valid, 8);
    }
    else
    {
    }

    unsigned selected_bytes = 0;

    if (true == context->has_selection)
    {
        selected_bytes = (unsigned)context->selected_length;
    }
    else
    {
    }

    igText("Interpret @ 0x%llx (%u bytes selected)",
           (unsigned long long)context->selected_address,
           selected_bytes);

    const ImVec2_c outer_size  = { .x = 0.0f, .y = 0.0f };
    const int      columns     = 3;
    const float    inner_width = 0.0f;

    if (false
        == igBeginTable("##hex_interpret",
                        columns,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH,
                        outer_size,
                        inner_width))
    {
        return;
    }

    igTableNextRow(0, 0.0f);
    igTableSetColumnIndex(0);
    igText("Type");
    igTableSetColumnIndex(1);
    igText("Little-Endian");
    igTableSetColumnIndex(2);
    igText("Big-Endian");

    const struct
    {
        const char *name;
        size_t      width;
        int         kind;
    } rows[] = {
        { "u8", 1, 0 },  { "u16", 2, 0 }, { "u32", 4, 0 }, { "u64", 8, 0 },
        { "i32", 4, 1 }, { "f32", 4, 2 }, { "f64", 8, 2 }, { "ptr", 8, 3 },
    };

    size_t row = 0;

    for (; row < sizeof(rows) / sizeof(rows[0]); ++row)
    {
        igTableNextRow(0, 0.0f);
        igTableSetColumnIndex(0);
        igText("%s", rows[row].name);
        igTableSetColumnIndex(1);

        if (3 == rows[row].kind)
        {
            if (false == debug_hex_bytes_readable(valid, 8))
            {
                igText("N/A");
            }
            else
            {
                igText("0x%llx", (unsigned long long)debug_hex_decode_u64(bytes, 8U, false));
            }
        }
        else if (2 == rows[row].kind)
        {
            debug_hex_print_float_cell(bytes, valid, rows[row].width, false);
        }
        else
        {
            debug_hex_print_int_cell(bytes, valid, rows[row].width, false, 1 == rows[row].kind);
        }

        igTableSetColumnIndex(2);

        if (3 == rows[row].kind)
        {
            if (false == debug_hex_bytes_readable(valid, 8))
            {
                igText("N/A");
            }
            else
            {
                igText("0x%llx", (unsigned long long)debug_hex_decode_u64(bytes, 8U, true));
            }
        }
        else if (2 == rows[row].kind)
        {
            debug_hex_print_float_cell(bytes, valid, rows[row].width, true);
        }
        else
        {
            debug_hex_print_int_cell(bytes, valid, rows[row].width, true, 1 == rows[row].kind);
        }
    }

    igEndTable();
}

/*** end of file ***/
