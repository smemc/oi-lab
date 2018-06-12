#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/randr.h>

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
    fprintf(stderr,
            "Host X server does not support RANDR extension (or it's disabled).\n");
    return false;
  }

  cookie = xcb_randr_query_version(connection, major, minor);
  reply = xcb_randr_query_version_reply(connection, cookie, &error);

  if (!reply)
  {
    fprintf(stderr,
            "Failed to get RandR version supported by host X server. Error code = %d.\n",
            error->error_code);
    free(error);
    return false;
  }
  else if (reply->major_version < major ||
           (reply->major_version == major && reply->minor_version < minor))
  {
    fprintf(stderr,
            "Host X server doesn't support RandR %d.%d, needed for option \"--output\" usage.\n",
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

bool get_output_geometry(xcb_connection_t *connection,
                         xcb_screen_t *screen,
                         const char *output_name,
                         int *x, int *y,
                         unsigned int *width, unsigned int *height)
{
  xcb_generic_error_t *error;
  xcb_randr_get_screen_resources_cookie_t screen_resources_c;
  xcb_randr_get_screen_resources_reply_t *screen_resources_r;
  xcb_randr_output_t *outputs;
  xcb_randr_mode_info_t *available_modes;
  int available_modes_len, i, j;

  if (output_name == "" || !output_name)
  {
    *x = 0;
    *y = 0;
    *width = screen->width_in_pixels;
    *height = screen->height_in_pixels;

    return true;
  }

  if (!check_randr_version(connection, 1, 2))
    return false;

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