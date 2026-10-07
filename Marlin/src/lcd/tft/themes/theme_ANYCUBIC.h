/**
 * Marlin 3D Printer Firmware
 * Copyright (c) 2023 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (c) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#pragma once

// Factory (Anycubic Kobra 2 Neo) TFT palette.

#define MOVE_AXIS_SCREEN  // Special "Move Axis" screen activated by touching coordinates area

// Base colors that differ from the upstream tft_color.h defaults
#undef COLOR_GREY
#define COLOR_GREY              0x4A69  // #4D4D4D
#undef COLOR_RED
#define COLOR_RED               0xF9C0  // #FF3A33
#undef COLOR_BLUE
#define COLOR_BLUE              0x445F  // #438DFF
#undef COLOR_GREEN
#define COLOR_GREEN             0x3666  // #1B4D1B

// Factory derived colors
#ifndef COLOR_BACKGROUND
  #define COLOR_BACKGROUND      COLOR_BLACK
#endif
#ifndef COLOR_SELECTION_BG
  #define COLOR_SELECTION_BG    COLOR_BLUE
#endif

#define COLOR_MENU_TEXT         COLOR_WHITE
#define COLOR_SLIDER_INACTIVE   0xA554
