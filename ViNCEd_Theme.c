/**
 * ViNCEd Theme Manager
 *
 * A utility for managing ViNCEd color themes by replacing COLOR and CURSORCOLOR
 * entries in ViNCEd preference files. Supports various input formats and converts
 * them to ViNCEd-compatible 16-bit RGB values.
 *
 * Compatible with Workbench 2.x/3.x systems using AmigaDOS conventions.
 *
 * Template: THEMEFILE,USE/S,SAVE/S,RESET/S,CHECK/S,LOAD/S,NOLOAD/S,ANSI/S,NOANSI/S
 *
 * Input format support:
 *   - 16-bit hex (0x1234) - passed through as-is
 *   - 8-bit hex (0x12) - converted to 16-bit (0x1212)
 *   - Integer 0-255 - converted to 16-bit hex
 *   - Float 0.0-1.0 - converted to 16-bit hex
 *
 * Parsing logic:
 *   1. Find first CURSORCOLOR= line (ignoring leading whitespace)
 *   2. Find next 16 COLOR= lines (ignoring leading whitespace)
 *   3. Fill missing entries with defaults
 *
 * Author: Brielle Harrison <nyteshade at gmail dot com> and a shepherded vibe-coded
 *   Claude 4 Sonnet trained on SAS/C 6.58 documentation.
 *
 * Target: AmigaOS 2.x/3.x compatibility
 *   Tested on AmigaOS 3.2.3
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <clib/exec_protos.h>
#include <clib/dos_protos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "amiga_color_window.h"

/* Program information */
#define PROG_NAME "ViNCEd_Theme"
#define PROG_VERSION "1.2"
#define PROG_DATE "23.6.2025"

/* AmigaDOS version string for 'version' command */
static char version[] = "\0$VER: " PROG_NAME " " PROG_VERSION " (" PROG_DATE ") ViNCEd Theme Manager";

/* ReadArgs template */
#define TEMPLATE "THEMEFILE,USE/S,SAVE/S,RESET/S,CHECK/S,LOAD/S,NOLOAD/S,ANSI/S,NOANSI/S,VIEW/S"

/* Maximum line length for preference files */
#define MAX_LINE_LENGTH 512
/* Maximum number of color entries we expect (CURSORCOLOR + 16 COLOR lines) */
#define MAX_COLOR_ENTRIES 17
/* Required number of COLOR lines (excluding CURSORCOLOR) */
#define REQUIRED_COLOR_LINES 16
/* Buffer size for file operations */
#define BUFFER_SIZE 8192

/* ReadArgs indices */
enum
{
  ARG_THEMEFILE,
  ARG_USE,
  ARG_SAVE,
  ARG_RESET,
  ARG_CHECK,
  ARG_LOAD,
  ARG_NOLOAD,
  ARG_ANSI,
  ARG_NOANSI,
  ARG_VIEW,
  ARG_COUNT
};

/**
 * Structure to hold override flags for color conversion
 */
typedef struct ColorOverrides
{
  BOOL override_load;       /* TRUE if LOAD/NOLOAD should be overridden */
  BOOL use_load;           /* TRUE for LOAD, FALSE for NOLOAD (when override_load is TRUE) */
  BOOL override_ansi;      /* TRUE if ANSI/NOANSI should be overridden */
  BOOL use_ansi;          /* TRUE for ANSI, FALSE for NOANSI (when override_ansi is TRUE) */
} ColorOverrides;

/**
 * Structure to hold a single color preference line
 */
typedef struct ColorEntry
{
  UBYTE *line;                    /* Complete line text */
  struct ColorEntry *next;        /* Next entry in linked list */
} ColorEntry;

/**
 * Structure to manage color entries collection
 */
typedef struct ColorList
{
  ColorEntry *first;              /* First color entry */
  ColorEntry *last;               /* Last color entry for efficient appending */
  ULONG count;                    /* Number of entries */
} ColorList;

/**
 * Convert a string to uppercase in-place for case-insensitive comparison
 *
 * @param str String to convert to uppercase
 */
VOID str_to_upper(UBYTE *str)
{
  if (!str) return;

  while (*str)
  {
    *str = toupper(*str);
    str++;
  }
}

/**
 * Check if a line starts with a given prefix, ignoring leading whitespace
 *
 * @param line Line to check (may have leading whitespace)
 * @param prefix Prefix to look for
 * @return TRUE if line starts with prefix after whitespace, FALSE otherwise
 */
BOOL line_starts_with(const UBYTE *line, const UBYTE *prefix)
{
  const UBYTE *start;
  UBYTE temp_line[MAX_LINE_LENGTH];
  UBYTE temp_prefix[256];
  ULONG prefix_len;

  if (!line || !prefix) return FALSE;

  /* Skip leading whitespace and tabs */
  start = line;
  while (*start && (*start == ' ' || *start == '\t'))
  {
    start++;
  }

  /* Make local uppercase copies for comparison */
  strncpy(temp_line, start, sizeof(temp_line) - 1);
  temp_line[sizeof(temp_line) - 1] = '\0';
  str_to_upper(temp_line);

  strncpy(temp_prefix, prefix, sizeof(temp_prefix) - 1);
  temp_prefix[sizeof(temp_prefix) - 1] = '\0';
  str_to_upper(temp_prefix);

  prefix_len = strlen(temp_prefix);
  return (BOOL)(strncmp(temp_line, temp_prefix, prefix_len) == 0);
}

