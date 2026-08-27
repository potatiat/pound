#define _GNU_SOURCE
#include "debug_hex.h"
#include "log.h"
#include "platform.h"
#include "safe_math.h"
#include <stdio.h>
#include <string.h>

#if POUND_PLATFORM_WINDOWS

#include <windows.h>

#elif POUND_PLATFORM_LINUX

#include <sys/uio.h>
#include <unistd.h>

#elif POUND_PLATFORM_APPLE

#include <mach/mach.h>
#include <mach/mach_vm.h>

#endif

typedef enum
{
    DEBUG_HEX_SCAN_MISS = 0,
    DEBUG_HEX_SCAN_HIT,
    DEBUG_HEX_SCAN_TRUNCATED,
} debug_hex_scan_result_t;

static bool                    debug_hex_range_is_valid(uint64_t range_start, uint64_t range_end);
static bool                    debug_hex_parse_find(const char *POUND_RESTRICT input,
                                                    uint8_t *POUND_RESTRICT    needle,
                                                    uint8_t *POUND_RESTRICT    needle_len);
static debug_hex_scan_result_t debug_hex_scan_range(debug_hex_editor_t *POUND_RESTRICT editor,
                                                    uint64_t                           scan_start,
                                                    uint64_t                           scan_end,
                                                    uint64_t *POUND_RESTRICT bytes_scanned);

void
debug_hex_open(debug_hex_editor_t *editor,
               const char         *region_name,
               const uint64_t      range_start,
               const uint64_t      range_end)
{
    if (POUND_UNLIKELY(NULL == editor))
    {
        POUND_LOG_ERROR(&thread_logger, "Aborting function: editor is NULL.");
        return;
    }

    memset(editor->region_name, 0, sizeof(editor->region_name));
    memset(editor->find_input, 0, sizeof(editor->find_input));
    memset(editor->find_needle, 0, sizeof(editor->find_needle));
    editor->find_needle_len     = 0;
    editor->has_selection       = false;
    editor->status              = DEBUG_HEX_STATUS_NONE;
    editor->find_resume_address = 0;

    if (NULL != region_name)
    {
        size_t name_length = strlen(region_name);

        if (name_length >= sizeof(editor->region_name))
        {
            name_length = sizeof(editor->region_name) - 1U;
        }

        memcpy(editor->region_name, region_name, name_length);
        editor->region_name[name_length] = '\0';
    }

    editor->range_start  = range_start;
    editor->range_end    = range_end;
    editor->range_valid  = debug_hex_range_is_valid(range_start, range_end);
    editor->request_open = true;

    if (true == editor->range_valid)
    {
        uint64_t remain = range_end - range_start;
        uint8_t  length = 8;

        if (remain < (uint64_t)length)
        {
            length = (uint8_t)remain;
        }

        if (0 == length)
        {
            length = 1;
        }

        editor->view_address       = range_start;
        editor->selected_address   = range_start;
        editor->selected_length    = length;
        editor->has_selection      = true;
        editor->scroll_to_selected = true;
        snprintf(editor->address_input,
                 sizeof(editor->address_input),
                 "0x%llx",
                 (unsigned long long)range_start);
    }
    else
    {
        editor->view_address     = 0;
        editor->selected_address = 0;
        editor->selected_length  = 0;
        memset(editor->address_input, 0, sizeof(editor->address_input));
    }
}

static bool
debug_hex_range_is_valid(const uint64_t range_start, const uint64_t range_end)
{
    if (UINT64_MAX == range_start)
    {
        return false;
    }

    if (0 == range_end)
    {
        return false;
    }

    if (range_start >= range_end)
    {
        return false;
    }

    return true;
}

