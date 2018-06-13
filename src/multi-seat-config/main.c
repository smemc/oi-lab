#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/wait.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo.h>
#include <unistd.h>

#define MAX_XCB_WINDOWS 10
#define MAX_STRING_LENGTH 256
#define MAX_INPUT_DEVICES 30
#define MAX_VIDEO_DEVICES 10

#define STREQ(x, y) ((x) && (y) && strcmp((x), (y)) == 0)

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

struct hub_device
{
  char devpath[MAX_STRING_LENGTH];
  char syspath[MAX_STRING_LENGTH];
  char vendor_id[MAX_STRING_LENGTH];
  char product_id[MAX_STRING_LENGTH];
};

struct input_device
{
  char devnode[MAX_STRING_LENGTH];
  char devpath[MAX_STRING_LENGTH];
  char syspath[MAX_STRING_LENGTH];
  struct hub_device parent;
};

struct video_device
{
  char devnode[MAX_STRING_LENGTH];
  char devpath[MAX_STRING_LENGTH];
  char syspath[MAX_STRING_LENGTH];
  char output[MAX_STRING_LENGTH];
};

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

bool scan_udev_devices(struct input_device input_devices_list[],
                       unsigned int *input_devices_list_length,
                       struct video_device video_devices_list[],
                       unsigned int *video_devices_list_length);

struct input_device
read_input_devices(struct input_device input_devices_list[],
                   int input_devices_list_length,
                   int expected_key);

static bool
geometry_regex_match(const char *text)
{
  const char *geometry_regex = "[0-9]+x[0-9]+\\+[0-9]+\\+[0-9]+";
  regex_t reg;

  if (!text)
    return false;

  if (regcomp(&reg, geometry_regex, REG_EXTENDED | REG_NOSUB) != 0)
  {
    LOG_ERROR("regcomp error");
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

  struct input_device detected_input_devices[MAX_INPUT_DEVICES];
  struct video_device detected_video_devices[MAX_VIDEO_DEVICES];
  unsigned int num_detected_input_devices = 0,
               num_detected_video_devices = 0;

  LOG_OPEN;

  if (!scan_udev_devices(detected_input_devices,
                         &num_detected_input_devices,
                         detected_video_devices,
                         &num_detected_video_devices))
  {
    LOG_ERROR("Failed to scan input/video devices!");
    exit(1);
  }

  for (int i = 0; i < num_detected_input_devices; i++)
    LOG_MESSAGE("[%d] devnode=%s\n          devpath=%s\n          syspath=%s",
                i,
                detected_input_devices[i].devnode,
                detected_input_devices[i].devpath,
                detected_input_devices[i].syspath);

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
        LOG_ERROR("Cannot open display %s.", display_name);
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

  /* Start a background process for reading input devices for each expected key. */
  int max_key = 3;
  int num_unread_keys = max_key;
  pid_t child_pids[max_key];
  for (int expected_key = 1; expected_key <= max_key; expected_key++)
  {
    if ((child_pids[expected_key - 1] = fork()) == 0)
    {
      /* Here we are in a child process */
      struct input_device triggered_input_device;

      LOG_MESSAGE("Waiting for F%d key press...", expected_key);

      do
      {
        triggered_input_device =
            read_input_devices(detected_input_devices,
                               num_detected_input_devices,
                               expected_key);
      } while (STREQ(triggered_input_device.devnode, "timeout"));

      return 0;
    }
  }

  /* Wait for all child processes */
  pid_t waited_pid;
  while ((waited_pid = wait(NULL)) > 0)
  {
    for (int pressed_key = 1; pressed_key <= max_key; pressed_key++)
    {
      if (child_pids[pressed_key - 1] == waited_pid)
      {
        if (--num_unread_keys > 0)
          LOG_MESSAGE("Child PID for F%d key press terminated. Waiting for remaining %d PIDs...",
                      pressed_key,
                      num_unread_keys);
        else
          LOG_MESSAGE("Child PID for F%d key press terminated. All PIDs terminated.", pressed_key);

        break;
      }
      else if (pressed_key == max_key)
        LOG_MESSAGE("Unknown PID %d", waited_pid);
    }
  }

  for (int i = 0; i < num_windows; i++)
    xcb_disconnect(windows[i].connection);

  LOG_CLOSE;
  return 0;
}