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
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>


/**
 * The directory where we have our executables
 */
#ifndef LIBEXECDIR
# define LIBEXECDIR  "." /* Nice for testing. */
#endif



/**
 * This function will be called in the parent
 * process when a signal is catched. It is only
 * here to make sure `pause` gets interrupted.
 * 
 * @param  signo  The caught signal
 */
static void parent_handle_signal(int signo)
{
  (void) signo;
}


/**
 * Do some initialisation for the daemon
 * 
 * @return  The value with which `main` should return
 */
static int initialise_daemon(void)
{
  if (signal(SIGCHLD, parent_handle_signal) == SIG_ERR)
    return 1;
  return 0;
}


/**
 * Mane procedure for the child process after the fork
 * 
 * @return  The value with which `main` should return
 */
static int child_procedure(void)
{
  execlp(LIBEXECDIR "/daemond", "daemond", NULL);
  return 1;
}


/**
 * Mane procedure for the parent process after the fork
 * 
 * @param   child  The PID of the child process
 * @return         The value with which `main` should return
 */
static int parent_procedure(pid_t child)
{
  int rc = 0;
  pid_t r;
  
  pause();
  
  r = waitpid(child, &rc, WNOHANG);
  if (r == -1)
    rc = 1;
  else if (r != 0)
    {
      rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : WTERMSIG(rc);
      errno = EINTR;
    }
  
  return rc;
}


/**
 * Respawn the child everytime it dies
 * 
 * @param   child     The PID of the child process
 * @param   execname  `*argv` from `main`
 * @return            The value with which `main` should return
 */
static int respawn(pid_t child, char* execname)
{
  pid_t pid;
  int r;
  
  for (;;)
    {
      pid = waitpid(child, &r, 0);
      if (pid == -1)
	return 1;
      else if (pid != child)
	continue; /* This should not happen. */
      
      if (WIFEXITED(r))
	fprintf(stderr, "%s: daemond exited with value %i", execname, WEXITSTATUS(r));
      else
	fprintf(stderr, "%s: daemond died by signal %i", execname, WTERMSIG(r));
      if (WIFEXITED(r) && (WEXITSTATUS(r) == 0))
	return fprintf(stderr, "\n"), 0;
      else
	fprintf(stderr, ", respawning\n");
      
      child = fork();
      if (child == -1)
	return 1;
      if (child == 0)
	return child_procedure();
    }
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
  int r;
  pid_t pid;
  
  (void) argc;
  
  if (initialise_daemon() < 0)
    return perror(*argv), 1;
  
  pid = fork();
  if (pid == -1)
    return perror(*argv), 1;
  
  if (pid == 0)
    r = child_procedure();
  else
    r = parent_procedure(pid);
  if (r || !pid)
    /* Interruption means that the child died. */
    return (errno != EINTR) ? (perror(*argv), r) : r;
  
  /* Signal `start-daemond` that we are running. */
  if (kill(getppid(), SIGCHLD) < 0)
    return perror(*argv), 1;
  
  if (signal(SIGCHLD, SIG_DFL) == SIG_ERR)
    perror(*argv);
  
  r = respawn(pid, *argv);
  return r ? (perror(*argv), r) : r;
}

