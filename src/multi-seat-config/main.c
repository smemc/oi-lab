#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo.h>
#include <unistd.h>

#define MAX_XCB_WINDOWS 10
#define MAX_STRING_LENGTH 128

bool get_output_geometry(xcb_connection_t *connection,
                         xcb_screen_t *screen,
                         const char *output_name,
                         int *x, int *y,
                         unsigned int *width, unsigned int *height);

xcb_window_t
create_window(xcb_connection_t *connection,
              xcb_screen_t *screen,
              char *name,
              int x, int y,
              unsigned int width, unsigned int height);

cairo_t *
set_font(xcb_connection_t *connection,
         xcb_screen_t *screen,
         xcb_window_t window,
         unsigned int width,
         unsigned int height);

void write_message(xcb_connection_t *connection,
                   xcb_screen_t *screen,
                   xcb_window_t window,
                   cairo_t *cr,
                   unsigned int width,
                   unsigned int height,
                   const char *lines[],
                   unsigned int num_lines);

struct seat_window
{
  int x;
  int y;
  unsigned int width;
  unsigned int height;
  char name[MAX_STRING_LENGTH];
  xcb_window_t id;
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  cairo_t *cr;
};

static bool
geometry_regex_match(const char *text)
{
  const char *geometry_regex = "[0-9]+x[0-9]+\\+[0-9]+\\+[0-9]+";
  regex_t reg;

  if (!text)
    return false;

  if (regcomp(&reg, geometry_regex, REG_EXTENDED | REG_NOSUB) != 0)
  {
    fprintf(stderr, "erro regcomp\n");
    exit(1);
  }

  bool result = regexec(&reg, text, 0, (regmatch_t *)NULL, 0) == 0 ? true : false;
  regfree(&reg);

  return result;
}

int main(int argc, char *argv[])
{
  struct seat_window windows[MAX_XCB_WINDOWS];

  unsigned int num_windows = 0;
  char argument[MAX_STRING_LENGTH];
  const char *lines[1];
  unsigned int num_lines = 1;
  lines[0] = "Aguarde...";

  for (int i = 1, j = 0; i < argc; i++)
  {
    int screen_number;
    char *output = NULL;
    char *geometry = NULL;
    char *running = strcpy(argument, argv[i]);
    char *display_name = strsep(&running, ",");
    char *token = strsep(&running, ",");

    do
    {
      windows[j].connection = xcb_connect(display_name, &screen_number);

      if (xcb_connection_has_error(windows[j].connection))
      {
        fprintf(stderr, "Cannot open display %s.\n", display_name);
        exit(1);
      }

      num_windows++;
      windows[j].screen = xcb_aux_get_screen(windows[j].connection, screen_number);
      sprintf(windows[j].name, "w%d", j + 1);

      if (geometry_regex_match(token))
        sscanf(token,
               "%dx%d+%d+%d",
               &windows[j].width,
               &windows[j].height,
               &windows[j].x,
               &windows[j].y);
      else
        get_output_geometry(windows[j].connection,
                            windows[j].screen,
                            token,
                            &windows[j].x,
                            &windows[j].y,
                            &windows[j].width,
                            &windows[j].height);

      windows[j].id = create_window(windows[j].connection,
                                    windows[j].screen,
                                    windows[j].name,
                                    windows[j].x,
                                    windows[j].y,
                                    windows[j].width,
                                    windows[j].height);
      windows[j].cr = set_font(windows[j].connection,
                               windows[j].screen,
                               windows[j].id,
                               windows[j].width,
                               windows[j].height);
      write_message(windows[j].connection,
                    windows[j].screen,
                    windows[j].id,
                    windows[j].cr,
                    windows[j].width,
                    windows[j].height,
                    lines,
                    num_lines);

      token = strsep(&running, ",");
      j++;
    } while (token != NULL);
  }

  for (int i = 0; i < num_windows; i++)
    xcb_aux_sync(windows[i].connection);

  /* Main loop should go here */
  sleep(60);

  for (int i = 0; i < num_windows; i++)
    xcb_disconnect(windows[i].connection);

  return 0;
}