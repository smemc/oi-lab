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
#include <stdbool.h>
#include <string.h>
#include <argp.h>

#include <libudev.h>
#include <syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/input.h>

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <xcb/randr.h>
#include <cairo.h>
#include <cairo-xcb.h>

/* No, linux/input.h doesn't have these... */
#define EV_PRESS 1
#define EV_RELEASE 0

#define MAX_INPUT_DEVICES 30
#define MAX_VIDEO_DEVICES 10
#define STREQ(x, y) ((x) && (y) && strcmp((x), (y)) == 0)
#define RESIZE(x, y, s) (x) = realloc((x), sizeof(char *) * ((y) + (s)))
#define LOG_OPEN openlog(NULL, LOG_CONS | LOG_PID, LOG_USER)
#define LOG_CLOSE closelog()

#define LOG_MESSAGE(x, ...)                 \
  ({                                        \
    printf("INFO: " x "\n", ##__VA_ARGS__); \
    syslog(LOG_NOTICE, x, ##__VA_ARGS__);   \
  })

#define LOG_ERROR(x, ...)                             \
  ({                                                  \
    fprintf(stderr, "ERROR: " x "\n", ##__VA_ARGS__); \
    syslog(LOG_ERR, x, ##__VA_ARGS__);                \
  })

/* -------------- BEGIN OF ARGUMENT PARSING SECTION ------------- */
const char *argp_program_version = "oi-lab-multi-seat-config-new 1.0";
const char *argp_program_bug_address = "<laerciosousa@sme-mogidascruzes.sp.gov.br>";
static char args_doc[] = "--name WINDOW_NAME [--output OUTPUT | --geometry WIDTHxHEIGHT+X+Y]";
static char doc[] = "oi-lab-multi-seat-config-window -- a window helper for multi-seat dynamic input assignment";

static struct argp_option options[] = {
    {"output", 'o', "OUTPUT_NAME", OPTION_ARG_OPTIONAL, "Target video XRandR output"},
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
    if (state->arg_num < 1)
      argp_usage(state); /* Not enough arguments. */

    break;

  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = {options, parse_opt, args_doc, doc};
/* --------------- END OF ARGUMENT PARSING SECTION ---------------- */

/* --------------- BEGIN OF INPUT DEVICE SCANNING SECTION --------- */
struct hub_device
{
  const char *devpath;
  const char *vendor_id;
  const char *product_id;
};

struct input_device
{
  const char *devpath;
  const char *devnode;
  struct hub_device parent;
};

struct video_device
{
  const char *devpath;
  const char *devnode;
  const char *output;
};

static struct udev_device *
get_parent_hub(struct udev_device *dev)
{
  struct udev_device *parent_dev =
      udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");

  if (!parent_dev)
    return NULL;
  else if (udev_device_has_tag(parent_dev, "seat"))
    return parent_dev;
  else
    return get_parent_hub(parent_dev);
}

bool scan_udev_devices(struct input_device input_devices_list[],
                       unsigned int *input_devices_list_length,
                       struct video_device video_devices_list[],
                       unsigned int *video_devices_list_length)
{
  const char *devnode, *devpath, *syspath, *devseat;
  struct udev *udev;
  struct udev_enumerate
      *keybd_enumerate,
      *mouse_enumerate,
      *video_kms_enumerate,
      *video_sm501_enumerate;
  struct udev_list_entry
      *keybd_devices,
      *mouse_devices,
      *video_kms_devices,
      *video_sm501_devices,
      *dev_list_entry;
  struct udev_device *dev, *hub_dev;

  udev = udev_new();
  if (!udev)
  {
    LOG_ERROR("Error opening udev");
    return false;
  }

  keybd_enumerate = udev_enumerate_new(udev);
  if (!keybd_enumerate)
  {
    LOG_ERROR("Error opening keyboard enumeration");
    udev_unref(udev);
    return false;
  }

  mouse_enumerate = udev_enumerate_new(udev);
  if (!mouse_enumerate)
  {
    LOG_ERROR("Error opening mouse enumeration");
    udev_enumerate_unref(keybd_enumerate);
    udev_unref(udev);
    return false;
  }

  video_kms_enumerate = udev_enumerate_new(udev);
  if (!video_kms_enumerate)
  {
    LOG_ERROR("Error opening DRM/KMS video enumeration");
    udev_enumerate_unref(mouse_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_unref(udev);
    return false;
  }

  video_sm501_enumerate = udev_enumerate_new(udev);
  if (!video_kms_enumerate)
  {
    LOG_ERROR("Error opening SM501 video enumeration");
    udev_enumerate_unref(video_kms_enumerate);
    udev_enumerate_unref(mouse_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_unref(udev);
    return false;
  }

  if (udev_enumerate_add_match_subsystem(keybd_enumerate, "input") < 0)
    LOG_ERROR("Failed to add subsystem \"input\" to keyboard matching rules.");

  if (udev_enumerate_add_match_subsystem(mouse_enumerate, "input") < 0)
    LOG_ERROR("Failed to add subsystem \"input\" to mouse matching rules.");

  if (udev_enumerate_add_match_property(keybd_enumerate, "ID_INPUT_KEYBOARD", "1") < 0)
    LOG_ERROR("Failed to add keyboard type to keyboard matching rules.");

  if (udev_enumerate_add_match_property(mouse_enumerate, "ID_INPUT_MOUSE", "1") < 0)
    LOG_ERROR("Failed to add mouse type to mouse matching rules.");

  if (udev_enumerate_add_match_subsystem(video_kms_enumerate, "drm") < 0)
    LOG_ERROR("Failed to add subsystem \"drm\" to DRM/KMS video matching rules.");

  if (udev_enumerate_add_match_subsystem(video_sm501_enumerate, "platform") < 0)
    LOG_ERROR("Failed to add subsystem \"platform\" to SM501 video matching rules.");

  if (udev_enumerate_add_match_tag(video_sm501_enumerate, "master-of-seat") < 0)
    LOG_ERROR("Failed to add tag \"master-of-seat\" to SM501 video matching rules.");

  if (udev_enumerate_scan_devices(keybd_enumerate) < 0)
  {
    LOG_ERROR("Error scanning keyboard devices");
    udev_enumerate_unref(video_sm501_enumerate);
    udev_enumerate_unref(video_kms_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_enumerate_unref(mouse_enumerate);
    udev_unref(udev);
    return false;
  }

  if (udev_enumerate_scan_devices(mouse_enumerate) < 0)
  {
    LOG_ERROR("Error scanning mouse devices");
    udev_enumerate_unref(video_sm501_enumerate);
    udev_enumerate_unref(video_kms_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_enumerate_unref(mouse_enumerate);
    udev_unref(udev);
    return false;
  }

  if (udev_enumerate_scan_devices(video_kms_enumerate) < 0)
  {
    LOG_ERROR("Error scanning DRM/KMS video devices");
    udev_enumerate_unref(video_sm501_enumerate);
    udev_enumerate_unref(video_kms_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_enumerate_unref(mouse_enumerate);
    udev_unref(udev);
    return false;
  }

  if (udev_enumerate_scan_devices(video_sm501_enumerate) < 0)
  {
    LOG_ERROR("Error scanning SM501 video devices");
    udev_enumerate_unref(video_sm501_enumerate);
    udev_enumerate_unref(video_kms_enumerate);
    udev_enumerate_unref(keybd_enumerate);
    udev_enumerate_unref(mouse_enumerate);
    udev_unref(udev);
    return false;
  }

  keybd_devices = udev_enumerate_get_list_entry(keybd_enumerate);
  mouse_devices = udev_enumerate_get_list_entry(mouse_enumerate);
  video_kms_devices = udev_enumerate_get_list_entry(video_kms_enumerate);
  video_sm501_devices = udev_enumerate_get_list_entry(video_sm501_enumerate);

  udev_list_entry_foreach(dev_list_entry, keybd_devices)
  {
    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    devnode = udev_device_get_devnode(dev);
    devpath = udev_device_get_devpath(dev);

    if (devnode && starts_with(devnode, "/dev/input"))
    {
      struct input_device inputdev;
      struct hub_device parentdev;
      parentdev.devpath = NULL;
      parentdev.vendor_id = NULL;
      parentdev.product_id = NULL;

      LOG_MESSAGE("Keyboard found: %s -> %s", devnode, devpath);

      hub_dev = get_parent_hub(dev);

      if (hub_dev)
      {
        parentdev.devpath = udev_device_get_devpath(hub_dev);
        parentdev.vendor_id = udev_device_get_sysattr_value(hub_dev, "idVendor");
        parentdev.product_id = udev_device_get_sysattr_value(hub_dev, "idProduct");
      }

      inputdev.parent = parentdev;
      inputdev.devpath = devpath;
      inputdev.devnode = devnode;
      input_devices_list[*input_devices_list_length++] = inputdev;
    }

    udev_device_unref(dev);
  }

  udev_list_entry_foreach(dev_list_entry, mouse_devices)
  {
    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    devnode = udev_device_get_devnode(dev);
    devpath = udev_device_get_devpath(dev);

    if (devnode && starts_with(devnode, "/dev/input"))
    {
      struct input_device inputdev;
      struct hub_device parentdev;
      parentdev.devpath = NULL;
      parentdev.vendor_id = NULL;
      parentdev.product_id = NULL;

      LOG_MESSAGE("Mouse found: %s -> %s", devnode, devpath);

      hub_dev = get_parent_hub(dev);

      if (hub_dev)
      {
        parentdev.devpath = udev_device_get_devpath(hub_dev);
        parentdev.vendor_id = udev_device_get_sysattr_value(hub_dev, "idVendor");
        parentdev.product_id = udev_device_get_sysattr_value(hub_dev, "idProduct");
      }

      inputdev.parent = parentdev;
      inputdev.devpath = devpath;
      inputdev.devnode = devnode;
      input_devices_list[*input_devices_list_length++] = inputdev;
    }

    udev_device_unref(dev);
  }

  udev_list_entry_foreach(dev_list_entry, video_kms_devices)
  {
    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    devnode = udev_device_get_devnode(dev);
    devpath = udev_device_get_devpath(dev);

    if (devnode && starts_with(devnode, "/dev/dri"))
    {
      struct video_device videodev;

      LOG_MESSAGE("DRM/KMS video device found: %s -> %s", devnode, devpath);

      videodev.devpath = devpath;
      videodev.devnode = devnode;
      video_devices_list[*video_devices_list_length++] = videodev;
    }

    udev_device_unref(dev);
  }

  udev_list_entry_foreach(dev_list_entry, video_sm501_devices)
  {
    struct video_device videodev;

    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);
    devpath = udev_device_get_devpath(dev);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    LOG_MESSAGE("SM501 video device found: %s", devpath);

    videodev.devpath = devpath;
    videodev.output = udev_device_get_property_value(dev, "SM501_OUTPUT");
    video_devices_list[*video_devices_list_length++] = videodev;

    udev_device_unref(dev);
  }

  udev_enumerate_unref(video_sm501_enumerate);
  udev_enumerate_unref(video_kms_enumerate);
  udev_enumerate_unref(mouse_enumerate);
  udev_enumerate_unref(keybd_enumerate);
  udev_unref(udev);

  return true;
}

void read_node(unsigned char *buffer, int sock, int how_many)
{
  /* Keep calling recv until everything is received */
  int pointer = 0;
  int maximum = how_many;
  int bytes_read;

  while (pointer < how_many)
  {
    bytes_read = read(sock, (void *)(buffer + pointer), maximum);

    if (bytes_read == -1)
    {
      perror("Error reading the socket.");
      exit(1);
    }
    else if (bytes_read == 0)
    {
      fprintf(stderr, "End of file.\n");
      exit(1);
    }

    pointer += bytes_read; /* Forward pointer */
    maximum -= bytes_read;
  }
}

struct input_device
read_input_devices(struct input_device input_devices_list[],
                   int input_devices_list_length,
                   int expected_key)
{
  fd_set rfds;
  struct input_event ev;
  int retval, i;
  int biggest_so_far = 0;
  int fd_array[MAX_INPUT_DEVICES];
  struct timeval tv;

  struct input_device default_enter, default_esc, default_timeout;
  default_enter.devnode = "enter";
  default_esc.devnode = "esc";
  default_timeout.devnode = "timeout";

  /* Timeout is different for ESC/ENTER */
  if (expected_key != 14)
    tv.tv_sec = 20;
  else
    tv.tv_sec = 5;

  tv.tv_usec = 0;

  /* Open the file and store at biggest_so_far the biggest FD between the files */
  for (i = 0; i < input_devices_list_length; i++)
  {
    fd_array[i] = open(input_devices_list[i].devnode, O_RDONLY);

    if (fd_array[i] == -1)
    {
      LOG_ERROR("select (ERROR)");
      exit(1);
    }

    if (fd_array[i] > biggest_so_far)
      biggest_so_far = fd_array[i];
  }

  while (1)
  {
    /* Zero the FD set */
    FD_ZERO(&rfds);

    /* Insert each FD in the set */
    for (i = 0; i < input_devices_list_length; i++)
      FD_SET(fd_array[i], &rfds);

    retval = select(biggest_so_far + 1, &rfds, 0, 0, &tv); /* no timeout */

    /* Verify which FDs are still in the set: which ones have data to be read */
    if (retval == -1)
    {
      LOG_ERROR("select (ERROR)");
      exit(1);
    }
    else if (retval)
    {
      for (i = 0; i < input_devices_list_length; i++)
      {
        if (FD_ISSET(fd_array[i], &rfds))
        {
          /* Read from FD */
          read_node((unsigned char *)&ev, fd_array[i],
                    sizeof(struct input_event));

          /* f1..f10 */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ((ev.code - (KEY_F1) + 1) == expected_key))
            return input_devices_list[i];

          /* f11 or f12 */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ((ev.code == KEY_F11 && expected_key == 11) ||
               (ev.code == KEY_F12 && expected_key == 12)))
            return input_devices_list[i];

          /* left button */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ev.code == BTN_LEFT && expected_key == 13)
            return input_devices_list[i];

          /* enter */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) &&
              expected_key == 14)
            return default_enter;

          /* esc */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ev.code == KEY_ESC && expected_key == 14)
            return default_esc;
        }
      }
    }
    else
      return default_timeout;
  }
}

bool attach_input_device_to_seat(const char *seat_name,
                                 struct input_device detected_input_device)
{
  return true;
}
/* --------------- END OF INPUT DEVICE SCANNING SECTION ----------- */

/* --------------- BEGIN OF WINDOW CREATION SECTION --------------- */
static inline bool
check_extension(xcb_connection_t *connection,
                xcb_extension_t *extension)
{
  const xcb_query_extension_reply_t *rep =
      xcb_get_extension_data(connection, extension);
  return rep && rep->present;
}

static bool
check_randr_version(xcb_connection_t *connection,
                    int major, int minor)
{
  xcb_randr_query_version_cookie_t cookie;
  xcb_randr_query_version_reply_t *reply;
  xcb_generic_error_t *error;

  if (!check_extension(connection, &xcb_randr_id))
  {
    LOG_ERROR("Host X server does not support RANDR extension (or it's disabled).");
    return false;
  }

  cookie = xcb_randr_query_version(connection, major, minor);
  reply = xcb_randr_query_version_reply(connection, cookie, &error);

  if (!reply)
  {
    LOG_ERROR("Failed to get RandR version supported by host X server. Error code = %d.",
              error->error_code);
    free(error);
    return false;
  }
  else if (reply->major_version < major ||
           (reply->major_version == major && reply->minor_version < minor))
  {
    LOG_ERROR("Host X server doesn't support RandR %d.%d, needed for option \"--output\" usage.",
              major, minor);
    free(reply);
    return false;
  }
  else
  {
    free(reply);
    return true;
  }
}

static bool
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
    return false;

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
    LOG_ERROR("Failed to get host X server screen resources. Error code = %d.",
              error->error_code);
    free(error);
    return false;
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
      LOG_ERROR("Failed to get info for output %d. Error code = %d.",
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
          return false;
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
        return false;
      }

      free(name);
      free(output_info_r);
      free(screen_resources_r);
      return true;
    }

    free(output_info_r);
  }

  free(screen_resources_r);
  return false;
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

void create_window(const char *display_name,
                   const char *output,
                   xcb_connection_t *connections[],
                   unsigned int num_connections,
                   xcb_window_t windows[],
                   unsigned int num_windows)
{
  int x;
  int y;
  unsigned int width, height, screen_number;
  uint32_t mask;
  uint32_t values[1];
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t window;

  connection = xcb_connect(display_name, &screen_number);

  if (xcb_connection_has_error(connection))
  {
    LOG_ERROR("Cannot open display %s.", display_name);
    exit(1);
  }

  screen = xcb_aux_get_screen(connection, screen_number);

  if (output != NULL)
    get_output_geometry(connection, screen_number, output,
                        &x, &y, &width, &height);
  else
  {
    x = 0;
    y = 0;
    width = screen->width_in_pixels;
    height = screen->height_in_pixels;
  }

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

  xcb_map_window(connection, window);
  place_window(connection, window, x, y);

  connections[num_connections++] = connection;
  windows[num_windows++] = window;

  xcb_aux_sync(connection);
}
/* --------------- END OF WINDOW CREATION SECTION -------------- */

/* --------------- BEGIN OF MESSAGE WRITING SECTION ------------ */
void get_window_size(xcb_connection_t *connection,
                     xcb_window_t window,
                     unsigned int *width, unsigned int *height)
{
  xcb_get_geometry_cookie_t cookie;
  xcb_get_geometry_reply_t *reply;

  cookie = xcb_get_geometry(connection, window);
  reply = xcb_get_geometry_reply(connection, cookie, NULL);

  if (reply != NULL)
  {
    *width = reply->width;
    *height = reply->height;

    free(reply);
    reply = NULL;
  }
}

cairo_t *
set_font(xcb_connection_t *connection,
         xcb_screen_t *screen,
         xcb_window_t window,
         unsigned int width,
         unsigned int height)
{
  cairo_surface_t *surface;
  cairo_t *cr;

  surface = cairo_xcb_surface_create(connection,
                                     window,
                                     xcb_aux_find_visual_by_id(screen, screen->root_visual),
                                     width,
                                     height);
  cr = cairo_create(surface);

  cairo_select_font_face(cr, "sans-serif",
                         CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);

  cairo_set_font_size(cr, 48.0);
  cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

  return cr;
}

double
set_text_position(cairo_t *cr,
                  unsigned int string_list_length,
                  const char **string_list,
                  unsigned int first_index,
                  unsigned int width,
                  unsigned int height)
{
  int i;
  double x;
  double y;
  double y_all_extents = 0.0;
  cairo_text_extents_t extents;

  for (i = first_index; i < string_list_length; i++)
  {
    cairo_text_extents(cr, string_list[i], &extents);
    y_all_extents += (extents.height / 2 + extents.y_bearing * 2);
  }

  y_all_extents = y_all_extents / 2.0;

  cairo_text_extents(cr, string_list[1], &extents);
  x = width / 2 - (extents.width / 2 + extents.x_bearing);
  y = y_all_extents + height / 2 - (extents.height / 2 + extents.y_bearing);

  cairo_move_to(cr, x, y);

  return y;
}

void write_message(cairo_t *cr,
                   unsigned int string_list_length,
                   const char **string_list,
                   unsigned int first_index,
                   unsigned int width,
                   double text_y)
{
  int i;
  double x;
  double y = text_y;
  cairo_text_extents_t extents;

  for (i = first_index; i < string_list_length; i++)
  {
    cairo_text_extents(cr, string_list[i], &extents);
    x = width / 2 - (extents.width / 2 + extents.x_bearing);

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, string_list[i]);

    y -= (extents.height / 2 + extents.y_bearing * 2);
  }
}

