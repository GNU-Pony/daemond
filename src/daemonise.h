/**
 * daemond — A daemon managing daemon
 * Copyright © 2014  Mattias Andrée (maandree@member.fsf.org)
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef DAEMOND_DAEMONISE_H
#define DAEMOND_DAEMONISE_H


#include "config.h"


/**
 * Daemonise the process and start a daemon
 * 
 * @param   arguments  `NULL`-terminated list of command line arguments,
 *                     the verb first, then the name of the daemon, followed
 *                     by optional additional script-dependent arguments
 * @return             The function can not return, it will
 *                     however exit the image with a return
 *                     as an unlikely fallback
 */
int start_daemon(char** arguments) __attribute__((noreturn));


#endif

