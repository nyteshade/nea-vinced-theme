#ifndef VINCED_THEME_COLORS_H
#define VINCED_THEME_COLORS_H

#include <exec/types.h>
#include <intuition/intuition.h>

/**
 * @brief Represents a single ANSI color with its properties
 */
typedef struct {
  UBYTE red;          ///< Red component (0-255)
  UBYTE green;        ///< Green component (0-255)
  UBYTE blue;         ///< Blue component (0-255)
  BOOL load_flag;     ///< TRUE if color should be loaded into pen, FALSE for closest match
  UBYTE assigned_pen; ///< The pen number assigned to this color
} AnsiColor;

/**
 * @brief Display format options for color values
 */
typedef enum {
  DISPLAY_RGB = 0,
  DISPLAY_HEX,
  DISPLAY_PEN
} DisplayFormat;

/**
 * @brief Rectangle structure for button hit testing
 */
typedef struct {
  WORD x, y, width, height;
} ButtonRect;

/**
 * @brief Structure for color swatch hit testing
 */
typedef struct {
  WORD x, y, width, height;
  UBYTE color_index;
} SwatchRect;

/**
 * @brief Main structure for the color swatch window
 */
typedef struct {
  struct Window *window;          ///< Intuition window
  struct Screen *screen;          ///< Screen to open window on
  struct RastPort *rastport;      ///< Window's rastport
  struct TextFont *font;          ///< User-selected font
  AnsiColor colors[16];           ///< The 16 ANSI colors
  UBYTE depth;                    ///< Screen depth in bit planes
  UBYTE available_pens;           ///< Number of pens available for assignment
  UBYTE allocated_pens[16];       ///< Track which pens we've allocated
  DisplayFormat display_format;   ///< Current display format
  BOOL is_rtg;                    ///< TRUE if RTG screen detected
  ButtonRect close_button;        ///< Close button coordinates
  ButtonRect rgb_button;          ///< RGB Colors button coordinates
  BOOL close_button_pressed;      ///< TRUE if close button is currently pressed
  BOOL rgb_button_pressed;        ///< TRUE if RGB button is currently pressed
  SwatchRect swatches[16];        ///< Color swatch hit areas
  UBYTE selected_color;           ///< Currently selected color (0-15)
  UBYTE cycle_mode;               ///< Current cycle mode: 0=RGB, 1=HEX, 2=PEN
  WORD aspect_x, aspect_y;        ///< Pixel aspect ratio from IControl prefs
  BOOL dragging;                  ///< TRUE if window is being dragged
  WORD drag_offset_x, drag_offset_y; ///< Mouse offset when dragging started
} ColorSwatchWindow;

/**
 * @brief Default ANSI color definitions (standard 16-color palette)
 */
static const AnsiColor default_ansi_colors[16] = {
  {0x00, 0x00, 0x00, FALSE, 0}, // Black
  {0x80, 0x00, 0x00, FALSE, 0}, // Dark Red
  {0x00, 0x80, 0x00, FALSE, 0}, // Dark Green
  {0x80, 0x80, 0x00, FALSE, 0}, // Dark Yellow
  {0x00, 0x00, 0x80, FALSE, 0}, // Dark Blue
  {0x80, 0x00, 0x80, FALSE, 0}, // Dark Magenta
  {0x00, 0x80, 0x80, FALSE, 0}, // Dark Cyan
  {0xC0, 0xC0, 0xC0, FALSE, 0}, // Light Gray
  {0x80, 0x80, 0x80, FALSE, 0}, // Dark Gray
  {0xFF, 0x00, 0x00, FALSE, 0}, // Bright Red
  {0x00, 0xFF, 0x00, FALSE, 0}, // Bright Green
  {0xFF, 0xFF, 0x00, FALSE, 0}, // Bright Yellow
  {0x00, 0x00, 0xFF, FALSE, 0}, // Bright Blue
  {0xFF, 0x00, 0xFF, FALSE, 0}, // Bright Magenta
  {0x00, 0xFF, 0xFF, FALSE, 0}, // Bright Cyan
  {0xFF, 0xFF, 0xFF, FALSE, 0}  // White
};


/* Internal functions - not exposed in header */
ColorSwatchWindow *init_color_swatch_window(
  AnsiColor *colors,
  char *screen_name
);
void cleanup_color_swatch_window(ColorSwatchWindow *csw);
void show_color_swatch_window(AnsiColor *colors, char *screen_name);

#endif
