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
#ifndef DAEMOND_CONFIG
#define DAEMOND_CONFIG


/* Default values are used for testing. */


/**
 * The directory where we have our executables
 */
#ifndef LIBEXECDIR
# define LIBEXECDIR  "."
#endif

/**
 * The base-directory for where we store runtime data
 */
#ifndef RUNDIR
# define RUNDIR  ".run"
#endif

/**
 * The package name of the software
 */
#ifndef PKGNAME
# define PKGNAME  "daemond"
#endif


#endif
