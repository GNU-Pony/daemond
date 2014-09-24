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
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>


/**
 * The directory where we have our executables
 */
#ifndef LIBEXECDIR
# define LIBEXECDIR  "." /* Nice for testing. */
#endif



/**
 * This is a signal handler that does nothing
 * 
 * @param  signo  The caught signal
 */
static void noop_sig_handler(int signo)
{
  (void) signo;
}


/**
 * Mane procedure for the child process after the fork
 * to resurrect `daemond-resurrectd`
 * 
 * @return  The value with which `main` should return
 */
static int child_procedure(void)
{
  execlp(LIBEXECDIR "/daemond-resurrectd", "daemond-resurrectd", NULL);
  return 1;
}


/**
 * Mane procedure for the parent process after the fork
 * to resurrect `daemond-resurrectd`
 * 
 * @param   child  The PID of the child process
 * @return         Zero on success or the exit value of the child process, -1 on error
 */
static int parent_procedure(pid_t child)
{
  int rc = 0;
  pid_t pid;
  
  pause();
  
  pid = waitpid(child, &rc, WNOHANG);
  if (pid == -1)
    rc = -1;
  else if (pid != 0)
    {
      rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : WTERMSIG(rc);
      errno = EINTR;
    }
  
  return rc;
}



/**
 * Starts the daemon (managing) daemon
 * 
 * @param   argc  The number of elements in `argv`
 * @param   argv  Command line arguments
 * @return        Zero on success, between 1 and 255 on error
 */
int main(int argc, char* argv[])
{
  (void) argc;
  
  if (signal(SIGRTMIN, noop_sig_handler) == SIG_ERR)
    return perror(*argv), 1;
  
  if (prctl(PR_SET_PDEATHSIG, SIGRTMIN) < 0)
    return perror(*argv), 1;
  
  /* Signal `daemond-resurrectd` that we are running. */
  if (kill(getppid(), SIGCHLD) < 0)
    return perror(*argv), 1;
  
  for (;;)
    {
      pid_t pid;
      pause();
      fprintf(stderr, "%s: daemond-resurrectd died, respawning\n", *argv);
      if ((signal(SIGCHLD, noop_sig_handler) == SIG_ERR) ||
	  (pid = fork(), pid == -1))
	{
	  perror(*argv);
	  continue;
	}
      else if (pid == 0)
	{
	  int r = child_procedure();
	  return perror(*argv), r;
	}
      else
	if (parent_procedure(pid))
	  perror(*argv);
	else
	  return 0;
    }
}