/**
 * Check if a string starts with a given prefix (case-insensitive)
 *
 * @param str String to check
 * @param prefix Prefix to look for
 * @return TRUE if string starts with prefix, FALSE otherwise
 */
BOOL starts_with(const UBYTE *str, const UBYTE *prefix)
{
  UBYTE temp_str[MAX_LINE_LENGTH];
  UBYTE temp_prefix[256];

  if (!str || !prefix) return FALSE;

  /* Make local uppercase copies for comparison */
  strncpy(temp_str, str, sizeof(temp_str) - 1);
  temp_str[sizeof(temp_str) - 1] = '\0';
  str_to_upper(temp_str);

  strncpy(temp_prefix, prefix, sizeof(temp_prefix) - 1);
  temp_prefix[sizeof(temp_prefix) - 1] = '\0';
  str_to_upper(temp_prefix);

  return (BOOL)(strncmp(temp_str, temp_prefix, strlen(temp_prefix)) == 0);
}

/**
 * Convert various input formats to 16-bit RGB hex value
 *
 * @param input Input string containing color value
 * @return 16-bit RGB value (0x0000-0xFFFF)
 */
UWORD convert_to_16bit_rgb(const UBYTE *input)
{
  UBYTE clean_input[32];
  UBYTE *ptr;
  ULONG value;

  if (!input) return 0;

  /* Remove whitespace and copy to work buffer */
  ptr = clean_input;
  while (*input && ptr < clean_input + sizeof(clean_input) - 1)
  {
    if (!isspace(*input))
    {
      *ptr++ = *input;
    }
    input++;
  }
  *ptr = '\0';

  /* Check for hex prefix */
  if (starts_with(clean_input, "0x") || starts_with(clean_input, "0X"))
  {
    value = strtoul(clean_input + 2, NULL, 16);

    /* If it's an 8-bit hex value (0x00-0xFF), expand to 16-bit */
    if (value <= 0xFF)
    {
      return (UWORD)((value << 8) | value);
    }
    else
    {
      /* Already 16-bit, use as-is */
      return (UWORD)(value & 0xFFFF);
    }
  }

  /* Check if it contains a decimal point (floating point) */
  if (strchr(clean_input, '.'))
  {
    /* Simple integer-only floating point parsing for 0.0-1.0 range */
    char *dot_pos = strchr(clean_input, '.');
    ULONG int_part = 0;
    ULONG frac_part = 0;
    ULONG divisor = 1;
    char *frac_start = dot_pos + 1;
    char *p = frac_start;

    /* Get integer part */
    if (dot_pos > (char *)clean_input) {
      int_part = atol((char *)clean_input);
    }

    /* Get fractional part and divisor */
    while (*p && (*p >= '0' && *p <= '9')) {
      frac_part = frac_part * 10 + (*p - '0');
      divisor *= 10;
      p++;
    }

    /* Convert to 16-bit value */
    if (int_part >= 1) {
      return 0xFFFF;  /* 1.0 or greater */
    } else {
      /* Calculate fractional value * 65535 */
      value = (frac_part * 65535UL) / divisor;
      return (UWORD)value;
    }
  }

  /* Integer 0-255 */
  value = atol(clean_input);
  if (value > 255) value = 255;

  /* Convert 8-bit to 16-bit by duplicating high byte */
  return (UWORD)((value << 8) | value);
}

/**
 * Parse a color line and convert RGB values to ViNCEd format
 * Handles both simple format (COLOR=r,g,b) and complex format (COLOR=prefix1,prefix2,r,g,b)
 * Now supports overriding LOAD/NOLOAD and ANSI/NOANSI flags
 *
 * @param input_line Original color line from theme file
 * @param output_line Buffer to store converted line
 * @param max_output Maximum size of output buffer
 * @param overrides Pointer to override flags (can be NULL)
 * @return TRUE on success, FALSE on failure
 */
