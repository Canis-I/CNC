/*
  coolant_control.h - spindle control methods
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

#ifndef coolant_control_h
#define coolant_control_h

#define COOLANT_STATE_DISABLE   0  // Must be zero
#define COOLANT_STATE_FLOOD     PL_COND_FLAG_COOLANT_FLOOD
#define COOLANT_STATE_MIST      PL_COND_FLAG_COOLANT_MIST


// Inicia los pines de enfriamiento.
void coolant_init();

// Devuelve el estado actual de la salida de enfriamiento.
// Las anulaciones pueden alterar el estado programado.
uint8_t coolant_get_state();

// Deshabilita los pines de enfriamiento de forma inmediata.
void coolant_stop();

// Establece los pines de enfriamiento de acuerdo al estado especificado.
void coolant_set_state(uint8_t mode);

// Entidad principal de G-Code para establecer los estados de enfriamiento.
// Verifica y ejecuta condiciones adicionales
void coolant_sync(uint8_t mode);

#endif
