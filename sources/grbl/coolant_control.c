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


void coolant_init() {
    // Configura los pines como salida
    COOLANT_FLOOD_DDR |= (1 << COOLANT_FLOOD_BIT);
    COOLANT_MIST_DDR |= (1 << COOLANT_MIST_BIT);
    coolant_stop();
}


// Devuelve el estado actual de la salida de enfriamiento.
// Las anulaciones pueden alterar el estado programado.
uint8_t coolant_get_state() {
    uint8_t cl_state = COOLANT_STATE_DISABLE;
#ifdef INVERT_COOLANT_FLOOD_PIN
    if (bit_isfalse(COOLANT_FLOOD_PORT,(1 << COOLANT_FLOOD_BIT))) {
#else
    if (bit_istrue(COOLANT_FLOOD_PORT, (1 << COOLANT_FLOOD_BIT))) {
#endif
        cl_state |= COOLANT_STATE_FLOOD;
    }
#ifdef INVERT_COOLANT_MIST_PIN
    if (bit_isfalse(COOLANT_MIST_PORT,(1 << COOLANT_MIST_BIT))) {
#else
    if (bit_istrue(COOLANT_MIST_PORT, (1 << COOLANT_MIST_BIT))) {
#endif
        cl_state |= COOLANT_STATE_MIST;
    }
    return (cl_state);
}


// Llamado directamente por coolant_init() y mc_reset(),
// que puede ser a nivel de interrupción.
// No se establece la bandera de informe, pero sólo es llamado por las rutinas que no lo necesitan.
void coolant_stop() {
#ifdef INVERT_COOLANT_FLOOD_PIN
    COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#else
    COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#endif
#ifdef INVERT_COOLANT_MIST_PIN
    COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#else
    COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#endif
}


// Sólo programa principal.
// Establece inmediatamente el estado de funcionamiento de enfriamiento de FLOOD y también de enfriamiento de MIST,
// si está habilitado. También establece una bandera para reportar una actualización del estado de enfriamiento.
// Llamado por el cambio de refrigerante, restauración de estacionamiento, retracción de estacionamiento, modo de sueño,
// fin de programa del g-code, y g-code parser coolant_sync().
void coolant_set_state(uint8_t mode) {
    if (sys.abort) { return; } // Block during abort.

    if (mode & COOLANT_FLOOD_ENABLE) {
#ifdef INVERT_COOLANT_FLOOD_PIN
        COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#else
        COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#endif
    } else {
#ifdef INVERT_COOLANT_FLOOD_PIN
        COOLANT_FLOOD_PORT |= (1 << COOLANT_FLOOD_BIT);
#else
        COOLANT_FLOOD_PORT &= ~(1 << COOLANT_FLOOD_BIT);
#endif
    }

    if (mode & COOLANT_MIST_ENABLE) {
#ifdef INVERT_COOLANT_MIST_PIN
        COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#else
        COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#endif
    } else {
#ifdef INVERT_COOLANT_MIST_PIN
        COOLANT_MIST_PORT |= (1 << COOLANT_MIST_BIT);
#else
        COOLANT_MIST_PORT &= ~(1 << COOLANT_MIST_BIT);
#endif
    }

    sys.report_ovr_counter = 0; // Set to report change immediately
}


// Punto de entrada del analizador de código G para establecer el estado de enfriamiento.
// Fuerza una sincronización del buffer del planificador
// y se retira si un modo de abortar o comprobar está activo.
void coolant_sync(uint8_t mode) {
    if (sys.state == STATE_CHECK_MODE) { return; }
    protocol_buffer_synchronize(); // Ensure coolant turns on when specified in program.
    coolant_set_state(mode);
}
