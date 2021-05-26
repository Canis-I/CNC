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

#include "grbl.h"


// Establece un movimiento de corrida válido recibido del parser de g-code
// Comprueba los límites suaves y ejecuta el movimiento.
uint8_t jog_execute(plan_line_data_t *pl_data, parser_block_t *gc_block)
{
  // Inicializar la estructura de datos del planificador para los movimientos de jogging.
  pl_data->feed_rate = gc_block->values.f;
  pl_data->condition |= PL_COND_FLAG_NO_FEED_OVERRIDE;
  pl_data->line_number = gc_block->values.n;

  if (bit_istrue(settings.flags,BITFLAG_SOFT_LIMIT_ENABLE)) {
    if (system_check_travel_limits(gc_block->values.xyz)) { return(STATUS_TRAVEL_EXCEEDED); }
  }

  // Comando de corrida valido. Planea, establece el estado, y ejecuta.
  mc_line(gc_block->values.xyz,pl_data);
  if (sys.state == STATE_IDLE) {
    if (plan_get_current_block() != NULL) { // Verifica si hay un bloque a ejecutar
      sys.state = STATE_JOG;
      st_prep_buffer();
      st_wake_up();  // Inicio manual. Sin estado de maquina requerido.
    }
  }

  return(STATUS_OK);
}