int NO_main(int argc, const char *argv[])
{
  int screen_number;
  unsigned int width;
  unsigned int height;
  double y;

  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t window;
  cairo_t *cr;

  connection = xcb_connect(NULL, &screen_number);

  if (xcb_connection_has_error(connection))
  {
    LOG_ERROR("Cannot open display.");
    exit(1);
  }

  screen = xcb_aux_get_screen(connection, screen_number);
  sscanf(argv[1], "%x", &window);

  get_window_size(connection, window, &width, &height);
  xcb_clear_area(connection, 0, window, 0, 0, width, height);

  cr = set_font(connection, screen, window, width, height);
  y = set_text_position(cr, argc, argv, 2, width, height);
  write_message(cr, argc, argv, 2, width, y);

  xcb_aux_sync(connection);
  xcb_disconnect(connection);

  return 0;
}
/* --------------- END OF MESSAGE WRITING SECTION -------------- */

int main_loop(struct input_device input_devices_list[],
              int num_input_devices_list,
              int expected_key)
{
  bool is_input_device_attached = false;
  const char *seat_name = "seat0";
  struct input_device detected_input_device;

  while (!is_input_device_attached)
  {
    detected_input_device = read_input_devices(input_devices_list,
                                               num_input_devices_list,
                                               expected_key);

    if (detected_input_device.devnode == "timeout")
      continue;
    else
    {
      attach_input_device_to_seat(seat_name, detected_input_device);
      is_input_device_attached = true;
    }
  }
}

int main(int argc, char *argv[])
{
  struct input_device detected_input_devices[MAX_INPUT_DEVICES];
  struct video_device detected_video_devices[MAX_VIDEO_DEVICES];
  xcb_connection_t *connections[MAX_VIDEO_DEVICES];
  xcb_window_t windows[MAX_VIDEO_DEVICES];
  unsigned int num_detected_input_devices = 0,
               num_detected_video_devices = 0,
               num_connections = 0,
               num_windows = 0;

  const char *display_name, *output;

  struct arguments arguments;
  arguments.output = NULL;
  arguments.geometry = NULL;
  arguments.window_name = NULL;
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  if (!scan_udev_devices(detected_input_devices,
                         &num_detected_input_devices,
                         detected_video_devices,
                         &num_detected_video_devices))
  {
    LOG_ERROR("Failed to scan input/video devices!");
    exit(1);
  }

  create_window(display_name,
                output,
                connections,
                &num_connections,
                windows,
                &num_windows);

  pause();

  for (int i = 0; i < num_windows; i++) {
    xcb_destroy_window()
  }

  for (int i = 0; i < num_connections; i++)
    xcb_disconnect(connections[i]);

  return 0;
}
