#include "amiga_color_window.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <libraries/gadtools.h>
#include <libraries/iffparse.h>
#include <prefs/font.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/diskfont.h>
#include <proto/iffparse.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stdio.h>
#include <string.h>

#define BORDER_WIDTH 4          ///< Custom border width in pixels
#define BORDER_HEIGHT 6         ///< Custom border height in pixels
#define BUTTON_WIDTH 80         ///< Width of buttons
#define BUTTON_HEIGHT 20        ///< Height of buttons
#define SWATCH_SIZE 24          ///< Size of color swatches
#define SWATCH_SPACING 2        ///< Spacing between swatches

/**
 * @brief Gets pixel aspect ratio from IControl preferences
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void get_pixel_aspect_ratio(ColorSwatchWindow *csw)
{
  // Default to square pixels for RTG
  csw->aspect_x = 1;
  csw->aspect_y = 1;

  if (!csw->is_rtg) {
    struct DisplayInfo display_info;

    if (GetDisplayInfoData(NULL, (UBYTE *)&display_info, sizeof(display_info),
                          DTAG_DISP, GetVPModeID(&csw->screen->ViewPort))) {
      // Extract aspect ratio from display info
      csw->aspect_x = display_info.Resolution.x;
      csw->aspect_y = display_info.Resolution.y;
    }
    else {
      // Fallback to common Amiga ratios
      if (csw->screen->Width >= 640) {
        csw->aspect_x = 44;  // NTSC HiRes
        csw->aspect_y = 52;
      }
      else {
        csw->aspect_x = 44;  // NTSC LoRes
        csw->aspect_y = 44;
      }
    }
  }
}

/**
 * @brief Draws the custom minimalist border around the window
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void draw_custom_border(ColorSwatchWindow *csw)
{
  struct RastPort *rp = csw->rastport;
  WORD width = csw->window->Width;
  WORD height = csw->window->Height;

  // Calculate border dimensions respecting aspect ratio
  WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
  WORD adj_border_height = BORDER_HEIGHT;

  // Draw outer border (darker)
  SetAPen(rp, 2); // Dark pen

  // Top border
  RectFill(rp, 0, 0, width - 1, adj_border_height - 1);

  // Bottom border
  RectFill(rp, 0, height - adj_border_height, width - 1, height - 1);

  // Left border
  RectFill(rp, 0, 0, adj_border_width - 1, height - 1);

  // Right border
  RectFill(rp, width - adj_border_width, 0, width - 1, height - 1);

  // Draw inner highlight (lighter)
  SetAPen(rp, 1); // Light pen

  // Inner border lines
  Move(rp, adj_border_width, adj_border_height);
  Draw(rp, width - adj_border_width - 1, adj_border_height);
  Draw(rp, width - adj_border_width - 1, height - adj_border_height - 1);
  Draw(rp, adj_border_width, height - adj_border_height - 1);
  Draw(rp, adj_border_width, adj_border_height);
}

/**
 * @brief Draws a button with proper styling
 * @param csw Pointer to ColorSwatchWindow structure
 * @param button Pointer to ButtonRect structure
 * @param text Button text
 * @param pressed TRUE if button is pressed
 */
static void draw_button(ColorSwatchWindow *csw, ButtonRect *button, 
                       char *text, BOOL pressed)
{
  struct RastPort *rp = csw->rastport;
  WORD text_x, text_y;
  WORD text_len;

  // Draw button background using proper background pen
  SetAPen(rp, 0); // Use background pen (typically white/light)
  RectFill(rp, button->x, button->y,
           button->x + button->width - 1,
           button->y + button->height - 1);

  // Draw button border (raised/lowered effect)
  SetAPen(rp, pressed ? 2 : 1);
  
  // Top and left (highlight/shadow based on pressed state)
  Move(rp, button->x, button->y + button->height - 1);
  Draw(rp, button->x, button->y);
  Draw(rp, button->x + button->width - 1, button->y);
  
  SetAPen(rp, pressed ? 1 : 2);
  
  // Bottom and right (shadow/highlight based on pressed state)
  Move(rp, button->x + button->width - 1, button->y + 1);
  Draw(rp, button->x + button->width - 1, button->y + button->height - 1);
  Draw(rp, button->x + 1, button->y + button->height - 1);

  // Draw button text using foreground pen
  SetAPen(rp, 1); // Use foreground pen instead of pen 3
  SetFont(rp, csw->font);
  
  text_len = TextLength(rp, text, strlen(text));
  text_x = button->x + (button->width - text_len) / 2;
  text_y = button->y + (button->height + csw->font->tf_YSize) / 2 - 2;
  
  Move(rp, text_x, text_y);
  Text(rp, text, strlen(text));
}