BOOL convert_color_line(const UBYTE *input_line, UBYTE *output_line, ULONG max_output, ColorOverrides *overrides)
{
  UBYTE work_line[MAX_LINE_LENGTH];
  UBYTE *equals_pos;
  UBYTE *start_pos;
  UBYTE prefix_part[256];
  UBYTE r_str[32], g_str[32], b_str[32];
  UBYTE *comma_positions[10];
  UWORD r_val, g_val, b_val;
  ULONG comma_count = 0;
  UBYTE *ptr;
  UBYTE load_flag[32] = "NOLOAD";
  UBYTE ansi_flag[32] = "NOANSI";

  if (!input_line || !output_line || max_output < 1) return FALSE;

  /* Copy input to work buffer */
  strncpy(work_line, input_line, sizeof(work_line) - 1);
  work_line[sizeof(work_line) - 1] = '\0';

  /* Find the equals sign */
  equals_pos = strchr(work_line, '=');
  if (!equals_pos) return FALSE;

  /* Find all comma positions after equals */
  ptr = equals_pos + 1;
  while (*ptr && comma_count < 10)
  {
    if (*ptr == ',')
    {
      comma_positions[comma_count] = ptr;
      comma_count++;
    }
    ptr++;
  }

  /* Need at least 2 commas for RGB triplet */
  if (comma_count < 2) return FALSE;

  /* Extract prefix (everything up to and including equals) */
  *equals_pos = '\0';
  strcpy(prefix_part, work_line);
  strcat(prefix_part, "=");

  start_pos = equals_pos + 1;

  if (comma_count == 2)
  {
    /* Simple format: COLOR=r,g,b */
    *comma_positions[0] = '\0';
    *comma_positions[1] = '\0';

    strcpy(r_str, start_pos);
    strcpy(g_str, comma_positions[0] + 1);
    strcpy(b_str, comma_positions[1] + 1);

    /* Use default flags or overrides */
    if (overrides)
    {
      if (overrides->override_load)
      {
        strcpy(load_flag, overrides->use_load ? "LOAD" : "NOLOAD");
      }
      if (overrides->override_ansi)
      {
        strcpy(ansi_flag, overrides->use_ansi ? "ANSI" : "NOANSI");
      }
    }
  }
  else if (comma_count == 4)
  {
    /* ViNCEd format: COLOR=NOLOAD,ANSI,r,g,b */
    /* RGB values are after comma positions 1, 2, and 3 */
    *comma_positions[0] = '\0';
    *comma_positions[1] = '\0';
    *comma_positions[2] = '\0';
    *comma_positions[3] = '\0';

    /* Extract the original flags */
    strcpy(load_flag, start_pos);
    strcpy(ansi_flag, comma_positions[0] + 1);

    /* Apply overrides if specified */
    if (overrides)
    {
      if (overrides->override_load)
      {
        strcpy(load_flag, overrides->use_load ? "LOAD" : "NOLOAD");
      }
      if (overrides->override_ansi)
      {
        strcpy(ansi_flag, overrides->use_ansi ? "ANSI" : "NOANSI");
      }
    }

    strcpy(r_str, comma_positions[1] + 1);
    strcpy(g_str, comma_positions[2] + 1);
    strcpy(b_str, comma_positions[3] + 1);
  }
  else
  {
    /* Other formats - take last 3 values as RGB */
    ULONG rgb_start = comma_count - 2;

    *comma_positions[rgb_start] = '\0';
    *comma_positions[rgb_start + 1] = '\0';

    strcpy(r_str, comma_positions[rgb_start - 1] + 1);
    strcpy(g_str, comma_positions[rgb_start] + 1);
    strcpy(b_str, comma_positions[rgb_start + 1] + 1);

    /* For other formats, we need to parse the existing flags if present */
    if (comma_count >= 4)
    {
      /* Assume first two values are LOAD/NOLOAD and ANSI/NOANSI */
      *comma_positions[0] = '\0';
      *comma_positions[1] = '\0';

      strcpy(load_flag, start_pos);
      strcpy(ansi_flag, comma_positions[0] + 1);

      /* Apply overrides if specified */
      if (overrides)
      {
        if (overrides->override_load)
        {
          strcpy(load_flag, overrides->use_load ? "LOAD" : "NOLOAD");
        }
        if (overrides->override_ansi)
        {
          strcpy(ansi_flag, overrides->use_ansi ? "ANSI" : "NOANSI");
        }
      }
    }
  }

  /* Remove trailing whitespace and newlines from blue component */
  {
    UBYTE *end = b_str + strlen(b_str) - 1;
    while (end >= b_str && (*end == '\n' || *end == '\r' || isspace(*end)))
    {
      *end-- = '\0';
    }
  }

  /* Convert to 16-bit RGB */
  r_val = convert_to_16bit_rgb(r_str);
  g_val = convert_to_16bit_rgb(g_str);
  b_val = convert_to_16bit_rgb(b_str);

  /* Build output line using sprintf for SAS/C compatibility */
  sprintf(output_line, "%s%s,%s,0x%04x,0x%04x,0x%04x",
          prefix_part, load_flag, ansi_flag, r_val, g_val, b_val);

  return TRUE;
}

/**
 * Initialize a ColorList structure
 *
 * @param list Pointer to ColorList to initialize
 */
VOID init_color_list(ColorList *list)
{
  if (!list) return;

  list->first = NULL;
  list->last = NULL;
  list->count = 0;
}

/**
 * Add a color entry to the list
 *
 * @param list ColorList to add entry to
 * @param line_text Text of the color line to add
 * @return TRUE on success, FALSE on failure
 */
BOOL add_color_entry(ColorList *list, const UBYTE *line_text)
{
  ColorEntry *entry;
  ULONG line_len;

  if (!list || !line_text) return FALSE;

  entry = AllocMem(sizeof(ColorEntry), MEMF_CLEAR);
  if (!entry) return FALSE;

  line_len = strlen(line_text) + 1;
  entry->line = AllocMem(line_len, MEMF_CLEAR);
  if (!entry->line)
  {
    FreeMem(entry, sizeof(ColorEntry));
    return FALSE;
  }

  strcpy(entry->line, line_text);
  entry->next = NULL;

  if (list->last)
  {
    list->last->next = entry;
    list->last = entry;
  }
  else
  {
    list->first = entry;
    list->last = entry;
  }

  list->count++;
  return TRUE;
}

