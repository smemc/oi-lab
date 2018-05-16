/*
 * Copyright (C) 2004-2007 Centro de Computacao Cientifica e Software Livre
 * Departamento de Informatica - Universidade Federal do Parana - C3SL/UFPR
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
 *   - "detect=|device/node/path" for f1..f12 and mouse button
 *   - "detect=|enter" or "detect=|esc" for enter and esc keys (no device path)
 */

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>

/* No, linux/input.h doesn't have these... */
#define EV_PRESS 1
#define EV_RELEASE 0

void read_node(unsigned char *buffer, int sock, int how_many);

int main(int argc, char *argv[])
{
  fd_set rfds;
  struct input_event ev;
  int retval, i;
  int biggest_so_far = 0;
  int fd_array[30];
  int expected_key;
  struct timeval tv;

#ifdef DEBUG
  for (i = 0; i < argc; i++)
    fprintf(stderr, "argv[%d] = %s\n", i, argv[i]);
#endif

  /* Basic checking */
  if (argc > 31)
  {
    fprintf(stderr, "read_devices: maximum is 30 arguments\n");
    fprintf(stderr, "usage: %s key files...\n", argv[0]);
    exit(1);
  }

  if (argc <= 2)
  {
    fprintf(stderr, "read_devices: few arguments\n");
    fprintf(stderr, "usage: %s key files...\n", argv[0]);
    exit(1);
  }

  expected_key = atoi(argv[1]);
  /* Timeout is different for ESC/ENTER */
  if (expected_key != 14)
    tv.tv_sec = 20;
  else
    tv.tv_sec = 5;

  tv.tv_usec = 0;

  /* Open the file and store at biggest_so_far the biggest FD between the
     * files */
  for (i = 2; i < argc; i++)
  {
    fd_array[i] = open(argv[i], O_RDONLY);

    if (fd_array[i] == -1)
    {
      perror("select (ERROR)");
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
    for (i = 2; i < argc; i++)
      FD_SET(fd_array[i], &rfds);

    retval = select(biggest_so_far + 1, &rfds, 0, 0, &tv); /* no timeout */

    /* Verify which FDs are still in the set: which ones have data to be read */
    if (retval == -1)
    {
      perror("select (ERROR)");
      exit(1);
    }
    else if (retval)
    {
      for (i = 2; i < argc; i++)
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
            printf("detect=|%s\n", argv[i]);
            exit(1);
          }

          /* f11 or f12 */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ((ev.code == KEY_F11 && expected_key == 11) ||
               (ev.code == KEY_F12 && expected_key == 12)))
          {
            printf("detect=|%s\n", argv[i]);
            exit(1);
          }

          /* left button */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ev.code == BTN_LEFT && expected_key == 13)
          {
            printf("detect=|%s\n", argv[i]);
            exit(1);
          }

          /* enter */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              (ev.code == KEY_ENTER || ev.code == KEY_KPENTER) &&
              expected_key == 14)
          {
            printf("detect=|enter\n");
            exit(1);
          }

          /* esc */
          if (ev.type == EV_KEY && ev.value == EV_PRESS &&
              ev.code == KEY_ESC && expected_key == 14)
          {
            printf("detect=|esc\n");
            exit(1);
          }
        }
      }
    }
    else
    {
      printf("detect=|timeout\n");
      exit(1);
    }
  }
  return 0;
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