POUND_HOT void
debug_hex_copy_bytes(const uint64_t range_start,
                     const uint64_t range_end,
                     const uint64_t address,
                     uint8_t       *out_bytes,
                     uint8_t       *out_valid,
                     const size_t   count)
{
    if (POUND_UNLIKELY(NULL == out_bytes || NULL == out_valid))
    {
        POUND_LOG_ERROR(&thread_logger, "Aborting function: output buffer is NULL.");
        return;
    }

    memset(out_bytes, 0, count);
    memset(out_valid, 0, count);

    if (0 == count)
    {
        return;
    }

    if (false == debug_hex_range_is_valid(range_start, range_end))
    {
        return;
    }

    size_t i = 0;

    while (i < count)
    {
        uint64_t host_address = 0;

        if (POUND_MATH_SUCCESS != safe_math_add_u64(address, (uint64_t)i, &host_address))
        {
            break;
        }

        if (host_address < range_start || host_address >= range_end)
        {
            ++i;
            continue;
        }

        size_t         remaining  = count - i;
        const uint64_t range_left = range_end - host_address;

        if (remaining > range_left)
        {
            remaining = (size_t)range_left;
        }

        const uint64_t page_mask = 0xFFFULL;
        const uint64_t page_left = (page_mask + 1ULL) - (host_address & page_mask);

        if (remaining > page_left)
        {
            remaining = (size_t)page_left;
        }

#if POUND_PLATFORM_WINDOWS

        MEMORY_BASIC_INFORMATION info;
        const SIZE_T queried = VirtualQuery((LPCVOID)(uintptr_t)host_address, &info, sizeof(info));

        if (0 == queried || queried < sizeof(info))
        {
            i += remaining;
            continue;
        }

        const DWORD protect_type = info.Protect & 0xFF;
        const bool  is_committed = (MEM_COMMIT == info.State);
        const bool is_readable = (PAGE_READONLY == protect_type) || (PAGE_READWRITE == protect_type)
                                 || (PAGE_WRITECOPY == protect_type)
                                 || (PAGE_EXECUTE_READ == protect_type)
                                 || (PAGE_EXECUTE_READWRITE == protect_type)
                                 || (PAGE_EXECUTE_WRITECOPY == protect_type);

        if (false == is_committed || false == is_readable || (0 != (info.Protect & PAGE_GUARD)))
        {
            i += remaining;
            continue;
        }

        SIZE_T     bytes_read = 0;
        const BOOL ok         = ReadProcessMemory(GetCurrentProcess(),
                                                  (LPCVOID)(uintptr_t)host_address,
                                                  out_bytes + i,
                                                  remaining,
                                                  &bytes_read);

        if (TRUE == ok && bytes_read > 0)
        {
            size_t copied = (size_t)bytes_read;

            if (copied > remaining)
            {
                copied = remaining;
            }

            memset(out_valid + i, 1, copied);
            i += copied;
        }
        else
        {
            i += remaining;
        }

#elif POUND_PLATFORM_LINUX

        struct iovec local  = { .iov_base = out_bytes + i, .iov_len = remaining };
        struct iovec remote = { .iov_base = (void *)(uintptr_t)host_address, .iov_len = remaining };
        const ssize_t copied = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);

        if (copied > 0)
        {
            size_t n = (size_t)copied;

            if (n > remaining)
            {
                n = remaining;
            }

            memset(out_valid + i, 1, n);
            i += n;
        }
        else
        {
            i += remaining;
        }

#elif POUND_PLATFORM_APPLE

        vm_offset_t            data       = 0;
        mach_msg_type_number_t data_count = 0;
        const kern_return_t    kr         = mach_vm_read(mach_task_self(),
                                                         (mach_vm_address_t)host_address,
                                                         (mach_vm_size_t)remaining,
                                                         &data,
                                                         &data_count);

        if (KERN_SUCCESS == kr && data_count > 0)
        {
            size_t n = (size_t)data_count;

            if (n > remaining)
            {
                n = remaining;
            }

            memcpy(out_bytes + i, (const void *)data, n);
            memset(out_valid + i, 1, n);
            vm_deallocate(mach_task_self(), data, data_count);
            i += n;
        }
        else
        {
            i += remaining;
        }

#else

        i += remaining;

#endif
    }
}

uint64_t
debug_hex_decode_u64(const uint8_t *bytes, const size_t width, const bool big_endian)
{
    uint64_t value = 0;

    if (POUND_UNLIKELY(NULL == bytes) || 0 == width || width > 8U)
    {
        return 0;
    }

    if (true == big_endian)
    {
        size_t index = 0;

        for (; index < width; ++index)
        {
            value = (value << 8U) | (uint64_t)bytes[index];
        }
    }
    else
    {
        size_t index = 0;

        for (; index < width; ++index)
        {
            value |= (uint64_t)bytes[index] << (8U * (unsigned)index);
        }
    }

    return value;
}

static int
debug_hex_nibble(const char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f')
    {
        return 10 + (character - 'a');
    }

    if (character >= 'A' && character <= 'F')
    {
        return 10 + (character - 'A');
    }

    return -1;
}

