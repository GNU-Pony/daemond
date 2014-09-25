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

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>



/**
 * Command line arguments
 */
static char** argv;

/**
 * Whether the immortality protocol is enabled
 */
static volatile sig_atomic_t immortality = 1;

/**
 * Whether we should re-exec.
 */
static volatile sig_atomic_t reexec = 0;



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
 * This function will if a function is caught
 * during the wait-and-resurrect loop`
 * 
 * @param  signo  The caught signal
 */
static void anastatis_handle_signal(int signo)
{
  if (signo == SIGUSR1)
    reexec = 1;
  else if (signo == SIGUSR2)
    immortality = 0;
}


/**
 * Do some initialisation for the daemon
 * 
 * @return  The value with which `main` should return
 */
static int initialise_daemon(void)
{
  if ((signal(SIGCHLD,    parent_handle_signal) == SIG_ERR) ||
      (signal(SIGUSR1, anastatis_handle_signal) == SIG_ERR) ||
      (signal(SIGUSR2, anastatis_handle_signal) == SIG_ERR))
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
 * @return            The value with which `main` should return
 */
static int respawn(pid_t child)
{
  int r, immortality_ = 1, respawn_ok, have_time;
  struct timespec birth;
  struct timespec death;
  pid_t pid;
  
  have_time = clock_gettime(CLOCK_MONOTONIC, &birth) == 0;
  if (!have_time)
    perror(*argv);
  
  for (;;)
    {
      pause(); /* We are having problems with getting signals to interrupt `wait`. */
      pid = waitpid(-1, &r, WNOHANG);
      if ((pid == 0) || ((pid == -1) && (errno == EINTR)))
	{
	  if (reexec)
	    {
	      char pid_str[3 * sizeof(pid_t) + 1];
	      fprintf(stderr, "%s: reexecuting\n", *argv);
	      if (!immortality)
		fprintf(stderr, "%s: immortality protocol will be reenabled\n", *argv);
	      sprintf(pid_str, "%ji", (intmax_t)pid);
	      execlp(LIBEXECDIR "/daemond-resurrectd", "daemond-resurrectd", pid_str, NULL);
	      perror(*argv);
	    }
	  else if (immortality_ && !immortality)
	    {
	      fprintf(stderr, "%s: disabling immortality protocol\n", *argv);
	      immortality_ = 0;
	      if (kill(child, SIGUSR2) < 0)
		perror(*argv);
	    }
	  continue;
	}
      else if (pid == -1)
	return 1;
      else if (pid != child)
	continue; /* This should not happen. */
      
      if (immortality == 0)
	return 0;
      
      if (have_time)
	{
	  have_time = clock_gettime(CLOCK_MONOTONIC, &death) == 0;
	  if (!have_time)
	    perror(*argv);
	}
      if (have_time)
	{
	  respawn_ok = death.tv_sec - birth.tv_sec > 1;
	  if (!respawn_ok)
	    {
	      long diff = death.tv_sec - birth.tv_sec;
	      diff *= 1000000000L;
	      diff += death.tv_nsec - birth.tv_nsec;
	      respawn_ok = diff >= 1000000000L;
	    }
	  birth = death;
	}
      else
	respawn_ok = 1;
      
      if (WIFEXITED(r))
	fprintf(stderr, "%s: daemond exited with value %i", *argv, WEXITSTATUS(r));
      else
	fprintf(stderr, "%s: daemond died by signal %i", *argv, WTERMSIG(r));
      if (WIFEXITED(r) && (WEXITSTATUS(r) == 0))
	return fprintf(stderr, "\n"), 0;
      else if (respawn_ok)
	fprintf(stderr, ", respawning\n");
      else
	{
	  fprintf(stderr, ", dying too fast, respawning in 5 minutes\n");
	  death.tv_sec += 5 * 60;
	resleep:
	  errno = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &death, NULL);
	  if (errno == EINTR)
	    goto resleep;
	  else if (errno)
	    perror(*argv);
	  fprintf(stderr, "%s: respawning now\n", *argv);
	}
      
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
 * @param   argc   The number of elements in `argv_`
 * @param   argv_  Command line arguments
 * @return         Zero on success, between 1 and 255 on error
 */
int main(int argc, char** argv_)
{
  int r;
  pid_t pid = -1;
  
  argv = argv_;
  
  if ((r = initialise_daemon()))
    return perror(*argv), r;
  
  if (argc == 2)
    {
      pid = (pid_t)atoll(argv[1]);
      goto have_child;
    }
  
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
  
have_child:
  r = respawn(pid);
  return r ? (perror(*argv), r) : r;
}

