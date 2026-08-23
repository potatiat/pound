#ifndef POUND_GUI_H
#define POUND_GUI_H

#include "attributes.h"
#include "platform.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_PATH         4096
#define FILE_BUFFER_SIZE 65536

#if POUND_PLATFORM_WINDOWS

#define GUI_PLUGIN_NAME "libPoundGui.dll"

#else

#define GUI_PLUGIN_NAME "libPoundGui.so"

#endif // POUND_PLATFORM_WINDOWS

typedef enum
{
    GUI_PLUGIN_SUCCESS = 0,
    GUI_PLUGIN_ERROR_INVALID_ARGUMENT,
    GUI_PLUGIN_ERROR_ALLOCATION_FAILED,
    GUI_PLUGIN_ERROR_BUFFER_TOO_SMALL,
    GUI_PLUGIN_ERROR_PANEL_REGISTRY_FULL,
} gui_plugin_error_t;

typedef struct
{
    gui_plugin_error_t (*create)(const void *saved_state, size_t saved_size, void **out);
    gui_plugin_error_t (*destroy)(void *gui);
    gui_plugin_error_t (*render_frame)(void *gui);
    gui_plugin_error_t (*save)(void *gui, void *out_gui, size_t capacity, size_t *out_size);
} gui_plugin_exports_t;

typedef struct
{
    gui_plugin_exports_t exports;
    void                *module;

    /// gui_state_t.
    void *gui_context;

    char loaded_path[MAX_PATH];
    bool loaded;
    char pad[7];
} gui_plugin_t;

/// gui.c
POUND_EXPORT gui_plugin_error_t gui_plugin_exports_get(gui_plugin_exports_t *out);
POUND_EXPORT const char        *gui_plugin_error_to_string(gui_plugin_error_t error);

/// gui_hot_reload.c
POUND_EXPORT bool     gui_plugin_load_module(gui_plugin_t *POUND_RESTRICT plugin,
                                             const char *POUND_RESTRICT   source_path);
POUND_EXPORT void     gui_plugin_destroy(gui_plugin_t *plugin);
POUND_EXPORT uint64_t file_modified_time(const char *path);
POUND_EXPORT bool     copy_file(const char *POUND_RESTRICT source,
                                const char *POUND_RESTRICT destination);

#endif // POUND_GUI_H

/*** end of file ***/
