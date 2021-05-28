/*
  coolant_control.c - coolant control methods
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

#include "grbl.h"


void drill_init() {
    // Configura los pines como salida
    DRILL_FLOOD_DDR |= (1 << DRILL_FLOOD_BIT);
    DRILL_MIST_DDR |= (1 << DRILL_MIST_BIT);
    stop_drill();
}


// Devuelve el estado actual de la salida de enfriamiento.
// Las anulaciones pueden alterar el estado programado.
uint8_t get_drill_state() {
    uint8_t cl_state = DRILL_STATE_DISABLE;
    if (bit_istrue(DRILL_FLOOD_PORT, (1 << DRILL_FLOOD_BIT))) {
        cl_state |= DRILL_STATE_FLOOD;
    }
    if (bit_istrue(DRILL_MIST_PORT, (1 << DRILL_MIST_BIT))) {
        cl_state |= DRILL_STATE_MIST;
    }
    return (cl_state);
}


// Llamado directamente por drill_init() y mc_reset(),
// que puede ser a nivel de interrupción.
// No se establece la bandera de informe, pero sólo es llamado por las rutinas que no lo necesitan.
void stop_drill() {
    DRILL_FLOOD_PORT &= ~(1 << DRILL_FLOOD_BIT);
    DRILL_MIST_PORT &= ~(1 << DRILL_MIST_BIT);
}


// Sólo programa principal.
// Establece inmediatamente el estado de funcionamiento de enfriamiento de FLOOD y también de enfriamiento de MIST,
// si está habilitado. También establece una bandera para reportar una actualización del estado de enfriamiento.
// Llamado por el cambio de refrigerante, restauración de estacionamiento, retracción de estacionamiento, modo de sueño,
// fin de programa del g-code, y g-code parser sync_drill().
void set_drill_state(uint8_t mode) {
    if (sys.abort) { return; } // Block during abort.

    if (mode & DRILL_FLOOD_ENABLE) {
        DRILL_FLOOD_PORT |= (1 << DRILL_FLOOD_BIT);
    } else {
        DRILL_FLOOD_PORT &= ~(1 << DRILL_FLOOD_BIT);
    }

    if (mode & DRILL_MIST_ENABLE) {
        DRILL_MIST_PORT |= (1 << DRILL_MIST_BIT);
    } else {
        DRILL_MIST_PORT &= ~(1 << DRILL_MIST_BIT);
    }

    sys.report_ovr_counter = 0; // Set to report change immediately
}


// Punto de entrada del analizador de código G para establecer el estado de enfriamiento.
// Fuerza una sincronización del buffer del planificador
// y se retira si un modo de abortar o comprobar está activo.
void sync_drill(uint8_t mode) {
    if (sys.state == STATE_CHECK_MODE) { return; }
    protocol_buffer_synchronize(); // Ensure coolant turns on when specified in program.
    set_drill_state(mode);
}
