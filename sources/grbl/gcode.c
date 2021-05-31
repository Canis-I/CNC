/*
  gcode.c - rs274/ngc parser.
  Part of Grbl

  Copyright (c) 2011-2016 Sungeun K. Jeon
  Copyright (c) 2009-2011 Simen Svale Skogsrud
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

// NOTE: Max line number is defined by the g-code standard to be 99999. It seems to be an
// arbitrary value, and some GUIs may require more. So we increased it based on a max safe
// value when converting a float (7.2 digit precision)s to an integer.
#define MAX_LINE_NUMBER 10000000
#define MAX_TOOL_NUMBER 255 // Limited by max unsigned 8-bit value

#define AXIS_COMMAND_NONE 0
#define AXIS_COMMAND_NON_MODAL 1
#define AXIS_COMMAND_MOTION_MODE 2
#define AXIS_COMMAND_TOOL_LENGTH_OFFSET 3 // *Undefined but required

// Extructura "externa" del G-Code
parser_state_t gc_state;
parser_block_t gc_block;

#define FAIL(status) return(status);

void gc_init() {
    memset(&gc_state, 0, sizeof(parser_state_t));

    // Carga las coordenadas predefinidas del sistema G54
    if (!(settings_read_coord_data(gc_state.modal.coord_select, gc_state.coord_system))) {
        report_status_message(STATUS_SETTING_READ_FAIL);
    }
}

//Asigna la posicion del G-Code en mm.
void gc_sync_position() {
    system_convert_array_steps_to_mpos(gc_state.position, sys_position);
}

// Ejecuta una linea de un codigo terminado en 0. Se supone que la linea contiene solo letras mayusculas
// y valores de punto flotante. Las lineas de comentario se eliminan. Enn esta funcion, todas las
// unidades y posiciones son ceonvertidad y exportadas a coordenadas absolutas.
// O instrucciones puras de Grbl.
uint8_t gc_execute_line(char *line) {
    // Inicializa el bloque de analisis y copia los estados actuales de G-Code

    memset(&gc_block, 0, sizeof(parser_block_t)); // Initialize the parser block struct.
    memcpy(&gc_block.modal, &gc_state.modal, sizeof(gc_modal_t)); // Copy current modes

    uint8_t axis_command = AXIS_COMMAND_NONE;
    uint8_t axis_0, axis_1, axis_linear;
    uint8_t coord_select = 0; // Pista G10 Seleccion de coordenadas P para ejecucion

    // Inicializar las variables de seguimiento de bitflag para las operaciones
    // compatibles con los índices de los ejes.
    uint8_t axis_words = 0; // Seguimiento de XYZ
    uint8_t ijk_words = 0; // Seguimento de IJK

    // Inicia los comandos y valores y variables de bandera.
    uint16_t command_words = 0; // Sigue los comandos G y M.
    uint16_t value_words = 0; // seguimiento de lsd palabras de valor.
    uint8_t gc_parser_flags = GC_PARSER_NONE;

    // Determina si la linea es un movimiento de corrida o un bloque normal de G-Code.
    if (line[0] == '$') {
        gc_parser_flags |= GC_PARSER_JOG_MOTION;
        gc_block.modal.motion = MOTION_MODE_LINEAR;
        gc_block.modal.feed_rate = FEED_RATE_MODE_UNITS_PER_MIN;
        gc_block.values.n = JOG_LINE_NUMBER; // Initialize default line number reported during jog.
    }

    // Todos los comandos importantes de G-Code van seguidos de una letra, se analiza.
    uint8_t word_bit; // Asigna seguimiento de valores.
    uint8_t char_counter;
    char letter;
    float value;
    uint8_t int_value = 0;
    uint16_t mantissa = 0;
    if (gc_parser_flags & GC_PARSER_JOG_MOTION) { char_counter = 3; } // Analiza despues de un $J=
    else { char_counter = 0; }

    while (line[char_counter] != 0) { // El ciclo termina cuando no hay mas palabras.

        // Importa la sigueinte palabra de G-Code
        // Se espera una letra seguida de un valor, de otra forma se emite un error.
        letter = line[char_counter];
        // Se espera una letra.
        if ((letter < 'A') || (letter > 'Z')) { FAIL(STATUS_EXPECTED_COMMAND_LETTER); }

        char_counter++;
        // Se espera el valor de la letra.
        if (!read_float(line, &char_counter, &value)) { FAIL(STATUS_BAD_NUMBER_FORMAT); }

        // Convierte los numeros a un valor significativo de 8 bits enteros y extrae la mantissa
        int_value = trunc(value);
        mantissa = round(100 * (value - int_value)); // Calcula la Mantisaa para valores G0.000

        // Valida si la palabra de G-Code es soportada o si existen errores para ese grupo de comandos.
        // Si esta bien, alamcena el comando.
        switch (letter) {
            case 'G':
                // Si es el commando G, identifica su grupo.
                switch (int_value) {
                    case 10:
                    case 28:
                    case 30:
                    case 92:
                        if (mantissa == 0) { // Ignora comandos decimales
                            if (axis_command) {
                                FAIL(STATUS_GCODE_AXIS_COMMAND_CONFLICT);
                            } // [Conflicto con el eje]
                            axis_command = AXIS_COMMAND_NON_MODAL;
                        }
                        // Continua el analisis.
                    case 4:
                    case 53:
                        word_bit = MODAL_GROUP_G0;
                        gc_block.non_modal_command = int_value;
                        if ((int_value == 28) || (int_value == 30) || (int_value == 92)) {
                            if (!((mantissa == 0) || (mantissa == 10))) { FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); }
                            gc_block.non_modal_command += mantissa;
                            mantissa = 0; // Se asigna 0 para indicar que es un no-entero valido.
                        }
                        break;
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                    case 38:
                        // Verifica si G0/1/2/38 fueron llamados junto a G10/28/30/92
                        if (axis_command) { FAIL(STATUS_GCODE_AXIS_COMMAND_CONFLICT); } // [Conflicto con el eje]
                        axis_command = AXIS_COMMAND_MOTION_MODE;
                        // Continua
                    case 80:
                        word_bit = MODAL_GROUP_G1;
                        gc_block.modal.motion = int_value;
                        if (int_value == 38) {
                            if (!((mantissa == 20) || (mantissa == 30) || (mantissa == 40) || (mantissa == 50))) {
                                FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); // [El comando G38.x no esta soportado]
                            }
                            gc_block.modal.motion += (mantissa / 10) + 100;
                            mantissa = 0; // Se asigna 0 para indicar que es un no-entero valido.
                        }
                        break;
                    case 17:
                    case 18:
                    case 19:
                        word_bit = MODAL_GROUP_G2;
                        gc_block.modal.plane_select = int_value - 17;
                        break;
                    case 90:
                    case 91:
                        if (mantissa == 0) {
                            word_bit = MODAL_GROUP_G3;
                            gc_block.modal.distance = int_value - 90;
                        } else {
                            word_bit = MODAL_GROUP_G4;
                            if ((mantissa != 10) || (int_value == 90)) {
                                FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
                            } // [G90.1 no soportado]
                            mantissa = 0; // Se asigna 0 para indicar que es un no-entero valido.
                            // De lo contrario, el modo incremental del arco IJK es el predeterminado.
                            // G91.1 no hace nada.
                        }
                        break;
                    case 93:
                    case 94:
                        word_bit = MODAL_GROUP_G5;
                        gc_block.modal.feed_rate = 94 - int_value;
                        break;
                    case 20:
                    case 21:
                        word_bit = MODAL_GROUP_G6;
                        gc_block.modal.units = 21 - int_value;
                        break;
                    case 40:
                        word_bit = MODAL_GROUP_G7;
                        break;
                    case 43:
                    case 49:
                        word_bit = MODAL_GROUP_G8;
                        // NOTE: The NIST g-code standard vaguely states that when a tool length offset is changed,
                        // there cannot be any axis motion or coordinate offsets updated. Meaning G43, G43.1, and G49
                        // all are explicit axis commands, regardless if they require axis words or not.
                        if (axis_command) {
                            FAIL(STATUS_GCODE_AXIS_COMMAND_CONFLICT);
                        } // [Axis word/command conflict] }
                        axis_command = AXIS_COMMAND_TOOL_LENGTH_OFFSET;
                        if (int_value == 49) { // G49
                            gc_block.modal.tool_length = TOOL_LENGTH_OFFSET_CANCEL;
                        } else if (mantissa == 10) { // G43.1
                            gc_block.modal.tool_length = TOOL_LENGTH_OFFSET_ENABLE_DYNAMIC;
                        } else { FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); } // [Unsupported G43.x command]
                        mantissa = 0; // Set to zero to indicate valid non-integer G command.
                        break;
                    case 54:
                    case 55:
                    case 56:
                    case 57:
                    case 58:
                    case 59:
                        // NOTE: G59.x are not supported. (But their int_values would be 60, 61, and 62.)
                        word_bit = MODAL_GROUP_G12;
                        gc_block.modal.coord_select = int_value - 54; // Shift to array indexing.
                        break;
                    case 61:
                        word_bit = MODAL_GROUP_G13;
                        if (mantissa != 0) { FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); } // [G61.1 no soportado]
                        break;
                    default:
                        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); // [Comando G no soportado]
                }
                if (mantissa > 0) {
                    FAIL(STATUS_GCODE_COMMAND_VALUE_NOT_INTEGER);
                } // [Comando invalido o no soportado Gxx.x]

                // Verifica si existe mas de un comando en el bloque actual, para emitir un codigo de violacion
                // La variable 'word_bit' esta asignada, si el comando es valido.
                if (bit_istrue(command_words, bit(word_bit))) { FAIL(STATUS_GCODE_MODAL_GROUP_VIOLATION); }
                command_words |= bit(word_bit);
                break;

                // Si es el commando M, identifica su grupo.
            case 'M':
                if (mantissa > 0) { FAIL(STATUS_GCODE_COMMAND_VALUE_NOT_INTEGER); } // [invalido Mxx.x]
                switch (int_value) {
                    case 0:
                    case 1:
                    case 2:
                    case 30:
                        word_bit = MODAL_GROUP_M4;
                        switch (int_value) {
                            case 0:
                                gc_block.modal.program_flow = PROGRAM_FLOW_PAUSED;
                                break; // Programa pausado
                            case 1:
                                break; // Paro opcional no esta soportado.
                            default:
                                gc_block.modal.program_flow = int_value; // Fin del programa
                        }
                        break;
                    case 3:
                    case 4:
                    case 5:
                        word_bit = MODAL_GROUP_M7;
                        switch (int_value) {
                            case 3:
                                gc_block.modal.spindle = SPINDLE_ENABLE_CW;
                                break;
                            case 4:
                                gc_block.modal.spindle = SPINDLE_ENABLE_CCW;
                                break;
                            case 5:
                                gc_block.modal.spindle = SPINDLE_DISABLE;
                                break;
                        }
                        break;
                    case 7:
                    case 8:
                    case 9:
                        word_bit = MODAL_GROUP_M8;
                        switch (int_value) {
                            case 7:
                                gc_block.modal.coolant |= DRILL_MIST_ENABLE;
                                break;
                            case 8:
                                gc_block.modal.coolant |= DRILL_FLOOD_ENABLE;
                                break;
                            case 9:
                                gc_block.modal.coolant = DRILL_DISABLE;
                                break; // M9 deshabilita ambos M7 y M8.
                        }
                        break;
                    default:
                        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); // [Comando M no soportado]
                }

                // Verifica si existe mas de un comando en el bloque actual, para emitir un codigo de violacion
                // La variable 'word_bit' esta asignada, si el comando es valido.
                if (bit_istrue(command_words, bit(word_bit))) { FAIL(STATUS_GCODE_MODAL_GROUP_VIOLATION); }
                command_words |= bit(word_bit);
                break;

            default:
                switch (letter) {
                    case 'F':
                        word_bit = WORD_F;
                        gc_block.values.f = value;
                        break;
                    case 'I':
                        word_bit = WORD_I;
                        gc_block.values.ijk[X_AXIS] = value;
                        ijk_words |= (1 << X_AXIS);
                        break;
                    case 'J':
                        word_bit = WORD_J;
                        gc_block.values.ijk[Y_AXIS] = value;
                        ijk_words |= (1 << Y_AXIS);
                        break;
                    case 'K':
                        word_bit = WORD_K;
                        gc_block.values.ijk[Z_AXIS] = value;
                        ijk_words |= (1 << Z_AXIS);
                        break;
                    case 'L':
                        word_bit = WORD_L;
                        gc_block.values.l = int_value;
                        break;
                    case 'N':
                        word_bit = WORD_N;
                        gc_block.values.n = trunc(value);
                        break;
                    case 'P':
                        word_bit = WORD_P;
                        gc_block.values.p = value;
                        break;
                        // En algunos casos, P deberia ser entero, pero esos comandos no estan soportados XD.
                    case 'R':
                        word_bit = WORD_R;
                        gc_block.values.r = value;
                        break;
                    case 'S':
                        word_bit = WORD_S;
                        gc_block.values.s = value;
                        break;
                    case 'T':
                        word_bit = WORD_T;
                        if (value > MAX_TOOL_NUMBER) { FAIL(STATUS_GCODE_MAX_VALUE_EXCEEDED); }
                        gc_block.values.t = int_value;
                        break;
                    case 'X':
                        word_bit = WORD_X;
                        gc_block.values.xyz[X_AXIS] = value;
                        axis_words |= (1 << X_AXIS);
                        break;
                    case 'Y':
                        word_bit = WORD_Y;
                        gc_block.values.xyz[Y_AXIS] = value;
                        axis_words |= (1 << Y_AXIS);
                        break;
                    case 'Z':
                        word_bit = WORD_Z;
                        gc_block.values.xyz[Z_AXIS] = value;
                        axis_words |= (1 << Z_AXIS);
                        break;
                    default:
                        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
                }

                // Verifica si existe mas de un comando en el bloque actual, para emitir un codigo de violacion
                // La variable 'word_bit' esta asignada, si el comando es valido.
                if (bit_istrue(value_words, bit(word_bit))) { FAIL(STATUS_GCODE_WORD_REPEATED); } // [Word repeated]
                // Check for invalid negative values for words F, N, P, T, and S.
                // NOTE: Negative value check is done here simply for code-efficiency.
                if (bit(word_bit) & (bit(WORD_F) | bit(WORD_N) | bit(WORD_P) | bit(WORD_T) | bit(WORD_S))) {
                    if (value < 0.0) { FAIL(STATUS_NEGATIVE_VALUE); } // [Word value cannot be negative]
                }
                value_words |= bit(word_bit); // Flag to indicate parameter assigned.

        }
    }
    // Determine implicit axis command conditions. Axis words have been passed, but no explicit axis
    // command has been sent. If so, set axis command to current motion mode.
    if (axis_words) {
        if (!axis_command) { axis_command = AXIS_COMMAND_MOTION_MODE; } // Assign implicit motion-mode
    }

    // Check for valid line number N value.
    if (bit_istrue(value_words, bit(WORD_N))) {
        // Line number value cannot be less than zero (done) or greater than max line number.
        if (gc_block.values.n > MAX_LINE_NUMBER) {
            FAIL(STATUS_GCODE_INVALID_LINE_NUMBER);
        } // [Exceeds max line number]
    }
    // bit_false(value_words,bit(WORD_N)); // NOTE: Single-meaning value word. Set at end of error-checking.

    // Track for unused words at the end of error-checking.
    // NOTE: Single-meaning value words are removed all at once at the end of error-checking, because
    // they are always used when present. This was done to save a few bytes of flash. For clarity, the
    // single-meaning value words may be removed as they are used. Also, axis words are treated in the
    // same way. If there is an explicit/implicit axis command, XYZ words are always used and are
    // are removed at the end of error-checking.

    // [1. Comments ]: MSG's NOT SUPPORTED. Comment handling performed by protocol.

    // [2. Set feed rate mode ]: G93 F word missing with G1,G2/3 active, implicitly or explicitly. Feed rate
    //   is not defined after switching to G94 from G93.
    // NOTE: For jogging, ignore prior feed rate mode. Enforce G94 and check for required F word.
    if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
        if (bit_isfalse(value_words, bit(WORD_F))) { FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE); }
        if (gc_block.modal.units == UNITS_MODE_INCHES) { gc_block.values.f *= MM_PER_INCH; }
    } else {
        if (gc_block.modal.feed_rate == FEED_RATE_MODE_INVERSE_TIME) { // = G93
            // NOTE: G38 can also operate in inverse time, but is undefined as an error. Missing F word check added here.
            if (axis_command == AXIS_COMMAND_MOTION_MODE) {
                if ((gc_block.modal.motion != MOTION_MODE_NONE) && (gc_block.modal.motion != MOTION_MODE_SEEK)) {
                    if (bit_isfalse(value_words, bit(WORD_F))) {
                        FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE);
                    } // [F word missing]
                }
            }
            // NOTE: It seems redundant to check for an F word to be passed after switching from G94 to G93. We would
            // accomplish the exact same thing if the feed rate value is always reset to zero and undefined after each
            // inverse time block, since the commands that use this value already perform undefined checks. This would
            // also allow other commands, following this switch, to execute and not error out needlessly. This code is
            // combined with the above feed rate mode and the below set feed rate error-checking.

            // [3. Set feed rate ]: F is negative (done.)
            // - In inverse time mode: Always implicitly zero the feed rate value before and after block completion.
            // NOTE: If in G93 mode or switched into it from G94, just keep F value as initialized zero or passed F word
            // value in the block. If no F word is passed with a motion command that requires a feed rate, this will error
            // out in the motion modes error-checking. However, if no F word is passed with NO motion command that requires
            // a feed rate, we simply move on and the state feed rate value gets updated to zero and remains undefined.
        } else { // = G94
            // - In units per mm mode: If F word passed, ensure value is in mm/min, otherwise push last state value.
            if (gc_state.modal.feed_rate == FEED_RATE_MODE_UNITS_PER_MIN) { // Last state is also G94
                if (bit_istrue(value_words, bit(WORD_F))) {
                    if (gc_block.modal.units == UNITS_MODE_INCHES) { gc_block.values.f *= MM_PER_INCH; }
                } else {
                    gc_block.values.f = gc_state.feed_rate; // Push last state feed rate
                }
            } // Else, switching to G94 from G93, so don't push last state feed rate. Its undefined or the passed F word value.
        }
    }
    // bit_false(value_words,bit(WORD_F)); // NOTE: Single-meaning value word. Set at end of error-checking.

    // [4. Set spindle speed ]: S is negative (done.)
    if (bit_isfalse(value_words, bit(WORD_S))) { gc_block.values.s = gc_state.spindle_speed; }
    // bit_false(value_words,bit(WORD_S)); // NOTE: Single-meaning value word. Set at end of error-checking.

    // [5. Select tool ]: NOT SUPPORTED. Only tracks value. T is negative (done.) Not an integer. Greater than max tool value.
    // bit_false(value_words,bit(WORD_T)); // NOTE: Single-meaning value word. Set at end of error-checking.

    // [6. Change tool ]: N/A
    // [7. Spindle control ]: N/A
    // [8. Coolant control ]: N/A
    // [9. Override control ]: Not supported except for a Grbl-only parking motion override control.

    // [10. Dwell ]: P value missing. P is negative (done.) NOTE: See below.
    if (gc_block.non_modal_command == NON_MODAL_DWELL) {
        if (bit_isfalse(value_words, bit(WORD_P))) { FAIL(STATUS_GCODE_VALUE_WORD_MISSING); } // [P word missing]
        bit_false(value_words, bit(WORD_P));
    }

    // [11. Set active plane ]: N/A
    switch (gc_block.modal.plane_select) {
        case PLANE_SELECT_XY:
            axis_0 = X_AXIS;
            axis_1 = Y_AXIS;
            axis_linear = Z_AXIS;
            break;
        case PLANE_SELECT_ZX:
            axis_0 = Z_AXIS;
            axis_1 = X_AXIS;
            axis_linear = Y_AXIS;
            break;
        default: // case PLANE_SELECT_YZ:
            axis_0 = Y_AXIS;
            axis_1 = Z_AXIS;
            axis_linear = X_AXIS;
    }

    // [12. Set length units ]: N/A
    // Pre-convert XYZ coordinate values to millimeters, if applicable.
    uint8_t idx;
    if (gc_block.modal.units == UNITS_MODE_INCHES) {
        for (idx = 0; idx < N_AXIS; idx++) { // Axes indices are consistent, so loop may be used.
            if (bit_istrue(axis_words, bit(idx))) {
                gc_block.values.xyz[idx] *= MM_PER_INCH;
            }
        }
    }

    // [13. Cutter radius compensation ]: G41/42 NOT SUPPORTED. Error, if enabled while G53 is active.
    // [G40 Errors]: G2/3 arc is programmed after a G40. The linear move after disabling is less than tool diameter.
    //   NOTE: Since cutter radius compensation is never enabled, these G40 errors don't apply. Grbl supports G40
    //   only for the purpose to not error when G40 is sent with a g-code program header to setup the default modes.

    // [14. Cutter length compensation ]: G43 NOT SUPPORTED, but G43.1 and G49 are.
    // [G43.1 Errors]: Motion command in same line.
    //   NOTE: Although not explicitly stated so, G43.1 should be applied to only one valid
    //   axis that is configured (in config.h). There should be an error if the configured axis
    //   is absent or if any of the other axis words are present.
    if (axis_command == AXIS_COMMAND_TOOL_LENGTH_OFFSET) { // Indicates called in block.
        if (gc_block.modal.tool_length == TOOL_LENGTH_OFFSET_ENABLE_DYNAMIC) {
            if (axis_words ^ (1 << TOOL_LENGTH_OFFSET_AXIS)) { FAIL(STATUS_GCODE_G43_DYNAMIC_AXIS_ERROR); }
        }
    }

    // [15. Coordinate system selection ]: *N/A. Error, if cutter radius comp is active.
    // TODO: An EEPROM read of the coordinate data may require a buffer sync when the cycle
    // is active. The read pauses the processor temporarily and may cause a rare crash. For
    // future versions on processors with enough memory, all coordinate data should be stored
    // in memory and written to EEPROM only when there is not a cycle active.
    float block_coord_system[N_AXIS];
    memcpy(block_coord_system, gc_state.coord_system, sizeof(gc_state.coord_system));
    if (bit_istrue(command_words, bit(MODAL_GROUP_G12))) { // Check if called in block
        if (gc_block.modal.coord_select > N_COORDINATE_SYSTEM) {
            FAIL(STATUS_GCODE_UNSUPPORTED_COORD_SYS);
        } // [Greater than N sys]
        if (gc_state.modal.coord_select != gc_block.modal.coord_select) {
            if (!(settings_read_coord_data(gc_block.modal.coord_select, block_coord_system))) {
                FAIL(STATUS_SETTING_READ_FAIL);
            }
        }
    }

    // [16. Set path control mode ]: N/A. Only G61. G61.1 and G64 NOT SUPPORTED.
    // [17. Set distance mode ]: N/A. Only G91.1. G90.1 NOT SUPPORTED.
    // [18. Set retract mode ]: NOT SUPPORTED.

    // [19. Remaining non-modal actions ]: Check go to predefined position, set G10, or set axis offsets.
    // NOTE: We need to separate the non-modal commands that are axis word-using (G10/G28/G30/G92), as these
    // commands all treat axis words differently. G10 as absolute offsets or computes current position as
    // the axis value, G92 similarly to G10 L20, and G28/30 as an intermediate target position that observes
    // all the current coordinate system and G92 offsets.
    switch (gc_block.non_modal_command) {
        case NON_MODAL_SET_COORDINATE_DATA:
            // [G10 Errors]: L missing and is not 2 or 20. P word missing. (Negative P value done.)
            // [G10 L2 Errors]: R word NOT SUPPORTED. P value not 0 to nCoordSys(max 9). Axis words missing.
            // [G10 L20 Errors]: P must be 0 to nCoordSys(max 9). Axis words missing.
            if (!axis_words) { FAIL(STATUS_GCODE_NO_AXIS_WORDS) }; // [No axis words]
            if (bit_isfalse(value_words, ((1 << WORD_P) | (1 << WORD_L)))) {
                FAIL(STATUS_GCODE_VALUE_WORD_MISSING);
            } // [P/L word missing]
            coord_select = trunc(gc_block.values.p); // Convert p value to int.
            if (coord_select > N_COORDINATE_SYSTEM) {
                FAIL(STATUS_GCODE_UNSUPPORTED_COORD_SYS);
            } // [Greater than N sys]
            if (gc_block.values.l != 20) {
                if (gc_block.values.l == 2) {
                    if (bit_istrue(value_words, bit(WORD_R))) {
                        FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND);
                    } // [G10 L2 R not supported]
                } else { FAIL(STATUS_GCODE_UNSUPPORTED_COMMAND); } // [Unsupported L]
            }
            bit_false(value_words, (bit(WORD_L) | bit(WORD_P)));

            // Determine coordinate system to change and try to load from EEPROM.
            if (coord_select > 0) { coord_select--; } // Adjust P1-P6 index to EEPROM coordinate data indexing.
            else { coord_select = gc_block.modal.coord_select; } // Index P0 as the active coordinate system

            // NOTE: Store parameter data in IJK values. By rule, they are not in use with this command.
            if (!settings_read_coord_data(coord_select, gc_block.values.ijk)) {
                FAIL(STATUS_SETTING_READ_FAIL);
            } // [EEPROM read fail]

            // Pre-calculate the coordinate data changes.
            for (idx = 0; idx < N_AXIS; idx++) { // Axes indices are consistent, so loop may be used.
                // Update axes defined only in block. Always in machine coordinates. Can change non-active system.
                if (bit_istrue(axis_words, bit(idx))) {
                    if (gc_block.values.l == 20) {
                        // L20: Update coordinate system axis at current position (with modifiers) with programmed value
                        // WPos = MPos - WCS - G92 - TLO  ->  WCS = MPos - G92 - TLO - WPos
                        gc_block.values.ijk[idx] =
                                gc_state.position[idx] - gc_state.coord_offset[idx] - gc_block.values.xyz[idx];
                        if (idx == TOOL_LENGTH_OFFSET_AXIS) { gc_block.values.ijk[idx] -= gc_state.tool_length_offset; }
                    } else {
                        // L2: Update coordinate system axis to programmed value.
                        gc_block.values.ijk[idx] = gc_block.values.xyz[idx];
                    }
                } // Else, keep current stored value.
            }
            break;
        case NON_MODAL_SET_COORDINATE_OFFSET:
            // [G92 Errors]: No axis words.
            if (!axis_words) { FAIL(STATUS_GCODE_NO_AXIS_WORDS); } // [No axis words]

            // Update axes defined only in block. Offsets current system to defined value. Does not update when
            // active coordinate system is selected, but is still active unless G92.1 disables it.
            for (idx = 0; idx < N_AXIS; idx++) { // Axes indices are consistent, so loop may be used.
                if (bit_istrue(axis_words, bit(idx))) {
                    // WPos = MPos - WCS - G92 - TLO  ->  G92 = MPos - WCS - TLO - WPos
                    gc_block.values.xyz[idx] =
                            gc_state.position[idx] - block_coord_system[idx] - gc_block.values.xyz[idx];
                    if (idx == TOOL_LENGTH_OFFSET_AXIS) { gc_block.values.xyz[idx] -= gc_state.tool_length_offset; }
                } else {
                    gc_block.values.xyz[idx] = gc_state.coord_offset[idx];
                }
            }
            break;

        default:

            // At this point, the rest of the explicit axis commands treat the axis values as the traditional
            // target position with the coordinate system offsets, G92 offsets, absolute override, and distance
            // modes applied. This includes the motion mode commands. We can now pre-compute the target position.
            // NOTE: Tool offsets may be appended to these conversions when/if this feature is added.
            if (axis_command != AXIS_COMMAND_TOOL_LENGTH_OFFSET) { // TLO block any axis command.
                if (axis_words) {
                    for (idx = 0;
                         idx < N_AXIS; idx++) { // Axes indices are consistent, so loop may be used to save flash space.
                        if (bit_isfalse(axis_words, bit(idx))) {
                            gc_block.values.xyz[idx] = gc_state.position[idx]; // No axis word in block. Keep same axis position.
                        } else {
                            // Update specified value according to distance mode or ignore if absolute override is active.
                            // NOTE: G53 is never active with G28/30 since they are in the same modal group.
                            if (gc_block.non_modal_command != NON_MODAL_ABSOLUTE_OVERRIDE) {
                                // Apply coordinate offsets based on distance mode.
                                if (gc_block.modal.distance == DISTANCE_MODE_ABSOLUTE) {
                                    gc_block.values.xyz[idx] += block_coord_system[idx] + gc_state.coord_offset[idx];
                                    if (idx ==
                                        TOOL_LENGTH_OFFSET_AXIS) { gc_block.values.xyz[idx] += gc_state.tool_length_offset; }
                                } else {  // Incremental mode
                                    gc_block.values.xyz[idx] += gc_state.position[idx];
                                }
                            }
                        }
                    }
                }
            }

            // Check remaining non-modal commands for errors.
            switch (gc_block.non_modal_command) {
                case NON_MODAL_GO_HOME_0: // G28
                case NON_MODAL_GO_HOME_1: // G30
                    // [G28/30 Errors]: Cutter compensation is enabled.
                    // Retreive G28/30 go-home position data (in machine coordinates) from EEPROM
                    // NOTE: Store parameter data in IJK values. By rule, they are not in use with this command.
                    if (gc_block.non_modal_command == NON_MODAL_GO_HOME_0) {
                        if (!settings_read_coord_data(SETTING_INDEX_G28, gc_block.values.ijk)) {
                            FAIL(STATUS_SETTING_READ_FAIL);
                        }
                    } else { // == NON_MODAL_GO_HOME_1
                        if (!settings_read_coord_data(SETTING_INDEX_G30, gc_block.values.ijk)) {
                            FAIL(STATUS_SETTING_READ_FAIL);
                        }
                    }
                    if (axis_words) {
                        // Move only the axes specified in secondary move.
                        for (idx = 0; idx < N_AXIS; idx++) {
                            if (!(axis_words & (1 << idx))) { gc_block.values.ijk[idx] = gc_state.position[idx]; }
                        }
                    } else {
                        axis_command = AXIS_COMMAND_NONE; // Set to none if no intermediate motion.
                    }
                    break;
                case NON_MODAL_SET_HOME_0: // G28.1
                case NON_MODAL_SET_HOME_1: // G30.1
                    // [G28.1/30.1 Errors]: Cutter compensation is enabled.
                    // NOTE: If axis words are passed here, they are interpreted as an implicit motion mode.
                    break;
                case NON_MODAL_RESET_COORDINATE_OFFSET:
                    // NOTE: If axis words are passed here, they are interpreted as an implicit motion mode.
                    break;
                case NON_MODAL_ABSOLUTE_OVERRIDE:
                    // [G53 Errors]: G0 and G1 are not active. Cutter compensation is enabled.
                    // NOTE: All explicit axis word commands are in this modal group. So no implicit check necessary.
                    if (!(gc_block.modal.motion == MOTION_MODE_SEEK || gc_block.modal.motion == MOTION_MODE_LINEAR)) {
                        FAIL(STATUS_GCODE_G53_INVALID_MOTION_MODE); // [G53 G0/1 not active]
                    }
                    break;
            }
    }

    // [20. Motion modes ]:
    if (gc_block.modal.motion == MOTION_MODE_NONE) {
        // [G80 Errors]: Axis word are programmed while G80 is active.
        // NOTE: Even non-modal commands or TLO that use axis words will throw this strict error.
        if (axis_words) { FAIL(STATUS_GCODE_AXIS_WORDS_EXIST); } // [No axis words allowed]

        // Check remaining motion modes, if axis word are implicit (exist and not used by G10/28/30/92), or
        // was explicitly commanded in the g-code block.
    } else if (axis_command == AXIS_COMMAND_MOTION_MODE) {

        if (gc_block.modal.motion == MOTION_MODE_SEEK) {
            // [G0 Errors]: Axis letter not configured or without real value (done.)
            // Axis words are optional. If missing, set axis command flag to ignore execution.
            if (!axis_words) { axis_command = AXIS_COMMAND_NONE; }

            // All remaining motion modes (all but G0 and G80), require a valid feed rate value. In units per mm mode,
            // the value must be positive. In inverse time mode, a positive value must be passed with each block.
        } else {
            // Check if feed rate is defined for the motion modes that require it.
            if (gc_block.values.f == 0.0) { FAIL(STATUS_GCODE_UNDEFINED_FEED_RATE); } // [Feed rate undefined]

            switch (gc_block.modal.motion) {
                case MOTION_MODE_LINEAR:
                    // [G1 Errors]: Feed rate undefined. Axis letter not configured or without real value.
                    // Axis words are optional. If missing, set axis command flag to ignore execution.
                    if (!axis_words) { axis_command = AXIS_COMMAND_NONE; }
                    break;
                case MOTION_MODE_CW_ARC:
                    gc_parser_flags |= GC_PARSER_ARC_IS_CLOCKWISE; // No break intentional.
                case MOTION_MODE_CCW_ARC:
                    // [G2/3 Errors All-Modes]: Feed rate undefined.
                    // [G2/3 Radius-Mode Errors]: No axis words in selected plane. Target point is same as current.
                    // [G2/3 Offset-Mode Errors]: No axis words and/or offsets in selected plane. The radius to the current
                    //   point and the radius to the target point differs more than 0.002mm (EMC def. 0.5mm OR 0.005mm and 0.1% radius).
                    // [G2/3 Full-Circle-Mode Errors]: NOT SUPPORTED. Axis words exist. No offsets programmed. P must be an integer.
                    // NOTE: Both radius and offsets are required for arc tracing and are pre-computed with the error-checking.

                    if (!axis_words) { FAIL(STATUS_GCODE_NO_AXIS_WORDS); } // [No axis words]
                    if (!(axis_words & (bit(axis_0) | bit(axis_1)))) {
                        FAIL(STATUS_GCODE_NO_AXIS_WORDS_IN_PLANE);
                    } // [No axis words in plane]

                    // Calculate the change in position along each selected axis
                    float x, y;
                    x = gc_block.values.xyz[axis_0] -
                        gc_state.position[axis_0]; // Delta x between current position and target
                    y = gc_block.values.xyz[axis_1] -
                        gc_state.position[axis_1]; // Delta y between current position and target

                    if (value_words & bit(WORD_R)) { // Arc Radius Mode
                        bit_false(value_words, bit(WORD_R));
                        if (isequal_position_vector(gc_state.position, gc_block.values.xyz)) {
                            FAIL(STATUS_GCODE_INVALID_TARGET);
                        } // [Invalid target]

                        // Convert radius value to proper units.
                        if (gc_block.modal.units == UNITS_MODE_INCHES) { gc_block.values.r *= MM_PER_INCH; }

                        // First, use h_x2_div_d to compute 4*h^2 to check if it is negative or r is smaller
                        // than d. If so, the sqrt of a negative number is complex and error out.
                        float h_x2_div_d = 4.0 * gc_block.values.r * gc_block.values.r - x * x - y * y;

                        if (h_x2_div_d < 0) { FAIL(STATUS_GCODE_ARC_RADIUS_ERROR); } // [Arc radius error]

                        // Finish computing h_x2_div_d.
                        h_x2_div_d = -sqrt(h_x2_div_d) / hypot_f(x, y); // == -(h * 2 / d)
                        // Invert the sign of h_x2_div_d if the circle is counter clockwise (see sketch below)
                        if (gc_block.modal.motion == MOTION_MODE_CCW_ARC) { h_x2_div_d = -h_x2_div_d; }

                        // Negative R is g-code-alese for "I want a circle with more than 180 degrees of travel" (go figure!),
                        // even though it is advised against ever generating such circles in a single line of g-code. By
                        // inverting the sign of h_x2_div_d the center of the circles is placed on the opposite side of the line of
                        // travel and thus we get the unadvisably long arcs as prescribed.
                        if (gc_block.values.r < 0) {
                            h_x2_div_d = -h_x2_div_d;
                            gc_block.values.r = -gc_block.values.r; // Finished with r. Set to positive for mc_arc
                        }
                        // Complete the operation by calculating the actual center of the arc
                        gc_block.values.ijk[axis_0] = 0.5 * (x - (y * h_x2_div_d));
                        gc_block.values.ijk[axis_1] = 0.5 * (y + (x * h_x2_div_d));

                    } else { // Arc Center Format Offset Mode
                        if (!(ijk_words & (bit(axis_0) | bit(axis_1)))) {
                            FAIL(STATUS_GCODE_NO_OFFSETS_IN_PLANE);
                        } // [No offsets in plane]
                        bit_false(value_words, (bit(WORD_I) | bit(WORD_J) | bit(WORD_K)));

                        // Convert IJK values to proper units.
                        if (gc_block.modal.units == UNITS_MODE_INCHES) {
                            for (idx = 0; idx <
                                          N_AXIS; idx++) { // Axes indices are consistent, so loop may be used to save flash space.
                                if (ijk_words & bit(idx)) { gc_block.values.ijk[idx] *= MM_PER_INCH; }
                            }
                        }

                        // Arc radius from center to target
                        x -= gc_block.values.ijk[axis_0]; // Delta x between circle center and target
                        y -= gc_block.values.ijk[axis_1]; // Delta y between circle center and target
                        float target_r = hypot_f(x, y);

                        // Compute arc radius for mc_arc. Defined from current location to center.
                        gc_block.values.r = hypot_f(gc_block.values.ijk[axis_0], gc_block.values.ijk[axis_1]);

                        // Compute difference between current location and target radii for final error-checks.
                        float delta_r = fabs(target_r - gc_block.values.r);
                        if (delta_r > 0.005) {
                            if (delta_r > 0.5) { FAIL(STATUS_GCODE_INVALID_TARGET); } // [Arc definition error] > 0.5mm
                            if (delta_r > (0.001 * gc_block.values.r)) {
                                FAIL(STATUS_GCODE_INVALID_TARGET);
                            } // [Arc definition error] > 0.005mm AND 0.1% radius
                        }
                    }
                    break;
                case MOTION_MODE_PROBE_TOWARD_NO_ERROR:
                case MOTION_MODE_PROBE_AWAY_NO_ERROR:
                    gc_parser_flags |= GC_PARSER_PROBE_IS_NO_ERROR; // No break intentional.
                case MOTION_MODE_PROBE_TOWARD:
                case MOTION_MODE_PROBE_AWAY:
                    if ((gc_block.modal.motion == MOTION_MODE_PROBE_AWAY) ||
                        (gc_block.modal.motion ==
                         MOTION_MODE_PROBE_AWAY_NO_ERROR)) { gc_parser_flags |= GC_PARSER_PROBE_IS_AWAY; }
                    // [G38 Errors]: Target is same current. No axis words. Cutter compensation is enabled. Feed rate
                    //   is undefined. Probe is triggered. NOTE: Probe check moved to probe cycle. Instead of returning
                    //   an error, it issues an alarm to prevent further motion to the probe. It's also done there to
                    //   allow the planner buffer to empty and move off the probe trigger before another probing cycle.
                    if (!axis_words) { FAIL(STATUS_GCODE_NO_AXIS_WORDS); } // [No axis words]
                    if (isequal_position_vector(gc_state.position, gc_block.values.xyz)) {
                        FAIL(STATUS_GCODE_INVALID_TARGET);
                    } // [Invalid target]
                    break;
            }
        }
    }

    // [21. Program flow ]: No error checks required.

    // [0. Non-specific error-checks]: Complete unused value words check, i.e. IJK used when in arc
    // radius mode, or axis words that aren't used in the block.
    if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
        // Jogging only uses the F feed rate and XYZ value words. N is valid, but S and T are invalid.
        bit_false(value_words, (bit(WORD_N) | bit(WORD_F)));
    } else {
        bit_false(value_words,
                  (bit(WORD_N) | bit(WORD_F) | bit(WORD_S) | bit(WORD_T))); // Remove single-meaning value words.
    }
    if (axis_command) { bit_false(value_words, (bit(WORD_X) | bit(WORD_Y) | bit(WORD_Z))); } // Remove axis words.
    if (value_words) { FAIL(STATUS_GCODE_UNUSED_WORDS); } // [Unused words]

    /* -------------------------------------------------------------------------------------
       STEP 4: EXECUTE!!
       Assumes that all error-checking has been completed and no failure modes exist. We just
       need to update the state and execute the block according to the order-of-execution.
    */

    // Initialize planner data struct for motion blocks.
    plan_line_data_t plan_data;
    plan_line_data_t *pl_data = &plan_data;
    memset(pl_data, 0, sizeof(plan_line_data_t)); // Zero pl_data struct

    // Intercept jog commands and complete error checking for valid jog commands and execute.
    // NOTE: G-code parser state is not updated, except the position to ensure sequential jog
    // targets are computed correctly. The final parser position after a jog is updated in
    // protocol_execute_realtime() when jogging completes or is canceled.
    if (gc_parser_flags & GC_PARSER_JOG_MOTION) {
        // Only distance and unit modal commands and G53 absolute override command are allowed.
        // NOTE: Feed rate word and axis word checks have already been performed in STEP 3.
        if (command_words & ~(bit(MODAL_GROUP_G3) | bit(MODAL_GROUP_G6 | bit(MODAL_GROUP_G0)))) {
            FAIL(STATUS_INVALID_JOG_COMMAND)
        };
        if (!(gc_block.non_modal_command == NON_MODAL_ABSOLUTE_OVERRIDE ||
              gc_block.non_modal_command == NON_MODAL_NO_ACTION)) { FAIL(STATUS_INVALID_JOG_COMMAND); }

        // Initialize planner data to current spindle and coolant modal state.
        pl_data->spindle_speed = gc_state.spindle_speed;
        plan_data.condition = (gc_state.modal.spindle | gc_state.modal.coolant);

        uint8_t status = jog_execute(&plan_data, &gc_block);
        if (status == STATUS_OK) { memcpy(gc_state.position, gc_block.values.xyz, sizeof(gc_block.values.xyz)); }
        return (status);
    }

    // If in laser mode, setup laser power based on current and past parser conditions.
    if (bit_istrue(settings.flags, BITFLAG_LASER_MODE)) {
        if (!((gc_block.modal.motion == MOTION_MODE_LINEAR) || (gc_block.modal.motion == MOTION_MODE_CW_ARC)
              || (gc_block.modal.motion == MOTION_MODE_CCW_ARC))) {
            gc_parser_flags |= GC_PARSER_LASER_DISABLE;
        }

        // Any motion mode with axis words is allowed to be passed from a spindle speed update.
        // NOTE: G1 and G0 without axis words sets axis_command to none. G28/30 are intentionally omitted.
        // TODO: Check sync conditions for M3 enabled motions that don't enter the planner. (zero length).
        if (axis_words && (axis_command == AXIS_COMMAND_MOTION_MODE)) {
            gc_parser_flags |= GC_PARSER_LASER_ISMOTION;
        } else {
            // M3 constant power laser requires planner syncs to update the laser when changing between
            // a G1/2/3 motion mode state and vice versa when there is no motion in the line.
            if (gc_state.modal.spindle == SPINDLE_ENABLE_CW) {
                if ((gc_state.modal.motion == MOTION_MODE_LINEAR) || (gc_state.modal.motion == MOTION_MODE_CW_ARC)
                    || (gc_state.modal.motion == MOTION_MODE_CCW_ARC)) {
                    if (bit_istrue(gc_parser_flags, GC_PARSER_LASER_DISABLE)) {
                        gc_parser_flags |= GC_PARSER_LASER_FORCE_SYNC; // Change from G1/2/3 motion mode.
                    }
                } else {
                    // When changing to a G1 motion mode without axis words from a non-G1/2/3 motion mode.
                    if (bit_isfalse(gc_parser_flags, GC_PARSER_LASER_DISABLE)) {
                        gc_parser_flags |= GC_PARSER_LASER_FORCE_SYNC;
                    }
                }
            }
        }
    }

    // [0. Non-specific/common error-checks and miscellaneous setup]:
    // NOTE: If no line number is present, the value is zero.
    gc_state.line_number = gc_block.values.n;
    pl_data->line_number = gc_state.line_number; // Record data for planner use.

    // [1. Comments feedback ]:  NOT SUPPORTED

    // [2. Set feed rate mode ]:
    gc_state.modal.feed_rate = gc_block.modal.feed_rate;
    if (gc_state.modal.feed_rate) { pl_data->condition |= PL_COND_FLAG_INVERSE_TIME; } // Set condition flag for planner use.

    // [3. Set feed rate ]:
    gc_state.feed_rate = gc_block.values.f; // Always copy this value. See feed rate error-checking.
    pl_data->feed_rate = gc_state.feed_rate; // Record data for planner use.

    // [4. Set spindle speed ]:
    if ((gc_state.spindle_speed != gc_block.values.s) || bit_istrue(gc_parser_flags, GC_PARSER_LASER_FORCE_SYNC)) {
        if (gc_state.modal.spindle != SPINDLE_DISABLE) {
            if (bit_isfalse(gc_parser_flags, GC_PARSER_LASER_ISMOTION)) {
                if (bit_istrue(gc_parser_flags, GC_PARSER_LASER_DISABLE)) {
                    spindle_sync(gc_state.modal.spindle, 0.0);
                } else { spindle_sync(gc_state.modal.spindle, gc_block.values.s); }
            }
        }
        gc_state.spindle_speed = gc_block.values.s; // Update spindle speed state.
    }
    // NOTE: Pass zero spindle speed for all restricted laser motions.
    if (bit_isfalse(gc_parser_flags, GC_PARSER_LASER_DISABLE)) {
        pl_data->spindle_speed = gc_state.spindle_speed; // Record data for planner use.
    } // else { pl_data->spindle_speed = 0.0; } // Initialized as zero already.

    // [5. Select tool ]: NOT SUPPORTED. Only tracks tool value.
    gc_state.tool = gc_block.values.t;

    // [6. Change tool ]: NOT SUPPORTED

    // [7. Spindle control ]:
    if (gc_state.modal.spindle != gc_block.modal.spindle) {
        // Update spindle control and apply spindle speed when enabling it in this block.
        // NOTE: All spindle state changes are synced, even in laser mode. Also, pl_data,
        // rather than gc_state, is used to manage laser state for non-laser motions.
        spindle_sync(gc_block.modal.spindle, pl_data->spindle_speed);
        gc_state.modal.spindle = gc_block.modal.spindle;
    }
    pl_data->condition |= gc_state.modal.spindle; // Set condition flag for planner use.

    // [8. Coolant control ]:
    if (gc_state.modal.coolant != gc_block.modal.coolant) {
        // NOTE: Coolant M-codes are modal. Only one command per line is allowed. But, multiple states
        // can exist at the same time, while coolant disable clears all states.
        sync_drill(gc_block.modal.coolant);
        gc_state.modal.coolant = gc_block.modal.coolant;
    }
    pl_data->condition |= gc_state.modal.coolant; // Set condition flag for planner use.

    // [10. Dwell ]:
    if (gc_block.non_modal_command == NON_MODAL_DWELL) { mc_dwell(gc_block.values.p); }

    // [11. Set active plane ]:
    gc_state.modal.plane_select = gc_block.modal.plane_select;

    // [12. Set length units ]:
    gc_state.modal.units = gc_block.modal.units;

    // [13. Cutter radius compensation ]: G41/42 NOT SUPPORTED
    // gc_state.modal.cutter_comp = gc_block.modal.cutter_comp; // NOTE: Not needed since always disabled.

    // [14. Cutter length compensation ]: G43.1 and G49 supported. G43 NOT SUPPORTED.
    // NOTE: If G43 were supported, its operation wouldn't be any different from G43.1 in terms
    // of execution. The error-checking step would simply load the offset value into the correct
    // axis of the block XYZ value array.
    if (axis_command == AXIS_COMMAND_TOOL_LENGTH_OFFSET) { // Indicates a change.
        gc_state.modal.tool_length = gc_block.modal.tool_length;
        if (gc_state.modal.tool_length == TOOL_LENGTH_OFFSET_CANCEL) { // G49
            gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS] = 0.0;
        } // else G43.1
        if (gc_state.tool_length_offset != gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS]) {
            gc_state.tool_length_offset = gc_block.values.xyz[TOOL_LENGTH_OFFSET_AXIS];
            system_flag_wco_change();
        }
    }

    // [15. Coordinate system selection ]:
    if (gc_state.modal.coord_select != gc_block.modal.coord_select) {
        gc_state.modal.coord_select = gc_block.modal.coord_select;
        memcpy(gc_state.coord_system, block_coord_system, N_AXIS * sizeof(float));
        system_flag_wco_change();
    }

    // [16. Set path control mode ]: G61.1/G64 NOT SUPPORTED
    // gc_state.modal.control = gc_block.modal.control; // NOTE: Always default.

    // [17. Set distance mode ]:
    gc_state.modal.distance = gc_block.modal.distance;

    // [18. Set retract mode ]: NOT SUPPORTED

    // [19. Go to predefined position, Set G10, or Set axis offsets ]:
    switch (gc_block.non_modal_command) {
        case NON_MODAL_SET_COORDINATE_DATA:
            settings_write_coord_data(coord_select, gc_block.values.ijk);
            // Update system coordinate system if currently active.
            if (gc_state.modal.coord_select == coord_select) {
                memcpy(gc_state.coord_system, gc_block.values.ijk, N_AXIS * sizeof(float));
                system_flag_wco_change();
            }
            break;
        case NON_MODAL_GO_HOME_0:
        case NON_MODAL_GO_HOME_1:
            // Move to intermediate position before going home. Obeys current coordinate system and offsets
            // and absolute and incremental modes.
            pl_data->condition |= PL_COND_FLAG_RAPID_MOTION; // Set rapid motion condition flag.
            if (axis_command) { mc_line(gc_block.values.xyz, pl_data); }
            mc_line(gc_block.values.ijk, pl_data);
            memcpy(gc_state.position, gc_block.values.ijk, N_AXIS * sizeof(float));
            break;
        case NON_MODAL_SET_HOME_0:
            settings_write_coord_data(SETTING_INDEX_G28, gc_state.position);
            break;
        case NON_MODAL_SET_HOME_1:
            settings_write_coord_data(SETTING_INDEX_G30, gc_state.position);
            break;
        case NON_MODAL_SET_COORDINATE_OFFSET:
            memcpy(gc_state.coord_offset, gc_block.values.xyz, sizeof(gc_block.values.xyz));
            system_flag_wco_change();
            break;
        case NON_MODAL_RESET_COORDINATE_OFFSET:
            clear_vector(gc_state.coord_offset); // Disable G92 offsets by zeroing offset vector.
            system_flag_wco_change();
            break;
    }


    // [20. Motion modes ]:
    // NOTE: Commands G10,G28,G30,G92 lock out and prevent axis words from use in motion modes.
    // Enter motion modes only if there are axis words or a motion mode command word in the block.
    gc_state.modal.motion = gc_block.modal.motion;
    if (gc_state.modal.motion != MOTION_MODE_NONE) {
        if (axis_command == AXIS_COMMAND_MOTION_MODE) {
            uint8_t gc_update_pos = GC_UPDATE_POS_TARGET;
            if (gc_state.modal.motion == MOTION_MODE_LINEAR) {
                mc_line(gc_block.values.xyz, pl_data);
            } else if (gc_state.modal.motion == MOTION_MODE_SEEK) {
                pl_data->condition |= PL_COND_FLAG_RAPID_MOTION; // Set rapid motion condition flag.
                mc_line(gc_block.values.xyz, pl_data);
            } else if ((gc_state.modal.motion == MOTION_MODE_CW_ARC) ||
                       (gc_state.modal.motion == MOTION_MODE_CCW_ARC)) {
                mc_arc(gc_block.values.xyz, pl_data, gc_state.position, gc_block.values.ijk, gc_block.values.r,
                       axis_0, axis_1, axis_linear, bit_istrue(gc_parser_flags, GC_PARSER_ARC_IS_CLOCKWISE));
            } else {
                // NOTE: gc_block.values.xyz is returned from mc_probe_cycle with the updated position value. So
                // upon a successful probing cycle, the machine position and the returned value should be the same.
#ifndef ALLOW_FEED_OVERRIDE_DURING_PROBE_CYCLES
                pl_data->condition |= PL_COND_FLAG_NO_FEED_OVERRIDE;
#endif
                gc_update_pos = mc_probe_cycle(gc_block.values.xyz, pl_data, gc_parser_flags);
            }

            // As far as the parser is concerned, the position is now == target. In reality the
            // motion control system might still be processing the action and the real tool position
            // in any intermediate location.
            if (gc_update_pos == GC_UPDATE_POS_TARGET) {
                memcpy(gc_state.position, gc_block.values.xyz,
                       sizeof(gc_block.values.xyz)); // gc_state.position[] = gc_block.values.xyz[]
            } else if (gc_update_pos == GC_UPDATE_POS_SYSTEM) {
                gc_sync_position(); // gc_state.position[] = sys_position
            } // == GC_UPDATE_POS_NONE
        }
    }

    // [21. Program flow ]:
    // M0,M1,M2,M30: Perform non-running program flow actions. During a program pause, the buffer may
    // refill and can only be resumed by the cycle start run-time command.
    gc_state.modal.program_flow = gc_block.modal.program_flow;
    if (gc_state.modal.program_flow) {
        protocol_buffer_synchronize(); // Sync and finish all remaining buffered motions before moving on.
        if (gc_state.modal.program_flow == PROGRAM_FLOW_PAUSED) {
            if (sys.state != STATE_CHECK_MODE) {
                system_set_exec_state_flag(EXEC_FEED_HOLD); // Use feed hold for program pause.
                protocol_execute_realtime(); // Execute suspend.
            }
        } else { // == PROGRAM_FLOW_COMPLETED
            // Upon program complete, only a subset of g-codes reset to certain defaults, according to
            // LinuxCNC's program end descriptions and testing. Only modal groups [G-code 1,2,3,5,7,12]
            // and [M-code 7,8,9] reset to [G1,G17,G90,G94,G40,G54,M5,M9,M48]. The remaining modal groups
            // [G-code 4,6,8,10,13,14,15] and [M-code 4,5,6] and the modal words [F,S,T,H] do not reset.
            gc_state.modal.motion = MOTION_MODE_LINEAR;
            gc_state.modal.plane_select = PLANE_SELECT_XY;
            gc_state.modal.distance = DISTANCE_MODE_ABSOLUTE;
            gc_state.modal.feed_rate = FEED_RATE_MODE_UNITS_PER_MIN;
            gc_state.modal.coord_select = 0; // G54
            gc_state.modal.spindle = SPINDLE_DISABLE;
            gc_state.modal.coolant = DRILL_DISABLE;


#ifdef RESTORE_OVERRIDES_AFTER_PROGRAM_END
            sys.f_override = DEFAULT_FEED_OVERRIDE;
            sys.r_override = DEFAULT_RAPID_OVERRIDE;
            sys.spindle_speed_ovr = DEFAULT_SPINDLE_SPEED_OVERRIDE;
#endif

            // Execute coordinate change and spindle/coolant stop.
            if (sys.state != STATE_CHECK_MODE) {
                if (!(settings_read_coord_data(gc_state.modal.coord_select, gc_state.coord_system))) {
                    FAIL(STATUS_SETTING_READ_FAIL);
                }
                system_flag_wco_change(); // Set to refresh immediately just in case something altered.
                spindle_set_state(SPINDLE_DISABLE, 0.0);
                set_drill_state(DRILL_DISABLE);
            }
            report_feedback_message(MESSAGE_PROGRAM_END);
        }
        gc_state.modal.program_flow = PROGRAM_FLOW_RUNNING; // Reset program flow.
    }

    return (STATUS_OK);
}
