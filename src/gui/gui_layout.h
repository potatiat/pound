#ifndef POUND_GUI_LAYOUT_H
#define POUND_GUI_LAYOUT_H

#include "attributes.h"
#include "gui.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include "cimgui.h"

#define GUI_LAYOUT_PANEL_CAPACITY       16
#define GUI_LAYOUT_DOCK_TARGET_VIEWPORT (-1)

typedef enum
{
    GUI_LAYOUT_REGION_NONE = -1,
    GUI_LAYOUT_REGION_LEFT = 0,
    GUI_LAYOUT_REGION_RIGHT,
    GUI_LAYOUT_REGION_TOP,
    GUI_LAYOUT_REGION_BOTTOM,
} gui_layout_region_t;

typedef struct
{
    float x;
    float y;
    float width;
    float height;
} gui_layout_rectangle_t;

typedef struct
{
    gui_layout_rectangle_t panels[GUI_LAYOUT_PANEL_CAPACITY];
    gui_layout_region_t    regions[GUI_LAYOUT_PANEL_CAPACITY];

    // Fraction each panel requested for its region.
    float fractions[GUI_LAYOUT_PANEL_CAPACITY];

    int  panel_count;
    bool is_computed;
    char pad[3];
} gui_layout_t;

/// Registers a panel into a region.
///
/// A panel's rectangle index is `layout->panel_count` at the time of this call`.
gui_plugin_error_t gui_layout_add_panel(gui_layout_t *POUND_RESTRICT layout,
                                        gui_layout_region_t          region,
                                        float                        fraction);

/// Computes every registered panel rectangle and the main viewport rectangle.
///
/// Left and right regions are full-height strips sized by their largest request fraction.
/// Top and bottom regions span the width left between them.
///
/// Panels split their region evenly: Left/right regions stack top-down in registration order.
/// Top/bottom regions place the newest panel from left to right.
gui_plugin_error_t gui_layout_compute(gui_layout_t *POUND_RESTRICT           layout,
                                      float                                  screen_width,
                                      float                                  screen_height,
                                      gui_layout_rectangle_t *POUND_RESTRICT out_viewport);

#endif // POUND_GUI_LAYOUT_H

/*** end of file ***/