/**
 * Free all memory used by a ColorList
 *
 * @param list ColorList to free
 */
VOID free_color_list(ColorList *list)
{
  ColorEntry *current, *next;

  if (!list) return;

  current = list->first;
  while (current)
  {
    next = current->next;
    if (current->line)
    {
      FreeMem(current->line, strlen(current->line) + 1);
    }
    FreeMem(current, sizeof(ColorEntry));
    current = next;
  }

  init_color_list(list);
}

/**
 * Generate default color entries (CURSORCOLOR + 16 COLOR lines)
 *
 * @param colors ColorList to populate with default entries
 * @param overrides Optional color overrides to apply
 * @return TRUE on success, FALSE on failure
 */
BOOL generate_default_colors(ColorList *colors, ColorOverrides *overrides)
{
  ULONG i;
  UBYTE color_line[MAX_LINE_LENGTH];
  UBYTE *load_flag = "NOLOAD";
  UBYTE *ansi_flag = "NOANSI";

  if (!colors) return FALSE;

  init_color_list(colors);

  /* Apply overrides if specified */
  if (overrides)
  {
    if (overrides->override_load)
    {
      load_flag = overrides->use_load ? "LOAD" : "NOLOAD";
    }
    if (overrides->override_ansi)
    {
      ansi_flag = overrides->use_ansi ? "ANSI" : "NOANSI";
    }
  }

  /* Add default cursor color */
  sprintf(color_line, "CURSORCOLOR=%s,%s,0x0000,0x0000,0x0000", load_flag, ansi_flag);
  if (!add_color_entry(colors, color_line))
  {
    return FALSE;
  }

  /* Add 16 default color entries */
  for (i = 0; i < REQUIRED_COLOR_LINES; i++)
  {
    sprintf(color_line, "COLOR=%s,%s,0x0000,0x0000,0x0000", load_flag, ansi_flag);
    if (!add_color_entry(colors, color_line))
    {
      free_color_list(colors);
      return FALSE;
    }
  }

  Printf("Generated %ld default color entries\n", colors->count);
  return TRUE;
}

/**
 * Display color entries with RGB values for checking
 *
 * @param colors ColorList containing color entries to display
 */
VOID display_color_check(ColorList *colors)
{
  ColorEntry *entry;
  ULONG line_num = 0;

  if (!colors) return;

  Printf("=== COLOR CHECK - %ld entries to be written ===\n", colors->count);

  entry = colors->first;
  while (entry)
  {
    UBYTE *line = entry->line;
    UBYTE *equals_pos;
    UWORD r_val, g_val, b_val;
    ULONG r_8bit, g_8bit, b_8bit;

    line_num++;

    /* Extract the hex values to show RGB equivalents */
    equals_pos = strchr(line, '=');
    if (equals_pos)
    {
      /* Find the three hex values (0x****) */
      UBYTE *hex1 = strstr(equals_pos, "0x");
      if (hex1)
      {
        UBYTE *hex2 = strstr(hex1 + 1, "0x");
        if (hex2)
        {
          UBYTE *hex3 = strstr(hex2 + 1, "0x");
          if (hex3)
          {
            /* Parse the hex values */
            r_val = (UWORD)strtoul(hex1 + 2, NULL, 16);
            g_val = (UWORD)strtoul(hex2 + 2, NULL, 16);
            b_val = (UWORD)strtoul(hex3 + 2, NULL, 16);

            /* Convert 16-bit to 8-bit for display */
            r_8bit = (r_val >> 8) & 0xFF;
            g_8bit = (g_val >> 8) & 0xFF;
            b_8bit = (b_val >> 8) & 0xFF;

            Printf("%2ld: %s RGB(%ld,%ld,%ld)\n",
                   line_num, line, r_8bit, g_8bit, b_8bit);
          }
          else
          {
            Printf("%2ld: %s (parse error)\n", line_num, line);
          }
        }
        else
        {
          Printf("%2ld: %s (parse error)\n", line_num, line);
        }
      }
      else
      {
        Printf("%2ld: %s (no hex values)\n", line_num, line);
      }
    }
    else
    {
      Printf("%2ld: %s (malformed)\n", line_num, line);
    }

    entry = entry->next;
  }

  Printf("=== END COLOR CHECK ===\n\n");
}

/**
 * Read color entries from a theme file and convert to ViNCEd format
 * Uses sequential parsing: first CURSORCOLOR=, then up to 16 COLOR= lines
 *
 * @param filename Path to theme file
 * @param colors ColorList to populate with converted theme colors
 * @param overrides Optional color overrides to apply
 * @return TRUE on success, FALSE on failure
 */