/**
 * @brief Draws a cycle gadget with current mode
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void draw_cycle_gadget(ColorSwatchWindow *csw)
{
  struct RastPort *rp = csw->rastport;
  WORD window_height = csw->window->Height;
  WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
  WORD gadget_y = window_height - BORDER_HEIGHT - BUTTON_HEIGHT - 8;
  char *mode_texts[] = {"RGB", "HEX", "PEN"};
  char *current_text = mode_texts[csw->cycle_mode];
  WORD text_len, arrow_x;
  
  // Position cycle gadget
  csw->rgb_button.x = adj_border_width + 8;
  csw->rgb_button.y = gadget_y;
  csw->rgb_button.width = BUTTON_WIDTH;
  csw->rgb_button.height = BUTTON_HEIGHT;
  
  // Draw gadget background using proper background pen
  SetAPen(rp, 0); // Use background pen (typically white/light)
  RectFill(rp, csw->rgb_button.x, csw->rgb_button.y,
           csw->rgb_button.x + csw->rgb_button.width - 1,
           csw->rgb_button.y + csw->rgb_button.height - 1);
  
  // Draw sunken border for cycle gadget
  SetAPen(rp, 2); // Dark shadow
  Move(rp, csw->rgb_button.x, csw->rgb_button.y + csw->rgb_button.height - 1);
  Draw(rp, csw->rgb_button.x, csw->rgb_button.y);
  Draw(rp, csw->rgb_button.x + csw->rgb_button.width - 1, csw->rgb_button.y);
  
  SetAPen(rp, 1); // Light highlight
  Move(rp, csw->rgb_button.x + 1, csw->rgb_button.y + csw->rgb_button.height - 1);
  Draw(rp, csw->rgb_button.x + csw->rgb_button.width - 1, csw->rgb_button.y + csw->rgb_button.height - 1);
  Draw(rp, csw->rgb_button.x + csw->rgb_button.width - 1, csw->rgb_button.y + 1);
  
  // Clear text area completely before redrawing
  SetAPen(rp, 0); // Background pen
  RectFill(rp, csw->rgb_button.x + 2, csw->rgb_button.y + 2,
           csw->rgb_button.x + csw->rgb_button.width - 14, 
           csw->rgb_button.y + csw->rgb_button.height - 3);
  
  // Draw current mode text
  SetAPen(rp, 1); // Use foreground pen instead of pen 3
  SetFont(rp, csw->font);
  text_len = TextLength(rp, current_text, strlen(current_text));
  
  Move(rp, csw->rgb_button.x + 4, 
       csw->rgb_button.y + (csw->rgb_button.height + csw->font->tf_YSize) / 2 - 2);
  Text(rp, current_text, strlen(current_text));
  
  // Draw cycle arrows (indicating it's clickable)
  arrow_x = csw->rgb_button.x + csw->rgb_button.width - 12;
  SetAPen(rp, 1); // Use foreground pen
  
  // Up arrow
  Move(rp, arrow_x, csw->rgb_button.y + 6);
  Draw(rp, arrow_x + 3, csw->rgb_button.y + 3);
  Draw(rp, arrow_x + 6, csw->rgb_button.y + 6);
  
  // Down arrow  
  Move(rp, arrow_x, csw->rgb_button.y + 12);
  Draw(rp, arrow_x + 3, csw->rgb_button.y + 15);
  Draw(rp, arrow_x + 6, csw->rgb_button.y + 12);
}

/**
 * @brief Draws the bottom buttons (cycle gadget and Close)
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void draw_bottom_buttons(ColorSwatchWindow *csw)
{
  WORD window_width = csw->window->Width;
  WORD window_height = csw->window->Height;
  WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
  WORD button_y = window_height - BORDER_HEIGHT - BUTTON_HEIGHT - 8;
  
  // Position close button
  csw->close_button.x = window_width - adj_border_width - BUTTON_WIDTH - 8;
  csw->close_button.y = button_y;
  csw->close_button.width = BUTTON_WIDTH;
  csw->close_button.height = BUTTON_HEIGHT;
  
  // Draw the cycle gadget and close button
  draw_cycle_gadget(csw);
  draw_button(csw, &csw->close_button, "Close", csw->close_button_pressed);
}

/**
 * @brief Tests if a point is within a button
 * @param button Pointer to ButtonRect structure
 * @param x X coordinate to test
 * @param y Y coordinate to test
 * @return TRUE if point is within button
 */
