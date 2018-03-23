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
/* This program creates a "parent window" for xephyr      
 * and waits forever.
 * parameters: widthxheight+x+y, window name. 
 */
/*
 * Compilation hint: gcc seat-parent-window.c $(pkg-config --libs xcb-aux) -o seat-parent-window
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>

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
    xcb_void_cookie_t cookie;

    mask = XCB_CW_BACK_PIXEL;
    values[0] = screen->black_pixel;

    create_graphics_context(connection, screen);

    window = xcb_generate_id(connection);
    cookie = xcb_create_window(connection,
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

    connection = xcb_connect(NULL, &screen_number);

    if (xcb_connection_has_error(connection))
    {
        fprintf(stderr, "Cannot open display.\n");
        exit(1);
    }

    screen = xcb_aux_get_screen(connection, screen_number);

    sscanf(argv[1], "%dx%d+%d+%d", &width, &height, &x, &y);
    create_window(connection, screen, argv[2], x, y, width, height);

    xcb_aux_sync(connection);

    pause();

    xcb_disconnect(connection);

    return 0;
}