BOOL read_theme_file(const UBYTE *filename, ColorList *colors, ColorOverrides *overrides)
{
  BPTR file;
  UBYTE line[MAX_LINE_LENGTH];
  UBYTE converted_line[MAX_LINE_LENGTH];
  BOOL found_cursor_color = FALSE;
  ULONG color_count = 0;
  BOOL success = TRUE;

  if (!filename || !colors) return FALSE;

  file = Open((STRPTR)filename, MODE_OLDFILE);
  if (!file)
  {
    Printf("ERROR: Could not open theme file '%s'\n", filename);
    return FALSE;
  }

  init_color_list(colors);

  /* Single pass: look for CURSORCOLOR first, then COLOR lines in sequence */
  while (FGets(file, line, sizeof(line)) && success)
  {
    /* Remove trailing newline */
    ULONG len = strlen(line);
    if (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
    {
      line[len - 1] = '\0';
      if (len > 1 && line[len - 2] == '\r')
      {
        line[len - 2] = '\0';
      }
    }

    /* Check if this is a cursor color line (only if we haven't found one yet) */
    if (!found_cursor_color && line_starts_with(line, "CURSORCOLOR="))
    {
      if (convert_color_line(line, converted_line, sizeof(converted_line), overrides))
      {
        if (add_color_entry(colors, converted_line))
        {
          found_cursor_color = TRUE;
        }
        else
        {
          Printf("ERROR: Failed to add cursor color entry\n");
          success = FALSE;
          break;
        }
      }
      else
      {
        Printf("WARNING: Could not parse cursor color line: %s\n", line);
      }
    }
    /* Check if this is a color line (only after we found cursor color) */
    else if (found_cursor_color && color_count < REQUIRED_COLOR_LINES && line_starts_with(line, "COLOR="))
    {
      if (convert_color_line(line, converted_line, sizeof(converted_line), overrides))
      {
        if (add_color_entry(colors, converted_line))
        {
          color_count++;
        }
        else
        {
          Printf("ERROR: Failed to add color entry\n");
          success = FALSE;
          break;
        }
      }
      else
      {
        Printf("WARNING: Could not parse color line: %s\n", line);
      }
    }

    /* Stop if we have found cursor color and all required color lines */
    if (found_cursor_color && color_count >= REQUIRED_COLOR_LINES)
    {
      break;
    }
  }

  Close(file);

  if (!success)
  {
    free_color_list(colors);
    return FALSE;
  }

  /* Fill in missing entries with defaults */
  if (!found_cursor_color)
  {
    /* Insert default cursor color at beginning */
    ColorEntry *old_first = colors->first;
    ColorEntry *cursor_entry = AllocMem(sizeof(ColorEntry), MEMF_CLEAR);
    UBYTE default_line[MAX_LINE_LENGTH];
    UBYTE *load_flag = "NOLOAD";
    UBYTE *ansi_flag = "NOANSI";

    Printf("WARNING: No CURSORCOLOR found, using default\n");

    /* Apply overrides if specified */
    if (overrides)
    {
      if (overrides->override_load)
      {
        load_flag = overrides->use_load ? "LOAD" : "NOLOAD";
      }
      if (overrides->override_ansi)
      {
        ansi_flag = overrides->use_ansi ? "ANSI" : "NOANSI";
      }
    }

    if (cursor_entry)
    {
      ULONG line_len = strlen(default_line) + 1;
      sprintf(default_line, "CURSORCOLOR=%s,%s,0x0000,0x0000,0x0000", load_flag, ansi_flag);
      cursor_entry->line = AllocMem(line_len, MEMF_CLEAR);
      if (cursor_entry->line)
      {
        strcpy(cursor_entry->line, default_line);
        cursor_entry->next = old_first;
        colors->first = cursor_entry;
        if (!old_first)
        {
          colors->last = cursor_entry;
        }
        colors->count++;
      }
      else
      {
        FreeMem(cursor_entry, sizeof(ColorEntry));
      }
    }
  }

  /* Add default COLOR entries for any missing ones */
  while (color_count < REQUIRED_COLOR_LINES)
  {
    UBYTE default_line[MAX_LINE_LENGTH];
    UBYTE *load_flag = "NOLOAD";
    UBYTE *ansi_flag = "NOANSI";

    /* Apply overrides if specified */
    if (overrides)
    {
      if (overrides->override_load)
      {
        load_flag = overrides->use_load ? "LOAD" : "NOLOAD";
      }
      if (overrides->override_ansi)
      {
        ansi_flag = overrides->use_ansi ? "ANSI" : "NOANSI";
      }
    }

    sprintf(default_line, "COLOR=%s,%s,0x0000,0x0000,0x0000", load_flag, ansi_flag);
    if (!add_color_entry(colors, default_line))
    {
      Printf("ERROR: Failed to add default color entry\n");
      free_color_list(colors);
      return FALSE;
    }
    color_count++;
  }

  Printf("Loaded theme: %s CURSORCOLOR, %ld COLOR entries (%ld defaults added)\n",
         found_cursor_color ? "found" : "default",
         color_count,
         (found_cursor_color ? 0 : 1) + (REQUIRED_COLOR_LINES - (colors->count - (found_cursor_color ? 1 : 0))));

  return TRUE;
}

/**
 * Update a ViNCEd preferences file with new color entries
 * Replaces existing color lines in their current positions, adds new ones if missing
 *
 * @param prefs_path Path to preferences file to update
 * @param new_colors ColorList containing new color entries
 * @return TRUE on success, FALSE on failure
 */
BOOL update_prefs_file(const UBYTE *prefs_path, ColorList *new_colors)
{
  BPTR old_file, new_file;
  UBYTE line[MAX_LINE_LENGTH];
  UBYTE temp_path[256];
  ColorEntry *color_entry;
  ColorEntry *cursor_color = NULL;
  ColorEntry *color_entries[REQUIRED_COLOR_LINES];
  ULONG color_index = 0;
  BOOL success = FALSE;
  BOOL found_colors_section = FALSE;
  BOOL colors_added = FALSE;

  if (!prefs_path || !new_colors) return FALSE;

  /* Organize new colors: separate cursor color from regular colors */
  color_entry = new_colors->first;
  while (color_entry && color_index <= REQUIRED_COLOR_LINES)
  {
    if (starts_with(color_entry->line, "CURSORCOLOR="))
    {
      cursor_color = color_entry;
    }
    else if (starts_with(color_entry->line, "COLOR=") && color_index < REQUIRED_COLOR_LINES)
    {
      color_entries[color_index] = color_entry;
      color_index++;
    }
    color_entry = color_entry->next;
  }

  /* Create temporary file name */
  sprintf(temp_path, "%s.tmp", prefs_path);

  old_file = Open((STRPTR)prefs_path, MODE_OLDFILE);
  new_file = Open(temp_path, MODE_NEWFILE);

  if (!new_file)
  {
    Printf("ERROR: Could not create temporary file '%s'\n", temp_path);
    if (old_file) Close(old_file);
    return FALSE;
  }

  /* Process existing file if it exists */
  if (old_file)
  {
    ULONG current_color_index = 0;

    while (FGets(old_file, line, sizeof(line)))
    {
      /* Check for existing color lines to replace */
      if (line_starts_with(line, "CURSORCOLOR="))
      {
        /* Replace existing cursor color line */
        if (cursor_color)
        {
          FPuts(new_file, cursor_color->line);
          FPuts(new_file, "\n");
        }
        else
        {
          /* Keep original if no replacement */
          FPuts(new_file, line);
        }
      }
      else if (line_starts_with(line, "COLOR="))
      {
        /* Replace existing color line if we have a replacement */
        if (current_color_index < color_index && color_entries[current_color_index])
        {
          FPuts(new_file, color_entries[current_color_index]->line);
          FPuts(new_file, "\n");
        }
        else
        {
          /* Keep original if no replacement */
          FPuts(new_file, line);
        }
        current_color_index++;
      }
      else if (starts_with(line, ";Colors:"))
      {
        /* Keep colors section marker */
        found_colors_section = TRUE;
        FPuts(new_file, line);
      }
      else
      {
        /* Copy all other lines unchanged */
        FPuts(new_file, line);
      }
    }
    Close(old_file);

    /* Add any remaining new color entries that weren't replacements */
    if (current_color_index < color_index)
    {
      if (!found_colors_section)
      {
        FPuts(new_file, ";Colors:\n");
      }

      /* Add remaining color entries */
      while (current_color_index < color_index && color_entries[current_color_index])
      {
        FPuts(new_file, color_entries[current_color_index]->line);
        FPuts(new_file, "\n");
        current_color_index++;
      }
      colors_added = TRUE;
    }
  }
  else
  {
    /* No existing file - create new one with all entries */
    FPuts(new_file, ";Colors:\n");

    /* Add cursor color */
    if (cursor_color)
    {
      FPuts(new_file, cursor_color->line);
      FPuts(new_file, "\n");
    }

    /* Add all color entries */
    color_index = 0;
    while (color_index < REQUIRED_COLOR_LINES && color_entries[color_index])
    {
      FPuts(new_file, color_entries[color_index]->line);
      FPuts(new_file, "\n");
      color_index++;
    }
    colors_added = TRUE;
  }

  Close(new_file);

  /* Replace original file with temporary file */
  DeleteFile((STRPTR)prefs_path);
  if (Rename(temp_path, (STRPTR)prefs_path))
  {
    success = TRUE;
    Printf("Successfully updated '%s'\n", prefs_path);
  }
  else
  {
    Printf("ERROR: Could not replace original file '%s'\n", prefs_path);
    DeleteFile(temp_path);
  }

  return success;
}

/**
 * Display version information
 */
VOID show_version(VOID)
{
  Printf("%s %s (%s)\n", PROG_NAME, PROG_VERSION, PROG_DATE);
  Printf("ViNCEd Theme Manager for AmigaDOS\n");
  Printf("Converts various color formats to ViNCEd 16-bit RGB\n\n");
}

/**
 * Convert ColorList to AnsiColor array for display
 *
 * @param colors ColorList containing parsed theme colors
 * @param ansi_colors Array of 16 AnsiColor structures to fill
 * @return TRUE on success, FALSE on failure
 */
BOOL convert_to_ansi_colors(ColorList *colors, AnsiColor *ansi_colors)
{
  ColorEntry *entry;
  UBYTE *line;
  UBYTE *equals_pos;
  UBYTE *hex_start;
  ULONG r_val, g_val, b_val;
  int color_index = 0;
  int i;

  if (!colors || !ansi_colors) return FALSE;

  /* Initialize all colors to defaults first */
  for (i = 0; i < 16; i++) {
    ansi_colors[i] = default_ansi_colors[i];
  }

  /* Parse color entries, skipping CURSORCOLOR */
  entry = colors->first;
  while (entry && color_index < 16) {
    line = entry->line;

    /* Skip CURSORCOLOR entries */
    if (starts_with(line, "CURSORCOLOR=")) {
      entry = entry->next;
      continue;
    }

    /* Process COLOR entries */
    if (starts_with(line, "COLOR=")) {
      equals_pos = strchr(line, '=');
      if (equals_pos) {
        /* Find the three hex values (0x****) */
        hex_start = strstr(equals_pos, "0x");
        if (hex_start) {
          UBYTE *hex2 = strstr(hex_start + 1, "0x");
          if (hex2) {
            UBYTE *hex3 = strstr(hex2 + 1, "0x");
            if (hex3) {
              /* Parse the 16-bit hex values */
              r_val = strtoul(hex_start + 2, NULL, 16);
              g_val = strtoul(hex2 + 2, NULL, 16);
              b_val = strtoul(hex3 + 2, NULL, 16);

              /* Convert 16-bit to 8-bit */
              ansi_colors[color_index].red = (r_val >> 8) & 0xFF;
              ansi_colors[color_index].green = (g_val >> 8) & 0xFF;
              ansi_colors[color_index].blue = (b_val >> 8) & 0xFF;

              /* Check for LOAD/NOLOAD flag */
              if (strstr(line, "LOAD")) {
                ansi_colors[color_index].load_flag = TRUE;
              } else {
                ansi_colors[color_index].load_flag = FALSE;
              }

              color_index++;
            }
          }
        }
      }
    }

    entry = entry->next;
  }

  return TRUE;
}

/**
 * Display usage information
 */
VOID show_usage(VOID)
{
  show_version();
  Printf("Usage: %s [THEMEFILE] [USE] [SAVE] [RESET] [CHECK] [VIEW] [LOAD|NOLOAD] [ANSI|NOANSI]\n\n", PROG_NAME);
  Printf("THEMEFILE    - Theme file containing COLOR/CURSORCOLOR entries\n");
  Printf("USE/S        - Apply theme to ENV:ViNCEd.prefs (current session)\n");
  Printf("SAVE/S       - Apply theme to ENVARC:ViNCEd.prefs (persistent)\n");
  Printf("RESET/S      - Use default black colors (mutually exclusive)\n");
  Printf("CHECK/S      - Show parsed color entries with RGB values\n");
  Printf("VIEW/S       - Display colors in a graphical window\n");
  Printf("LOAD/S       - Force all colors to use LOAD flag\n");
  Printf("NOLOAD/S     - Force all colors to use NOLOAD flag (default)\n");
  Printf("ANSI/S       - Force all colors to use ANSI flag\n");
  Printf("NOANSI/S     - Force all colors to use NOANSI flag (default)\n\n");
  Printf("Note: LOAD/NOLOAD are mutually exclusive, as are ANSI/NOANSI.\n");
  Printf("      If neither is specified, the value from the theme file is used.\n\n");
  Printf("Input formats supported:\n");
  Printf("  0x1234     - 16-bit hex (passed through)\n");
  Printf("  0x12       - 8-bit hex (expanded to 0x1212)\n");
  Printf("  255        - Integer 0-255 (converted to 16-bit)\n");
  Printf("  0.5        - Float 0.0-1.0 (converted to 16-bit)\n\n");
  Printf("Parsing logic:\n");
  Printf("  1. Find first CURSORCOLOR= line (ignoring leading whitespace)\n");
  Printf("  2. Find next 16 COLOR= lines (ignoring leading whitespace)\n");
  Printf("  3. Fill missing entries with defaults\n\n");
  Printf("Examples:\n");
  Printf("  %s MyTheme.txt USE        Apply theme for current session\n", PROG_NAME);
  Printf("  %s MyTheme.txt SAVE       Save theme for next boot\n", PROG_NAME);
  Printf("  %s MyTheme.txt USE SAVE   Apply now and save for next boot\n", PROG_NAME);
  Printf("  %s MyTheme.txt USE LOAD   Apply theme with LOAD flag for all colors\n", PROG_NAME);
  Printf("  %s MyTheme.txt USE ANSI   Apply theme with ANSI flag for all colors\n", PROG_NAME);
  Printf("  %s RESET USE SAVE         Reset to defaults\n", PROG_NAME);
  Printf("  %s MyTheme.txt CHECK      Preview theme colors\n", PROG_NAME);
  Printf("  %s MyTheme.txt VIEW       Display theme in graphical window\n", PROG_NAME);
}

/**
 * Main program entry point using AmigaDOS conventions
 *
 * @return AmigaDOS return code
 */
int main(VOID)
{
  struct RDArgs *rdargs;
  LONG args[ARG_COUNT] = {0};
  ColorList theme_colors;
  ColorOverrides overrides;
  BOOL success = TRUE;
  LONG result = RETURN_OK;

  /* Parse command line arguments */
  rdargs = ReadArgs(TEMPLATE, args, NULL);
  if (!rdargs)
  {
    show_usage();
    return RETURN_ERROR;
  }

  /* Check for mutually exclusive LOAD/NOLOAD options */
  if (args[ARG_LOAD] && args[ARG_NOLOAD])
  {
    Printf("ERROR: LOAD and NOLOAD are mutually exclusive\n");
    FreeArgs(rdargs);
    return RETURN_ERROR;
  }

  /* Check for mutually exclusive ANSI/NOANSI options */
  if (args[ARG_ANSI] && args[ARG_NOANSI])
  {
    Printf("ERROR: ANSI and NOANSI are mutually exclusive\n");
    FreeArgs(rdargs);
    return RETURN_ERROR;
  }

  /* Set up override flags */
  overrides.override_load = (BOOL)(args[ARG_LOAD] || args[ARG_NOLOAD]);
  overrides.use_load = (BOOL)args[ARG_LOAD];
  overrides.override_ansi = (BOOL)(args[ARG_ANSI] || args[ARG_NOANSI]);
  overrides.use_ansi = (BOOL)args[ARG_ANSI];

  /* Check for mutually exclusive RESET option */
  if (args[ARG_RESET])
  {
    if (args[ARG_THEMEFILE])
    {
      Printf("ERROR: RESET and THEMEFILE are mutually exclusive\n");
      FreeArgs(rdargs);
      return RETURN_ERROR;
    }
  }
  else
  {
    /* If no RESET, require THEMEFILE */
    if (!args[ARG_THEMEFILE])
    {
      Printf("ERROR: THEMEFILE required (or use RESET)\n");
      show_usage();
      FreeArgs(rdargs);
      return RETURN_ERROR;
    }
  }

  /* Check if no action specified, default to USE (unless CHECK or VIEW only) */
  if (!args[ARG_USE] && !args[ARG_SAVE] && !args[ARG_CHECK] && !args[ARG_VIEW])
  {
    args[ARG_USE] = TRUE;
    Printf("No action specified, defaulting to USE\n");
  }

  /* Show version info */
  show_version();

  /* Show override flags if any are set */
  if (overrides.override_load || overrides.override_ansi)
  {
    Printf("Flag overrides:\n");
    if (overrides.override_load)
    {
      Printf("  - All colors will use %s\n", overrides.use_load ? "LOAD" : "NOLOAD");
    }
    if (overrides.override_ansi)
    {
      Printf("  - All colors will use %s\n", overrides.use_ansi ? "ANSI" : "NOANSI");
    }
    Printf("\n");
  }

  /* Read theme file or generate defaults */
  if (args[ARG_RESET])
  {
    if (!generate_default_colors(&theme_colors, &overrides))
    {
      Printf("ERROR: Failed to generate default colors\n");
      result = RETURN_ERROR;
      success = FALSE;
    }
  }
  else
  {
    if (!read_theme_file((UBYTE *)args[ARG_THEMEFILE], &theme_colors, &overrides))
    {
      Printf("ERROR: Failed to read theme file '%s'\n", (UBYTE *)args[ARG_THEMEFILE]);
      result = RETURN_ERROR;
      success = FALSE;
    }
  }

  /* Display color check if requested */
  if (success && args[ARG_CHECK])
  {
    display_color_check(&theme_colors);
  }

  /* Display colors in window if requested */
  if (success && args[ARG_VIEW])
  {
    AnsiColor ansi_colors[16];

    /* Convert ColorList to AnsiColor array */
    if (convert_to_ansi_colors(&theme_colors, ansi_colors))
    {
      /* Display the color window - this will block until window is closed */
      show_color_swatch_window(ansi_colors, NULL);
    }
    else
    {
      Printf("ERROR: Failed to convert colors for display\n");
      success = FALSE;
    }
  }

  /* Apply to ENV: if requested */
  if (success && args[ARG_USE])
  {
    if (!update_prefs_file("ENV:ViNCEd.prefs", &theme_colors))
    {
      Printf("ERROR: Failed to update ENV:ViNCEd.prefs\n");
      success = FALSE;
    }
  }

  /* Apply to ENVARC: if requested */
  if (success && args[ARG_SAVE])
  {
    if (!update_prefs_file("ENVARC:ViNCEd.prefs", &theme_colors))
    {
      Printf("ERROR: Failed to update ENVARC:ViNCEd.prefs\n");
      success = FALSE;
    }
  }

  /* Clean up */
  if (theme_colors.count > 0)
  {
    free_color_list(&theme_colors);
  }

  FreeArgs(rdargs);

  if (success)
  {
    Printf("Theme application completed successfully\n");
    return RETURN_OK;
  }
  else
  {
    return RETURN_ERROR;
  }
}