static BOOL point_in_button(ButtonRect *button, WORD x, WORD y)
{
  if (x >= button->x && x < button->x + button->width &&
      y >= button->y && y < button->y + button->height) {
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Tests if a point is within a color swatch
 * @param csw Pointer to ColorSwatchWindow structure
 * @param x X coordinate to test
 * @param y Y coordinate to test
 * @return Color index (0-15) or -1 if not in any swatch
 */
static int point_in_swatch(ColorSwatchWindow *csw, WORD x, WORD y)
{
  int i;
  for (i = 0; i < 16; i++) {
    if (x >= csw->swatches[i].x && x < csw->swatches[i].x + csw->swatches[i].width &&
        y >= csw->swatches[i].y && y < csw->swatches[i].y + csw->swatches[i].height) {
      return i;
    }
  }
  return -1;
}

/**
 * @brief Checks if a key combination matches the system affirmation shortcut
 * @param code Raw key code
 * @param qualifier Key qualifiers
 * @return TRUE if this is LeftAmiga+V (affirmation shortcut)
 */
static BOOL is_affirmation_shortcut(UWORD code, UWORD qualifier)
{
  // LeftAmiga + V (0x34 is V key)
  if ((qualifier & IEQUALIFIER_LCOMMAND) && (code == 0x34)) {
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Checks if a key combination matches the close shortcut
 * @param code Raw key code
 * @param qualifier Key qualifiers
 * @return TRUE if this is RightAmiga+C (close shortcut)
 */
static BOOL is_close_shortcut(UWORD code, UWORD qualifier)
{
  // RightAmiga + C (0x33 is C key)
  if ((qualifier & IEQUALIFIER_RCOMMAND) && (code == 0x33)) {
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Detects if the current screen is RTG-capable
 * @param screen Pointer to the screen to check
 * @return TRUE if RTG, FALSE if chipset
 */
static BOOL detect_rtg_screen(struct Screen *screen)
{
  struct DisplayInfo display_info;

  if (GetDisplayInfoData(NULL, (UBYTE *)&display_info, sizeof(display_info),
                        DTAG_DISP, GetVPModeID(&screen->ViewPort))) {
    // RTG screens typically have PropertyFlags that indicate non-Amiga chipset
    // Since DIPF_IS_RTG might not be defined in older includes, check for 
    // screens that are not standard Amiga display modes
    if (display_info.PropertyFlags & 0x00000200) { /* RTG flag if available */
      return TRUE;
    }
  }

  // Fallback: assume RTG if depth > 8 or unusual dimensions
  if (screen->RastPort.BitMap->Depth > 8) {
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief Calculates color distance for closest match approximation
 * @param r1 Red component of first color
 * @param g1 Green component of first color
 * @param b1 Blue component of first color
 * @param r2 Red component of second color
 * @param g2 Green component of second color
 * @param b2 Blue component of second color
 * @return Distance value (lower = closer match)
 */
static ULONG calculate_color_distance(UBYTE r1, UBYTE g1, UBYTE b1,
                                     UBYTE r2, UBYTE g2, UBYTE b2)
{
  LONG dr = r1 - r2;
  LONG dg = g1 - g2;
  LONG db = b1 - b2;

  return (ULONG)((dr * dr) + (dg * dg) + (db * db));
}

/**
 * @brief Finds the closest available pen for a given RGB color
 * @param csw Pointer to ColorSwatchWindow structure
 * @param red Red component to match
 * @param green Green component to match
 * @param blue Blue component to match
 * @return Best matching pen number
 */
static UBYTE find_closest_pen(ColorSwatchWindow *csw, UBYTE red, UBYTE green, UBYTE blue)
{
  UBYTE best_pen = 0;
  ULONG best_distance = 0xFFFFFFFF;
  ULONG *rgb;
  UBYTE pen_red, pen_green, pen_blue;
  UBYTE pen;
  ULONG distance;

  for (pen = 0; pen < csw->available_pens; pen++) {
    ULONG rgb_values[3];
    GetRGB32(csw->screen->ViewPort.ColorMap, pen, 1, rgb_values);
    rgb = rgb_values;

    pen_red = (rgb[0] >> 24) & 0xFF;
    pen_green = (rgb[1] >> 24) & 0xFF;
    pen_blue = (rgb[2] >> 24) & 0xFF;

    distance = calculate_color_distance(red, green, blue,
                                            pen_red, pen_green, pen_blue);

    if (distance < best_distance) {
      best_distance = distance;
      best_pen = pen;
    }
  }

  return best_pen;
}

/**
 * @brief Allocates and assigns pens based on screen capabilities
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void assign_color_pens(ColorSwatchWindow *csw)
{
  UBYTE load_pen_start;
  UBYTE load_pen_count = 0;
  int i;

  // Determine behavior based on bit depth
  switch (csw->depth) {
    case 1: // 2 colors - no loading, repeat every 2
      for (i = 0; i < 16; i++) {
        csw->colors[i].assigned_pen = i % 2;
      }
      break;

    case 2: // 4 colors - no loading, repeat every 4
      for (i = 0; i < 16; i++) {
        csw->colors[i].assigned_pen = i % 4;
      }
      break;

    case 3: // 8 colors - no loading, closest match only
      for (i = 0; i < 16; i++) {
        if (i < 8) {
          csw->colors[i].assigned_pen = find_closest_pen(csw,
            csw->colors[i].red, csw->colors[i].green, csw->colors[i].blue);
        }
        else {
          // Mirror first 8 colors
          csw->colors[i].assigned_pen = csw->colors[i - 8].assigned_pen;
        }
      }
      break;

    case 4: // 16 colors - 12 available for reassignment
      load_pen_start = 8; // Last 8 pens available for loading

      for (i = 0; i < 16; i++) {
        if (csw->colors[i].load_flag && load_pen_count < 8) {
          // Allocate a pen and set its color
          csw->colors[i].assigned_pen = load_pen_start + load_pen_count;
          SetRGB32(&csw->screen->ViewPort, csw->colors[i].assigned_pen,
                  (csw->colors[i].red << 24) | 0x00FFFFFF,
                  (csw->colors[i].green << 24) | 0x00FFFFFF,
                  (csw->colors[i].blue << 24) | 0x00FFFFFF);
          load_pen_count++;
        }
        else {
          if (i < 8) {
            csw->colors[i].assigned_pen = find_closest_pen(csw,
              csw->colors[i].red, csw->colors[i].green, csw->colors[i].blue);
          }
          else {
            // Mirror first 8 for bright colors
            csw->colors[i].assigned_pen = csw->colors[i - 8].assigned_pen;
          }
        }
      }
      break;

    default: // 5+ bit planes (32+ colors) - full loading capability
      for (i = 0; i < 16; i++) {
        if (csw->colors[i].load_flag) {
          // Try to obtain a pen
          LONG pen;
          pen = ObtainBestPenA(csw->screen->ViewPort.ColorMap,
            (csw->colors[i].red << 24) | 0x00FFFFFF,
            (csw->colors[i].green << 24) | 0x00FFFFFF,
            (csw->colors[i].blue << 24) | 0x00FFFFFF,
            NULL);

          if (pen != -1) {
            csw->colors[i].assigned_pen = pen;
            csw->allocated_pens[i] = pen;
          }
          else {
            csw->colors[i].assigned_pen = find_closest_pen(csw,
              csw->colors[i].red, csw->colors[i].green, csw->colors[i].blue);
          }
        }
        else {
          csw->colors[i].assigned_pen = find_closest_pen(csw,
            csw->colors[i].red, csw->colors[i].green, csw->colors[i].blue);
        }
      }
      break;
  }
}

/**
 * @brief Opens the user's preferred font from Font preferences or falls back to system default
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void open_user_font(ColorSwatchWindow *csw)
{
  struct TextAttr ta;
  struct IFFHandle *iff = NULL;
  struct FontPrefs fontprefs;
  BPTR fh;
  BOOL prefs_loaded = FALSE;

  // Initialize defaults
  ta.ta_Name = "topaz.font";
  ta.ta_YSize = 8;
  ta.ta_Style = 0;
  ta.ta_Flags = 0;

  // Try to read Font preferences
  fh = Open("ENV:Sys/font.prefs", MODE_OLDFILE);
  if (!fh) {
    fh = Open("ENVARC:Sys/font.prefs", MODE_OLDFILE);
  }
  
  if (fh) {
    iff = AllocIFF();
    if (iff) {
      iff->iff_Stream = (ULONG)fh;
      InitIFFasDOS(iff);
      
      if (OpenIFF(iff, IFFF_READ) == 0) {
        if (PropChunk(iff, MAKE_ID('P','R','E','F'), MAKE_ID('F','O','N','T')) == 0) {
          if (ParseIFF(iff, IFFPARSE_SCAN) == 0) {
            struct StoredProperty *sp = FindProp(iff, MAKE_ID('P','R','E','F'), MAKE_ID('F','O','N','T'));
            if (sp && sp->sp_Size >= sizeof(struct FontPrefs)) {
              CopyMem(sp->sp_Data, &fontprefs, sizeof(struct FontPrefs));
              
              // Use system font preferences
              ta.ta_Name = fontprefs.fp_Name;
              ta.ta_YSize = fontprefs.fp_TextAttr.ta_YSize;
              ta.ta_Style = fontprefs.fp_TextAttr.ta_Style;
              ta.ta_Flags = fontprefs.fp_TextAttr.ta_Flags;
              prefs_loaded = TRUE;
            }
          }
        }
        CloseIFF(iff);
      }
      FreeIFF(iff);
    }
    Close(fh);
  }

  // Try to open the font
  csw->font = OpenDiskFont(&ta);
  if (!csw->font) {
    // If preferred font failed, try screen font
    csw->font = csw->screen->RastPort.Font;
    if (!csw->font) {
      // Last resort: try to open topaz 8
      ta.ta_Name = "topaz.font";
      ta.ta_YSize = 8;
      ta.ta_Style = 0;
      ta.ta_Flags = 0;
      csw->font = OpenDiskFont(&ta);
    }
  }

  // If we still don't have a font, use the screen's font
  if (!csw->font) {
    csw->font = csw->screen->RastPort.Font;
  }
}

/**
 * @brief Formats color value according to current display format
 * @param csw Pointer to ColorSwatchWindow structure
 * @param color_index Index of the color to format
 * @param buffer Buffer to store formatted string
 * @param requested TRUE for requested RGB, FALSE for displayed RGB
 */
static void format_color_value(ColorSwatchWindow *csw, int color_index,
                              char *buffer, BOOL requested)
{
  AnsiColor *color = &csw->colors[color_index];
  UBYTE r, g, b;

  if (requested) {
    r = color->red;
    g = color->green;
    b = color->blue;
  }
  else {
    // Get actual displayed color from pen
    ULONG rgb_values[3];
    ULONG *rgb;
    GetRGB32(csw->screen->ViewPort.ColorMap, color->assigned_pen, 1, rgb_values);
    rgb = rgb_values;
    r = (rgb[0] >> 24) & 0xFF;
    g = (rgb[1] >> 24) & 0xFF;
    b = (rgb[2] >> 24) & 0xFF;
  }

  switch (csw->display_format) {
    case DISPLAY_RGB:
      sprintf(buffer, "RGB(%d,%d,%d)", r, g, b);
      break;

    case DISPLAY_HEX:
      sprintf(buffer, "#%02X%02X%02X", r, g, b);
      break;

    case DISPLAY_PEN:
      sprintf(buffer, "Pen %d", color->assigned_pen);
      break;
  }
}

/**
 * @brief Draws the color swatches in two rows of 8
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void draw_color_swatches(ColorSwatchWindow *csw)
{
  struct RastPort *rp = csw->rastport;
  WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
  WORD start_x = adj_border_width + 16;
  WORD start_y = BORDER_HEIGHT + 16;
  WORD swatch_x, swatch_y;
  int i, row, col;

  for (i = 0; i < 16; i++) {
    row = i / 8;  // 0 for colors 0-7, 1 for colors 8-15
    col = i % 8;  // 0-7 for position in row
    
    swatch_x = start_x + col * (SWATCH_SIZE + SWATCH_SPACING);
    swatch_y = start_y + row * (SWATCH_SIZE + SWATCH_SPACING);
    
    // Store swatch coordinates for hit testing
    csw->swatches[i].x = swatch_x;
    csw->swatches[i].y = swatch_y;
    csw->swatches[i].width = SWATCH_SIZE;
    csw->swatches[i].height = SWATCH_SIZE;
    csw->swatches[i].color_index = i;
    
    // Draw color swatch
    SetAPen(rp, csw->colors[i].assigned_pen);
    RectFill(rp, swatch_x, swatch_y, 
             swatch_x + SWATCH_SIZE - 1, swatch_y + SWATCH_SIZE - 1);
    
    // Draw swatch border (highlight if selected)
    if (i == csw->selected_color) {
      SetAPen(rp, 3); // Bright pen for selection
      
      // Draw thick selection border
      Move(rp, swatch_x - 2, swatch_y - 2);
      Draw(rp, swatch_x + SWATCH_SIZE + 1, swatch_y - 2);
      Draw(rp, swatch_x + SWATCH_SIZE + 1, swatch_y + SWATCH_SIZE + 1);
      Draw(rp, swatch_x - 2, swatch_y + SWATCH_SIZE + 1);
      Draw(rp, swatch_x - 2, swatch_y - 2);
      
      Move(rp, swatch_x - 1, swatch_y - 1);
      Draw(rp, swatch_x + SWATCH_SIZE, swatch_y - 1);
      Draw(rp, swatch_x + SWATCH_SIZE, swatch_y + SWATCH_SIZE);
      Draw(rp, swatch_x - 1, swatch_y + SWATCH_SIZE);
      Draw(rp, swatch_x - 1, swatch_y - 1);
    } else {
      SetAPen(rp, 2); // Normal border
      Move(rp, swatch_x - 1, swatch_y - 1);
      Draw(rp, swatch_x + SWATCH_SIZE, swatch_y - 1);
      Draw(rp, swatch_x + SWATCH_SIZE, swatch_y + SWATCH_SIZE);
      Draw(rp, swatch_x - 1, swatch_y + SWATCH_SIZE);
      Draw(rp, swatch_x - 1, swatch_y - 1);
    }
  }
}

/**
 * @brief Draws the color information table
 * @param csw Pointer to ColorSwatchWindow structure
 */
static void draw_color_table(ColorSwatchWindow *csw)
{
  struct RastPort *rp = csw->rastport;
  WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
  WORD table_x = adj_border_width + 16;
  WORD table_y = BORDER_HEIGHT + 80; // Below the swatches
  char buffer[64];
  int i;
  
  SetFont(rp, csw->font);
  SetAPen(rp, 1);
  
  // Draw table headers
  Move(rp, table_x, table_y);
  Text(rp, "Normal", 6);
  Move(rp, table_x + 200, table_y);
  Text(rp, "Bright", 6);
  
  // Draw color entries
  for (i = 0; i < 8; i++) {
    WORD line_y = table_y + 20 + (i * 16);
    
    // Highlight selected color
    if (i == (csw->selected_color % 8)) {
      SetAPen(rp, 2);
      RectFill(rp, table_x - 2, line_y - 10, 
               table_x + 380, line_y + 6);
    }
    
    SetAPen(rp, 1);
    
    // Normal color (0-7)
    sprintf(buffer, " %d     ", i);
    Move(rp, table_x, line_y);
    Text(rp, buffer, strlen(buffer));
    
    format_color_value(csw, i, buffer, TRUE);
    Move(rp, table_x + 40, line_y);
    Text(rp, buffer, strlen(buffer));
    
    // Bright color (8-15)
    sprintf(buffer, " %d", i);
    Move(rp, table_x + 200, line_y);
    Text(rp, buffer, strlen(buffer));
    
    format_color_value(csw, i + 8, buffer, TRUE);
    Move(rp, table_x + 240, line_y);
    Text(rp, buffer, strlen(buffer));
  }
}

/**
 * @brief Handles window events and user interaction
 * @param csw Pointer to ColorSwatchWindow structure
 * @return TRUE to continue, FALSE to exit
 */
static BOOL handle_events(ColorSwatchWindow *csw)
{
  struct IntuiMessage *msg;
  BOOL continue_loop = TRUE;

  while ((msg = (struct IntuiMessage *)GetMsg(csw->window->UserPort))) {
    switch (msg->Class) {
      case IDCMP_CLOSEWINDOW:
        continue_loop = FALSE;
        break;

      case IDCMP_MOUSEBUTTONS:
        if (msg->Code == SELECTDOWN) {
          if (point_in_button(&csw->close_button, msg->MouseX, msg->MouseY)) {
            csw->close_button_pressed = TRUE;
            draw_bottom_buttons(csw);
          }
          else if (point_in_button(&csw->rgb_button, msg->MouseX, msg->MouseY)) {
            csw->rgb_button_pressed = TRUE;
            draw_bottom_buttons(csw);
          }
          else {
            // Check for swatch clicks
            int swatch_idx = point_in_swatch(csw, msg->MouseX, msg->MouseY);
            if (swatch_idx >= 0) {
              csw->selected_color = swatch_idx;
              draw_color_swatches(csw);
              draw_color_table(csw);
            }
            else {
              // Check if clicking on border area for dragging
              WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
              if (msg->MouseX < adj_border_width || msg->MouseX >= csw->window->Width - adj_border_width ||
                  msg->MouseY < BORDER_HEIGHT || msg->MouseY >= csw->window->Height - BORDER_HEIGHT) {
                // Start dragging
                csw->dragging = TRUE;
                csw->drag_offset_x = msg->MouseX;
                csw->drag_offset_y = msg->MouseY;
              }
            }
          }
        }
        else if (msg->Code == SELECTUP) {
          if (csw->close_button_pressed) {
            csw->close_button_pressed = FALSE;
            if (point_in_button(&csw->close_button, msg->MouseX, msg->MouseY)) {
              continue_loop = FALSE; // Close window
            }
            draw_bottom_buttons(csw);
          }
          else if (csw->rgb_button_pressed) {
            csw->rgb_button_pressed = FALSE;
            if (point_in_button(&csw->rgb_button, msg->MouseX, msg->MouseY)) {
              // Cycle through display modes
              csw->cycle_mode = (csw->cycle_mode + 1) % 3;
              csw->display_format = (DisplayFormat)csw->cycle_mode;
              draw_color_table(csw);
            }
            draw_bottom_buttons(csw);
          }
          else if (csw->dragging) {
            // Stop dragging
            csw->dragging = FALSE;
          }
        }
        break;

      case IDCMP_MOUSEMOVE:
        // Handle window dragging
        if (csw->dragging) {
          WORD new_x = csw->window->LeftEdge + (msg->MouseX - csw->drag_offset_x);
          WORD new_y = csw->window->TopEdge + (msg->MouseY - csw->drag_offset_y);
          
          // Keep window on screen
          if (new_x < -csw->window->Width + 32) new_x = -csw->window->Width + 32;
          if (new_y < -csw->window->Height + 16) new_y = -csw->window->Height + 16;
          if (new_x > csw->screen->Width - 32) new_x = csw->screen->Width - 32;
          if (new_y > csw->screen->Height - 16) new_y = csw->screen->Height - 16;
          
          ChangeWindowBox(csw->window, new_x, new_y, 
                         csw->window->Width, csw->window->Height);
        }
        break;

      case IDCMP_RAWKEY:
        // Check for keyboard shortcuts
        if (is_affirmation_shortcut(msg->Code, msg->Qualifier) ||
            is_close_shortcut(msg->Code, msg->Qualifier)) {
          continue_loop = FALSE;
        }
        // Toggle display format with 'T' key
        else if (msg->Code == 0x14) { // T key
          csw->cycle_mode = (csw->cycle_mode + 1) % 3;
          csw->display_format = (DisplayFormat)csw->cycle_mode;
          draw_custom_border(csw);
          draw_bottom_buttons(csw);
          draw_color_swatches(csw);
          draw_color_table(csw);
        }
        break;

      case IDCMP_REFRESHWINDOW:
        BeginRefresh(csw->window);
        draw_custom_border(csw);
        draw_bottom_buttons(csw);
        draw_color_swatches(csw);
        draw_color_table(csw);
        EndRefresh(csw->window, TRUE);
        break;
    }
    ReplyMsg((struct Message *)msg);
  }

  return continue_loop;
}

/**
 * @brief Initializes the color swatch window
 * @param colors Array of 16 AnsiColor structures (can be NULL for defaults)
 * @param screen_name Name of screen to open on (NULL for default)
 * @return Pointer to ColorSwatchWindow structure or NULL on failure
 */
ColorSwatchWindow *init_color_swatch_window(AnsiColor *colors, char *screen_name)
{
  ColorSwatchWindow *csw = AllocVec(sizeof(ColorSwatchWindow), MEMF_CLEAR);
  if (!csw) return NULL;

  // Copy colors or use defaults
  if (colors) {
    memcpy(csw->colors, colors, sizeof(AnsiColor) * 16);
  }
  else {
    memcpy(csw->colors, default_ansi_colors, sizeof(AnsiColor) * 16);
  }

  // Open screen (or use Workbench)
  if (screen_name) {
    csw->screen = LockPubScreen(screen_name);
  }
  else {
    csw->screen = LockPubScreen(NULL); // Workbench
  }

  if (!csw->screen) {
    FreeVec(csw);
    return NULL;
  }

  // Detect screen capabilities
  csw->depth = csw->screen->RastPort.BitMap->Depth;
  csw->available_pens = 1 << csw->depth;
  csw->is_rtg = detect_rtg_screen(csw->screen);
  csw->display_format = DISPLAY_RGB;
  csw->close_button_pressed = FALSE;
  csw->rgb_button_pressed = FALSE;
  csw->selected_color = 0;
  csw->cycle_mode = 0;
  csw->dragging = FALSE;
  csw->drag_offset_x = 0;
  csw->drag_offset_y = 0;

  // Get pixel aspect ratio for proper border scaling
  get_pixel_aspect_ratio(csw);

  // Open font
  open_user_font(csw);

  // Assign color pens based on capabilities
  assign_color_pens(csw);

  // Calculate window dimensions with proper borders
  {
    WORD adj_border_width = (BORDER_WIDTH * csw->aspect_x) / csw->aspect_y;
    WORD window_width = 400 + (adj_border_width * 2);
    WORD window_height = 300 + (BORDER_HEIGHT * 2);

    // Open borderless draggable window
    csw->window = OpenWindowTags(NULL,
      WA_Left, 50,
      WA_Top, 50,
      WA_Width, window_width,
      WA_Height, window_height,
      WA_Title, NULL,  // No title bar
      WA_Flags, WFLG_BORDERLESS | WFLG_ACTIVATE | WFLG_RMBTRAP,
      WA_IDCMP, IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_RAWKEY | IDCMP_REFRESHWINDOW | IDCMP_ACTIVEWINDOW,
      WA_PubScreen, csw->screen,
      WA_MouseQueue, 10,
      TAG_DONE);
  }

  if (!csw->window) {
    UnlockPubScreen(NULL, csw->screen);
    FreeVec(csw);
    return NULL;
  }

  csw->rastport = csw->window->RPort;

  return csw;
}

/**
 * @brief Cleans up and closes the color swatch window
 * @param csw Pointer to ColorSwatchWindow structure
 */
void cleanup_color_swatch_window(ColorSwatchWindow *csw)
{
  int i;
  
  if (!csw) return;

  // Release allocated pens
  for (i = 0; i < 16; i++) {
    if (csw->allocated_pens[i] != 0) {
      ReleasePen(csw->screen->ViewPort.ColorMap, csw->allocated_pens[i]);
    }
  }

  if (csw->window) {
    CloseWindow(csw->window);
  }

  if (csw->font && csw->font != csw->screen->RastPort.Font) {
    CloseFont(csw->font);
  }

  if (csw->screen) {
    UnlockPubScreen(NULL, csw->screen);
  }

  FreeVec(csw);
}

/**
 * @brief Main function to display the color swatch window
 * @param colors Array of 16 AnsiColor structures
 * @param screen_name Screen to open on (NULL for Workbench)
 */
void show_color_swatch_window(AnsiColor *colors, char *screen_name)
{
  ColorSwatchWindow *csw = init_color_swatch_window(colors, screen_name);
  if (!csw) {
    printf("Failed to initialize color swatch window\n");
    return;
  }

  printf("Color Swatch Window opened.\n");
  printf("Shortcuts: T=Toggle format, RAmiga+C=Close, LAmiga+V=Close\n");
  printf("Depth: %d bit planes (%d colors), RTG: %s\n",
         csw->depth, csw->available_pens, csw->is_rtg ? "Yes" : "No");

  // Initial draw
  draw_custom_border(csw);
  draw_bottom_buttons(csw);
  draw_color_swatches(csw);
  draw_color_table(csw);

  // Event loop
  while (handle_events(csw)) {
    WaitPort(csw->window->UserPort);
  }

  cleanup_color_swatch_window(csw);
}
