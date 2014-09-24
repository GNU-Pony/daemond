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
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>



/**
 * The directory where we have our executables
 */
#ifndef LIBEXECDIR
# define LIBEXECDIR  "." /* Nice for testing. */
#endif



/**
 * If we are the parent process, the PID
 * of the child process
 */
static pid_t pid;



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
  umask(022);
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
  execlp(LIBEXECDIR "/daemond-resurrectd", "daemond-resurrectd", NULL);
  return 1;
}


/**
 * Mane procedure for the parent process after the fork
 * 
 * @return  The value with which `main` should return
 */
static int parent_procedure(void)
{
  int rc = 0;
  pid_t r;
  
  pause();
  
  r = waitpid(pid, &rc, WNOHANG);
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
 * Starts the daemon (managing) daemon and its immortality protocol
 * 
 * @param   argc  The number of elements in `argv`
 * @param   argv  Command line arguments
 * @return        Zero on success, between 1 and 255 on error
 */
int main(int argc, char* argv[])
{
  int r;
  
  (void) argc;
  
  if (initialise_daemon() < 0)
    return perror(*argv), 1;
  
  pid = fork();
  if (pid == -1)
    return perror(*argv), 1;
  
  if (pid == 0)
    r = child_procedure();
  else
    r = parent_procedure();
  
  /* Interruption means that the child died. */
  return (r && (errno != EINTR)) ? (perror(*argv), r) : r;
}

