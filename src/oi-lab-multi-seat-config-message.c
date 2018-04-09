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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo.h>
#include <cairo-xcb.h>

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

cairo_t *set_font(xcb_connection_t *connection,
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

double set_text_position(cairo_t *cr,
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

int main(int argc, const char *argv[])
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
        fprintf(stderr, "Cannot open display.\n");
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