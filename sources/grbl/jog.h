/*
  jog.h - Jogging methods
  Part of Grbl

  Copyright (c) 2016 Sungeun K. Jeon
  Copyright (c) 2021 Canis I

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef jog_h
#define jog_h

#include "gcode.h"

// Los números de línea de movimiento del sistema deben ser cero.
#define JOG_LINE_NUMBER 0

// Establece un movimiento de corrida válido recibido del parser de g-code
// Comprueba los límites suaves y ejecuta el movimiento.
uint8_t jog_execute(plan_line_data_t *pl_data, parser_block_t *gc_block);

#endif