static bool
debug_hex_pack_hex_needle(const uint8_t *digits,
                          const size_t   digit_count,
                          uint8_t       *needle,
                          uint8_t       *needle_len)
{
    if (digit_count < 2U || 0U != (digit_count % 2U) || digit_count > (DEBUG_HEX_NEEDLE_MAX * 2U))
    {
        return false;
    }

    const size_t byte_count = digit_count / 2U;
    size_t       byte_index = 0;

    for (; byte_index < byte_count; ++byte_index)
    {
        needle[byte_index]
            = (uint8_t)((digits[byte_index * 2U] << 4U) | digits[byte_index * 2U + 1U]);
    }

    *needle_len = (uint8_t)byte_count;
    return true;
}

static bool
debug_hex_parse_find(const char *input, uint8_t *needle, uint8_t *needle_len)
{
    if (NULL == input || NULL == needle || NULL == needle_len)
    {
        return false;
    }

    size_t i = 0;

    while (' ' == input[i] || '\t' == input[i])
    {
        ++i;
    }

    bool force_hex = false;

    if ('0' == input[i] && ('x' == input[i + 1] || 'X' == input[i + 1]))
    {
        force_hex = true;
        i += 2;
    }

    uint8_t digits[DEBUG_HEX_NEEDLE_MAX * 2U];
    size_t  digit_count = 0;
    bool    saw_non_hex = false;

    for (; '\0' != input[i]; ++i)
    {
        const char character = input[i];

        if (' ' == character || '\t' == character)
        {
            continue;
        }

        const int nibble = debug_hex_nibble(character);

        if (nibble < 0)
        {
            saw_non_hex = true;
            break;
        }

        if (digit_count >= sizeof(digits))
        {
            return false;
        }

        digits[digit_count] = (uint8_t)nibble;
        ++digit_count;
    }

    if (true == force_hex)
    {
        if (true == saw_non_hex)
        {
            return false;
        }

        return debug_hex_pack_hex_needle(digits, digit_count, needle, needle_len);
    }

    if (false == saw_non_hex)
    {
        return debug_hex_pack_hex_needle(digits, digit_count, needle, needle_len);
    }

    size_t ascii_length = strlen(input);

    while (ascii_length > 0U
           && (' ' == input[ascii_length - 1U] || '\t' == input[ascii_length - 1U]))
    {
        --ascii_length;
    }

    if (0 == ascii_length || ascii_length > DEBUG_HEX_NEEDLE_MAX)
    {
        return false;
    }

    memcpy(needle, input, ascii_length);
    *needle_len = (uint8_t)ascii_length;
    return true;
}

static debug_hex_scan_result_t
debug_hex_scan_range(debug_hex_editor_t *editor,
                     const uint64_t      scan_start,
                     const uint64_t      scan_end,
                     uint64_t           *bytes_scanned)
{
    const uint8_t needle_len = editor->find_needle_len;

    if (0 == needle_len || scan_start >= scan_end)
    {
        return DEBUG_HEX_SCAN_MISS;
    }

    uint8_t  chunk[4096];
    uint8_t  valid[4096];
    uint64_t cursor  = scan_start;
    uint64_t scanned = *bytes_scanned;

    while (cursor < scan_end && scanned < DEBUG_HEX_FIND_SCAN_MAX)
    {
        uint64_t left = scan_end - cursor;

        if (left < (uint64_t)needle_len)
        {
            break;
        }

        size_t take = sizeof(chunk);

        if ((uint64_t)take > left)
        {
            take = (size_t)left;
        }

        uint64_t remaining_cap = DEBUG_HEX_FIND_SCAN_MAX - scanned;

        if ((uint64_t)take > remaining_cap)
        {
            take = (size_t)remaining_cap;
        }

        if (take < (size_t)needle_len)
        {
            *bytes_scanned = scanned;
            return DEBUG_HEX_SCAN_TRUNCATED;
        }

        debug_hex_copy_bytes(editor->range_start, editor->range_end, cursor, chunk, valid, take);

        const size_t last   = (take >= (size_t)needle_len) ? (take - (size_t)needle_len) : 0;
        size_t       offset = 0;

        for (; offset <= last; ++offset)
        {
            bool   readable = true;
            size_t v        = 0;

            for (; v < (size_t)needle_len; ++v)
            {
                if (0 == valid[offset + v])
                {
                    readable = false;
                    break;
                }
            }

            if (true == readable && 0 == memcmp(chunk + offset, editor->find_needle, needle_len))
            {
                uint64_t hit = 0;

                if (POUND_MATH_SUCCESS != safe_math_add_u64(cursor, (uint64_t)offset, &hit))
                {
                    *bytes_scanned = scanned;
                    return DEBUG_HEX_SCAN_MISS;
                }

                editor->selected_address    = hit;
                editor->view_address        = hit;
                editor->selected_length     = needle_len;
                editor->has_selection       = true;
                editor->scroll_to_selected  = true;
                editor->find_resume_address = hit;
                snprintf(editor->address_input,
                         sizeof(editor->address_input),
                         "0x%llx",
                         (unsigned long long)hit);
                *bytes_scanned = scanned;
                return DEBUG_HEX_SCAN_HIT;
            }
        }

        const size_t advance = (take > (size_t)needle_len) ? (take - (size_t)needle_len + 1U) : 1U;
        uint64_t     next    = 0;

        if (POUND_MATH_SUCCESS != safe_math_add_u64(cursor, (uint64_t)advance, &next))
        {
            break;
        }

        cursor = next;
        scanned += (uint64_t)advance;
    }

    *bytes_scanned = scanned;

    if (cursor < scan_end && scanned >= DEBUG_HEX_FIND_SCAN_MAX)
    {
        return DEBUG_HEX_SCAN_TRUNCATED;
    }

    return DEBUG_HEX_SCAN_MISS;
}

