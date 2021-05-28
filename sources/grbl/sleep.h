/*
  sleep.h - Sleep methods header file
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

#ifndef sleep_h
#define sleep_h

#include "grbl.h"


// Inicia el temporalizador de sueño
void sleep_init();

// Verifica las condiciones para entrar en reposo.
// Si se cumplen inicia el contador, una vez cumplido
// el sistema entra en reposo
void sleep_check();

#endif
