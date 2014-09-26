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
#include "config.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>


/**
 * Starts the daemon supreaper
 * 
 * @param   argc  The number of elements in `argv`
 * @param   argv  Command line arguments
 * @return        Zero on success, between 1 and 255 on error
 */
int main(int argc, char** argv)
{
#define t(cond)  if (cond) goto fail
  
  char** args = malloc((size_t)argc * sizeof(char*));
  int i;
  pid_t pid;
  
  t (args == NULL);
  t (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0);
  
  for (i = 1; i < argc; i++)
    args[i - 1] = argv[i];
  args[argc - 1] = NULL;
  
  pid = fork();
  t (pid == -1);
  
  if (pid == 0)
    {
      execvp(*args, args);
      goto fail;
    }
  
  for (;;)
    if (pid = wait(&i), pid == -1)
      {
	if (errno == ECHILD)
	  return WIFEXITED(i) ? WEXITSTATUS(i) : WTERMSIG(i);
	else if (errno != EINTR)
	  return perror(*argv), 1;
      }
  
 fail:
  perror(*argv);
  return 1;
  
#undef t
}