bool
debug_hex_find_next(debug_hex_editor_t *editor)
{
    if (POUND_UNLIKELY(NULL == editor))
    {
        POUND_LOG_ERROR(&thread_logger, "Aborting function: editor is NULL.");
        return false;
    }

    if (false == editor->range_valid)
    {
        editor->status = DEBUG_HEX_STATUS_NONE;
        return false;
    }

    uint8_t needle[DEBUG_HEX_NEEDLE_MAX];
    uint8_t needle_len = 0;

    if (false == debug_hex_parse_find(editor->find_input, needle, &needle_len))
    {
        editor->status = DEBUG_HEX_STATUS_INVALID_PATTERN;
        return false;
    }

    if (needle_len != editor->find_needle_len
        || 0 != memcmp(needle, editor->find_needle, (size_t)needle_len))
    {
        editor->find_resume_address = 0;
    }
    else
    {
    }

    memcpy(editor->find_needle, needle, (size_t)needle_len);
    editor->find_needle_len = needle_len;
    editor->status          = DEBUG_HEX_STATUS_NONE;

    uint64_t scanned = 0;
    uint64_t from
        = (true == editor->has_selection) ? editor->selected_address : editor->range_start;
    bool wrapped_from_end = false;

    if (0 != editor->find_resume_address && editor->find_resume_address == from)
    {
        if (POUND_MATH_SUCCESS != safe_math_add_u64(from, 1ULL, &from))
        {
            from             = editor->range_start;
            wrapped_from_end = true;
        }
        else
        {
        }
    }
    else
    {
    }

    if (from >= editor->range_end)
    {
        from             = editor->range_start;
        wrapped_from_end = true;
    }

    debug_hex_scan_result_t result
        = debug_hex_scan_range(editor, from, editor->range_end, &scanned);

    if (DEBUG_HEX_SCAN_HIT == result)
    {
        if (true == wrapped_from_end)
        {
            editor->status = DEBUG_HEX_STATUS_WRAPPED;
        }
        else
        {
        }

        return true;
    }

    if (DEBUG_HEX_SCAN_TRUNCATED == result)
    {
        editor->status = DEBUG_HEX_STATUS_TRUNCATED;
        return false;
    }

    if (true == wrapped_from_end)
    {
        editor->status = DEBUG_HEX_STATUS_NOT_FOUND;
        return false;
    }

    scanned = 0;
    result  = debug_hex_scan_range(editor, editor->range_start, from, &scanned);

    if (DEBUG_HEX_SCAN_HIT == result)
    {
        editor->status = DEBUG_HEX_STATUS_WRAPPED;
        return true;
    }

    if (DEBUG_HEX_SCAN_TRUNCATED == result)
    {
        editor->status = DEBUG_HEX_STATUS_TRUNCATED;
        return false;
    }

    editor->status = DEBUG_HEX_STATUS_NOT_FOUND;
    return false;
}

/*** end of file ***/
