/*
 * Copyright (C) 2004-2007 Centro de Computacao Cientifica e Software Livre
 * Departamento de Informatica - Universidade Federal do Parana - C3SL/UFPR
 * 
 * Copyright (C) 2018 Prefeitura de Mogi das Cruzes, SP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
 */
/* This program creates a black window
 * and waits forever.
 * parameters: RandR output target or window geometry (WIDTHxHEIGHT+X+Y), window name. 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <argp.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/randr.h>

const char *argp_program_version = "oi-lab-multi-seat-config-window 1.0";
const char *argp_program_bug_address = "<laerciosousa@sme-mogidascruzes.sp.gov.br>";
static char args_doc[] = "--name WINDOW_NAME [--output OUTPUT | --geometry WIDTHxHEIGHT+X+Y]";
static char doc[] = "oi-lab-multi-seat-config-window -- a window helper for multi-seat dynamic input assignment";

static struct argp_option options[] = {
    {"output", 'o', "OUTPUT_NAME", 0, "Target video XRandR output"},
    {"geometry", 'g', "WIDTHxHEIGHT+X+Y", 0, "Window geometry"},
    {"name", 'n', "WINDOW_NAME", 0, "Window name"},
    {0}};

struct arguments
{
  char *args[3];
  char *output, *geometry, *window_name;
};

static error_t
parse_opt(int key, char *arg, struct argp_state *state)
{
  struct arguments *arguments = state->input;

  switch (key)
  {
  case 'o':
    arguments->output = arg;
    break;
  case 'g':
    arguments->geometry = arg;
    break;
  case 'n':
    arguments->window_name = arg;
    break;

  case ARGP_KEY_ARG:
    if (state->arg_num >= 3)
      argp_usage(state); /* Too many arguments. */

    arguments->args[state->arg_num] = arg;
    break;

  case ARGP_KEY_END:
    if (state->arg_num < 3)
      argp_usage(state); /* Not enough arguments. */

    break;

  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};

static inline int
check_extension(xcb_connection_t *connection,
                xcb_extension_t *extension)
{
  const xcb_query_extension_reply_t *rep =
      xcb_get_extension_data(connection, extension);
  return rep && rep->present;
}

static int
check_randr_version(xcb_connection_t *connection,
                    int major, int minor)
{
  xcb_randr_query_version_cookie_t cookie;
  xcb_randr_query_version_reply_t *reply;
  xcb_generic_error_t *error;

  if (!check_extension(connection, &xcb_randr_id))
  {
    fprintf(stderr,
            "Host X server does not support RANDR extension (or it's disabled).\n");
    return 0;
  }

  cookie = xcb_randr_query_version(connection, major, minor);
  reply = xcb_randr_query_version_reply(connection, cookie, &error);

  if (!reply)
  {
    fprintf(stderr,
            "Failed to get RandR version supported by host X server. Error code = %d.\n",
            error->error_code);
    free(error);
    return 0;
  }
  else if (reply->major_version < major ||
           (reply->major_version == major && reply->minor_version < minor))
  {
    fprintf(stderr,
            "Host X server doesn't support RandR %d.%d, needed for option \"--output\" usage.\n",
            major, minor);
    free(reply);
    return 0;
  }
  else
  {
    free(reply);
    return 1;
  }
}

