/*
  drill_control.c - drill control methods
  Part of Grbl

  Copyright (c) 2012-2016 Sungeun K. Jeon
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

#ifndef drill_control_h
#define drill_control_h

#define DRILL_STATE_DISABLE   0  // Debe ser cero.
#define DRILL_STATE_FLOOD     PL_COND_FLAG_DRILL_FLOOD
#define DRILL_STATE_MIST      PL_COND_FLAG_DRILL_MIST


// Inicia los pines del taladro
void drill_init();

// Devuelve el estado actual de la salida del taladro.
// Las anulaciones pueden alterar el estado programado.
uint8_t get_drill_state();

// Deshabilita los pines del taladro de forma inmediata.
void stop_drill();

// Establece los pines del taladro de acuerdo al estado especificado.
void set_drill_state(uint8_t mode);

// Entidad principal de G-Code para establecer los estados del taladro.
// Verifica y ejecuta condiciones adicionales
void sync_drill(uint8_t mode);

#endif
