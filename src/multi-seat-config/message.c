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

#include <xcb/xcb.h>
#include <xcb/xcb_aux.h>
#include <cairo.h>
#include <cairo-xcb.h>

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
                                     xcb_aux_find_visual_by_id(screen,
                                                               screen->root_visual),
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

void write_message(xcb_connection_t *connection,
                   xcb_screen_t *screen,
                   xcb_window_t window,
                   cairo_t *cr,
                   unsigned int width,
                   unsigned int height,
                   const char *lines[],
                   unsigned int num_lines)
{
  double x;
  double y;
  double y_all_extents = 0.0;
  cairo_text_extents_t extents;

  /* Set text position */
  for (int i = 0; i < num_lines; i++)
  {
    cairo_text_extents(cr, lines[i], &extents);
    y_all_extents += (extents.height / 2 + extents.y_bearing * 2);
  }

  y_all_extents = y_all_extents / 2.0;

  cairo_text_extents(cr, lines[0], &extents);
  x = width / 2 - (extents.width / 2 + extents.x_bearing);
  y = y_all_extents + height / 2 - (extents.height / 2 + extents.y_bearing);

  cairo_move_to(cr, x, y);

  /* Write text on screen */
  for (int i = 0; i < num_lines; i++)
  {
    cairo_text_extents(cr, lines[i], &extents);
    x = width / 2 - (extents.width / 2 + extents.x_bearing);

    cairo_move_to(cr, x, y);
    cairo_show_text(cr, lines[i]);

    y -= (extents.height / 2 + extents.y_bearing * 2);
  }
}