static int
get_output_geometry(xcb_connection_t *connection,
                    int screen_number,
                    const char *output_name,
                    int *x, int *y,
                    unsigned int *width, unsigned int *height)
{
  xcb_generic_error_t *error;
  xcb_screen_t *screen;
  xcb_randr_get_screen_resources_cookie_t screen_resources_c;
  xcb_randr_get_screen_resources_reply_t *screen_resources_r;
  xcb_randr_output_t *outputs;
  xcb_randr_mode_info_t *available_modes;
  int available_modes_len, i, j;

  if (!check_randr_version(connection, 1, 2))
    return 0;

  screen = xcb_aux_get_screen(connection, screen_number);

  /* Get list of outputs from screen resources */
  screen_resources_c =
      xcb_randr_get_screen_resources(connection,
                                     screen->root);
  screen_resources_r =
      xcb_randr_get_screen_resources_reply(connection,
                                           screen_resources_c,
                                           &error);

  if (!screen_resources_r)
  {
    fprintf(stderr,
            "Failed to get host X server screen resources. Error code = %d.\n",
            error->error_code);
    free(error);
    return 0;
  }

  outputs =
      xcb_randr_get_screen_resources_outputs(screen_resources_r);
  available_modes =
      xcb_randr_get_screen_resources_modes(screen_resources_r);
  available_modes_len =
      xcb_randr_get_screen_resources_modes_length(screen_resources_r);

  for (i = 0; i < screen_resources_r->num_outputs; i++)
  {
    char *name;
    int name_len;
    xcb_randr_get_output_info_cookie_t output_info_c;
    xcb_randr_get_output_info_reply_t *output_info_r;

    /* Get info from the output */
    output_info_c = xcb_randr_get_output_info(connection,
                                              outputs[i],
                                              XCB_TIME_CURRENT_TIME);
    output_info_r = xcb_randr_get_output_info_reply(connection,
                                                    output_info_c,
                                                    &error);

    if (!output_info_r)
    {
      fprintf(stderr,
              "Failed to get info for output %d. Error code = %d.\n",
              outputs[i], error->error_code);
      free(error);
      continue;
    }

    /* Get output name */
    name_len = xcb_randr_get_output_info_name_length(output_info_r);
    name = malloc(name_len + 1);
    strncpy(name,
            (char *)xcb_randr_get_output_info_name(output_info_r),
            name_len);
    name[name_len] = '\0';

    if (!strcmp(name, output_name))
    {
      /* Output found! */
      if (output_info_r->crtc != XCB_NONE)
      {
        /* Output is enabled! Get its CRTC geometry */
        xcb_randr_get_crtc_info_cookie_t crtc_info_c;
        xcb_randr_get_crtc_info_reply_t *crtc_info_r;

        crtc_info_c = xcb_randr_get_crtc_info(connection,
                                              output_info_r->crtc,
                                              XCB_TIME_CURRENT_TIME);
        crtc_info_r = xcb_randr_get_crtc_info_reply(connection,
                                                    crtc_info_c,
                                                    &error);

        if (!crtc_info_r)
        {
          fprintf(stderr,
                  "Failed to get CRTC info for output %s. Error code = %d.\n",
                  name, error->error_code);
          free(error);
          free(output_info_r);
          free(screen_resources_r);
          return 0;
        }
        else
        {
          *x = crtc_info_r->x;
          *y = crtc_info_r->y;
          *width = crtc_info_r->width;
          *height = crtc_info_r->height;
          free(crtc_info_r);
        }
      }
      else
      {
        fprintf(stderr,
                "Output %s is currently disabled or disconnected.\n",
                output_name);
        free(error);
        free(name);
        free(output_info_r);
        free(screen_resources_r);
        return 0;
      }

      free(name);
      free(output_info_r);
      free(screen_resources_r);
      return 1;
    }

    free(output_info_r);
  }

  free(screen_resources_r);
  return 0;
}

void create_graphics_context(xcb_connection_t *connection,
                             xcb_screen_t *screen)
{
  uint32_t mask;
  uint32_t values[2];
  xcb_gcontext_t gc;

  mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND;
  values[0] = screen->white_pixel;
  values[1] = screen->black_pixel;
  gc = xcb_generate_id(connection);

  xcb_create_gc(connection, gc, screen->root, mask, values);
}

void set_window_wm_name(xcb_connection_t *connection,
                        xcb_window_t window,
                        const char *name)
{
  xcb_change_property(connection,
                      XCB_PROP_MODE_REPLACE,
                      window,
                      XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING,
                      8,
                      strlen(name),
                      name);
}

void place_window(xcb_connection_t *connection,
                  xcb_window_t window,
                  int x, int y)
{
  uint32_t mask;
  uint32_t values[2];

  mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y;
  values[0] = x;
  values[1] = y;

  xcb_configure_window(connection, window, mask, values);
}

void create_window(xcb_connection_t *connection,
                   xcb_screen_t *screen,
                   const char *name,
                   int x, int y,
                   unsigned int width, unsigned int height)
{
  uint32_t mask;
  uint32_t values[1];
  xcb_window_t window;

  mask = XCB_CW_BACK_PIXEL;
  values[0] = screen->black_pixel;

  create_graphics_context(connection, screen);

  window = xcb_generate_id(connection);
  xcb_create_window(connection,
                    XCB_COPY_FROM_PARENT,
                    window,
                    screen->root,
                    0, 0,
                    width, height,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    screen->root_visual,
                    mask, values);

  set_window_wm_name(connection, window, name);

  xcb_map_window(connection, window);

  place_window(connection, window, x, y);
}

int main(int argc, char *argv[])
{
  int screen_number;
  int x;
  int y;
  unsigned int width, height;
  xcb_connection_t *connection;
  xcb_screen_t *screen;

  struct arguments arguments;
  arguments.output = NULL;
  arguments.geometry = NULL;
  arguments.window_name = NULL;
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (!arguments.window_name)
  {
    fprintf(stderr, "Missing mandatory option --window WINDOW_NAME.\n");
    exit(1);
  }

  connection = xcb_connect(NULL, &screen_number);

  if (xcb_connection_has_error(connection))
  {
    fprintf(stderr, "Cannot open display.\n");
    exit(1);
  }

  screen = xcb_aux_get_screen(connection, screen_number);

  if (arguments.output != NULL)
    get_output_geometry(connection, screen_number, arguments.output,
                        &x, &y, &width, &height);
  else if (arguments.geometry != NULL)
    sscanf(arguments.geometry,
           "%dx%d+%d+%d",
           &width, &height, &x, &y);
  else
  {
    x = 0;
    y = 0;
    width = screen->width_in_pixels;
    height = screen->height_in_pixels;
  }

  create_window(connection, screen, arguments.window_name,
                x, y, width, height);

  xcb_aux_sync(connection);

  pause();

  xcb_disconnect(connection);

  return 0;
}
