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



/* Remark: if daemond and daemond-resurrect is dies at the same
 *         time they will not be resurrected, this is acceptble
 *         because the immortality protocol is intended to stop
 *         crashes from causing a problem, and two  simultaneous
 *         deaths us most probably user triggered. */



/**
 * Run a hook script asynchronously
 * 
 * @param  hook  The hook script
 */
#define etcrun(hook)							\
  if (fork() == 0)							\
    execlp(SYSCONFDIR "/" PKGNAME ".d/" hook,				\
	   SYSCONFDIR "/" PKGNAME ".d/" hook, NULL), exit(0)



/**
 * Command line arguments
 */
static char** argv;

/**
 * The PID of the child process
 */
static pid_t child = -1;

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
 * @return  The value with which `main` should return
 */
static int parent_procedure(void)
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
 * Perform appropriate actions when an interruption has occurred
 */
static void respawn_handle_interruption(void)
{
  static int immortality_ = 1;
  
  if (reexec)
    {
      char pid_str[3 * sizeof(pid_t) + 1];
      fprintf(stderr, "%s: reexecuting\n", *argv);
      if (!immortality)
	fprintf(stderr, "%s: immortality protocol will be reenabled\n", *argv);
      sprintf(pid_str, "%ji", (intmax_t)child);
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
}


/**
 * Perform a resurrection if appropriate
 * 
 * @param   birth      The time when `daemond` was born the last time, will be updated
 * @param   have_time  Whether we have a value for `birth`, will be updated
 * @param   status     The exit status for `daemond`
 * @return             Return value for `respawn`, zero if `respawn` should not return
 */
static int respawn_perform_resurrection(struct timespec* restrict birth, int* restrict have_time, int status)
{
  int respawn_ok = 1;
  struct timespec death;
  
  /* Get time of death. */
  if (*have_time)
    {
      *have_time = clock_gettime(CLOCK_MONOTONIC, &death) == 0;
      if (!*have_time)
	perror(*argv);
    }
  
  /* Was the daemon alive for more than one second? */
  if (*have_time)
    {
      respawn_ok = death.tv_sec - birth->tv_sec > 1;
      if (!respawn_ok)
	{
	  long diff = death.tv_sec - birth->tv_sec;
	  diff *= 1000000000L;
	  diff += death.tv_nsec - birth->tv_nsec;
	  respawn_ok = diff >= 1000000000L;
	}
      *birth = death;
    }
  
  /* Print was is going on. */
  if (WIFEXITED(status))
    fprintf(stderr, "%s: daemond exited with value %i", *argv, WEXITSTATUS(status));
  else
    fprintf(stderr, "%s: daemond died by signal %i", *argv, WTERMSIG(status));
  if (WIFEXITED(status) && (WEXITSTATUS(status) == 0))
    return fprintf(stderr, "\n"), 0;
  else if (respawn_ok)
    fprintf(stderr, ", respawning\n");
  else
    {
      /* (Try to) sleep for 5 minutes, if the daemon too fast, before resurrecting it. */
      fprintf(stderr, ", dying too fast, respawning in 5 minutes\n");
      etcrun("resurrect-paused");
      death.tv_sec += 5 * 60;
    resleep:
      errno = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &death, NULL);
      if (errno == EINTR)
	goto resleep;
      else if (errno)
	perror(*argv);
      fprintf(stderr, "%s: respawning now\n", *argv);
      etcrun("resurrect-resumed");
    }
  
  /* Anastasis. */
  child = fork();
  if (child == -1)
    return 1;
  if (child == 0)
    return child_procedure();
  return 0;
}


/**
 * Respawn the child everytime it dies
 * 
 * @return  The value with which `main` should return
 */
static int respawn(void)
{
  int r, status, have_time;
  struct timespec birth;
  pid_t pid;
  
  /* Get time of birth for the daemon. */
  have_time = clock_gettime(CLOCK_MONOTONIC, &birth) == 0;
  if (!have_time)
    perror(*argv);
  
  for (;;)
    {
      pause(); /* We are having problems with getting signals to interrupt `wait`. */
      pid = waitpid(-1, &status, WNOHANG);
      if ((pid == 0) || ((pid == -1) && (errno == EINTR)))
	{
	  respawn_handle_interruption();
	  continue;
	}
      else if (pid == -1)
	return 1;
      else if (pid != child)
	continue; /* This should not happen. */
      
      if (immortality == 0)
	return 0;
      
      r = respawn_perform_resurrection(&birth, &have_time, status);
      if (r)
	return r;
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
  
  argv = argv_;
  
  if ((r = initialise_daemon()))
    return perror(*argv), r;
  
  if (argc == 2)
    {
      child = (pid_t)atoll(argv[1]);
      goto have_child;
    }
  
  child = fork();
  if (child == -1)
    return perror(*argv), 1;
  
  if (child == 0)
    r = child_procedure();
  else
    r = parent_procedure();
  if (r || !child)
    /* Interruption means that the child died. */
    return (errno != EINTR) ? (perror(*argv), r) : r;
  
  /* Signal `start-daemond` that we are running. */
  if (kill(getppid(), SIGCHLD) < 0)
    return perror(*argv), 1;
  
 have_child:
  r = respawn();
  return r ? (perror(*argv), r) : r;
}

