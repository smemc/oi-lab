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
 *
 *
 * This program monitors evdev device nodes passed as parameters to detect the
 * F1...F12, ENTER or ESC keys pressed or the left mouse button.
 *
 * The first argument is the key that we need to check.
 *   - If between f1 and f12, the argument must be between 1 and 12
 *   - If it's the left mouse button, the argument is 13
 *   - If it's enter or esc, the argument is 14
 * The other arguments are the device node file paths
 *
 * The timeout is 20 seconds.
 * If "enter" or "esc" is requested, the timeout is 5 seconds.
 *
 * The output is:
 *   - "device/node/path" for f1..f12 and mouse button
 *   - "enter" or "esc" for enter and esc keys (no device path)
 *   - "timeout" if no key is pressed after timeout has reached
 */

#include <fcntl.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <linux/input.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MAX_STRING_LENGTH 256
#define MAX_INPUT_DEVICES 30

/* No, linux/input.h doesn't have these... */
#define EV_PRESS 1
#define EV_RELEASE 0

#define STREQ(x, y) ((x) && (y) && strcmp((x), (y)) == 0)

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

static inline bool
starts_with(const char *string, const char *prefix)
{
  if (!string)
    return false;

  size_t len_str = strlen(string);
  size_t len_pre = strlen(prefix);

  return (len_str < len_pre)
             ? false
             : strncmp(string, prefix, len_pre) == 0;
}

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
  const char *devpath, *syspath, *devnode, *devseat;
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

  if (udev_enumerate_add_match_subsystem(video_kms_enumerate, "graphics") < 0)
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
    devpath = udev_device_get_devpath(dev);
    devnode = udev_device_get_devnode(dev);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    if (starts_with(devnode, "/dev/input"))
    {
      struct input_device inputdev;
      struct hub_device parentdev;

      hub_dev = get_parent_hub(dev);

      if (hub_dev)
      {
        strcpy(parentdev.devpath, udev_device_get_devpath(hub_dev));
        strcpy(parentdev.syspath, udev_device_get_syspath(hub_dev));
        strcpy(parentdev.vendor_id, udev_device_get_sysattr_value(hub_dev, "idVendor"));
        strcpy(parentdev.product_id, udev_device_get_sysattr_value(hub_dev, "idProduct"));
      }

      inputdev.parent = parentdev;
      strcpy(inputdev.devpath, devpath);
      strcpy(inputdev.syspath, syspath);
      strcpy(inputdev.devnode, devnode);
      input_devices_list[(*input_devices_list_length)++] = inputdev;

      LOG_MESSAGE("Keyboard found: %s -> %s", inputdev.devnode, inputdev.syspath);
    }

    udev_device_unref(dev);
  }

  udev_list_entry_foreach(dev_list_entry, mouse_devices)
  {
    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);
    devpath = udev_device_get_devpath(dev);
    devnode = udev_device_get_devnode(dev);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    if (starts_with(devnode, "/dev/input"))
    {
      struct input_device inputdev;
      struct hub_device parentdev;

      hub_dev = get_parent_hub(dev);

      if (hub_dev)
      {
        strcpy(parentdev.devpath, udev_device_get_devpath(hub_dev));
        strcpy(parentdev.syspath, udev_device_get_syspath(hub_dev));
        strcpy(parentdev.vendor_id, udev_device_get_sysattr_value(hub_dev, "idVendor"));
        strcpy(parentdev.product_id, udev_device_get_sysattr_value(hub_dev, "idProduct"));
      }

      inputdev.parent = parentdev;
      strcpy(inputdev.devpath, devpath);
      strcpy(inputdev.syspath, syspath);
      strcpy(inputdev.devnode, devnode);
      input_devices_list[(*input_devices_list_length)++] = inputdev;

      LOG_MESSAGE("Mouse found: %s -> %s", inputdev.devnode, inputdev.syspath);
    }

    udev_device_unref(dev);
  }

  udev_list_entry_foreach(dev_list_entry, video_kms_devices)
  {
    syspath = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(udev, syspath);
    devpath = udev_device_get_devpath(dev);
    devnode = udev_device_get_devnode(dev);

    /* Get the ID_SEAT property of the device. Not needed for now. */
    /*
    devseat = udev_device_get_property_value(dev, "ID_SEAT");
    if (!devseat)
      devseat = "seat0";
    */

    if (starts_with(devnode, "/dev/dri") || starts_with(devnode, "/dev/fb"))
    {
      struct video_device videodev;

      strcpy(videodev.devpath, devpath);
      strcpy(videodev.syspath, syspath);
      strcpy(videodev.devnode, devnode);
      video_devices_list[(*video_devices_list_length)++] = videodev;

      LOG_MESSAGE("DRM/KMS video device found: %s -> %s", videodev.devnode, videodev.syspath);
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

    strcpy(videodev.devpath, devpath);
    strcpy(videodev.syspath, syspath);
    strcpy(videodev.output, udev_device_get_property_value(dev, "SM501_OUTPUT"));
    video_devices_list[(*video_devices_list_length)++] = videodev;

    LOG_MESSAGE("SM501 video device found: %s -> %s", videodev.devpath, videodev.output);

    udev_device_unref(dev);
  }

  udev_enumerate_unref(video_sm501_enumerate);
  udev_enumerate_unref(video_kms_enumerate);
  udev_enumerate_unref(mouse_enumerate);
  udev_enumerate_unref(keybd_enumerate);
  udev_unref(udev);

  return true;
}

static void
read_node(unsigned char *buffer, int sock, int how_many)
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
  strcpy(default_enter.devnode, "enter");
  strcpy(default_esc.devnode, "esc");
  strcpy(default_timeout.devnode, "timeout");

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
          {
            LOG_MESSAGE("F%d key press detected by keyboard %s",
                        expected_key,
                        input_devices_list[i].devnode);
            return input_devices_list[i];
          }

          /* f11 or f12 */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ((ev.code == KEY_F11 && expected_key == 11) ||
               (ev.code == KEY_F12 && expected_key == 12)))
          {
            LOG_MESSAGE("F%d key press detected by keyboard %s",
                        expected_key,
                        input_devices_list[i].devnode);
            return input_devices_list[i];
          }

          /* left button */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ev.code == BTN_LEFT && expected_key == 13)
          {
            LOG_MESSAGE("Button press detected by mouse %s",
                        input_devices_list[i].devnode);
            return input_devices_list[i];
          }

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
    {
      LOG_MESSAGE("F%d key press detection timed out", expected_key);
      return default_timeout;
    }
  }
}

bool attach_input_device_to_seat(const char *seat_name,
                                 struct input_device detected_input_device)
{
  return true;
}