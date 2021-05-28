/*
  sleep.c - determines and executes sleep procedures
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


#define SLEEP_SEC_PER_OVERFLOW (65535.0*64.0/F_CPU) // With 16-bit timer size and prescaler
#define SLEEP_COUNT_MAX (SLEEP_DURATION/SLEEP_SEC_PER_OVERFLOW)

// 0 - 255
volatile uint8_t sleep_counter;


// Inicia los contadores de reposo
// y habilita el temporalizador
static void sleep_enable() {
    sleep_counter = 0; // Reinicia el contador
    TCNT3 = 0;  // Reinicia el registro contador "timer3"
    TIMSK3 |= (1 << TOIE3); // Habilita la interuccion de desbordamiento en el timer3
}


// Deshabilita el temporalizador
static void sleep_disable() { TIMSK3 &= ~(1 << TOIE3); } // Deshabilita la interrupcion de desbordamiento


// Inicia la rutina del temporalizador de reposo
void sleep_init() {
    // Configura el "Timer 3": Interrupción del desbordamiento del contador de reposo
    // Al usar la interrupcion de desbordamiento, el temporalizador se recarga automaticamente cuando se desborda
    TCCR3B = 0; // Operacion normal. Desbordamiento
    TCCR3A = 0;
    TCCR3B = (TCCR3B & ~((1 << CS32) | (1 << CS31))) | (1 << CS30); // Para la cuenta
    TCCR3B |= (1 << CS31) | (1 << CS30); // Habilitar temporizador con preescalador 1/64.
    // ~66.8sec maximo con uint8 y 0.262 s/tick
    sleep_disable();
}

// Incrementa el contador de reposos con cada desbordamiento del temporalizador
ISR(TIMER3_OVF_vect) { sleep_counter++; }


// Inicia el temporalizador de reposo si las condiciones se cumplen.
// Cuando se alcanza,el modo de reposo se ejecuta.
static void sleep_execute() {
    // Busca el numero actual de caracteres alamcenados en el buffer del serial RX
    uint8_t rx_initial = serial_get_rx_buffer_count();

    // Habilita el contador de reposo
    sleep_enable();

    do {
        // Monitor para cualquier dato nuevo en el serial RX o eventos externos de salida.
        if ((serial_get_rx_buffer_count() > rx_initial) || sys_rt_exec_state || sys_rt_exec_alarm) {
            // Desabilita el temporalizador de reposo y retoma operaciones.
            sleep_disable();
            return;
        }
    } while (sleep_counter <= SLEEP_COUNT_MAX);

    // Se se alcanza, el contador de reposo expiro. Se ejecuta el proceso de reposo.
    // Se notifica al usuario que GRBL agoto el tiempo de espera e iniciara el modo parqueo.
    // Para salir se debe resumir o reiniciar. De otra forma, el trabajo es irrecuperable.
    report_feedback_message(MESSAGE_SLEEP_MODE);
    system_set_exec_state_flag(EXEC_SLEEP);
}


// Verifica las condiciones para iniciar el reposo.
// NOTA: Los procedimientos de reposo pueden ser de bloqueo,
// ya que Grbl no está recibiendo ninguna orden, ni se mueve.
// Por lo que, se debe verificar que los comandos de reposo no se ejecuten cuando se esta moviendo
void sleep_check() {
    // El modo de reposo solo se ejecuta si la maquina esta en un estado IDLE o HOLD
    // Y tiene cualquier componente habilitado
    // NOTA: Con anulaciones o en modo láser, no se garantiza el estado del refrigerante y el taladro.
    // Necesita supervisar y registrar directamente el estado de funcionamiento durante
    // el estacionamiento para garantizar el correcto funcionamiento.
    if (gc_state.modal.spindle || gc_state.modal.coolant) {
        if (sys.state == STATE_IDLE) {
            sleep_execute();
        } else if ((sys.state & STATE_HOLD) && (sys.suspend & SUSPEND_HOLD_COMPLETE)) {
            sleep_execute();
        } else if (sys.state == STATE_SAFETY_DOOR && (sys.suspend & SUSPEND_RETRACT_COMPLETE)) {
            sleep_execute();
        }
    }
}  
