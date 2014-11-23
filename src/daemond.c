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
#include "daemonise.h"

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/file.h>
#include <sys/prctl.h>



/**
 * Command line arguments
 */
char** argv;

/**
 * The key of the server message queue
 */
static key_t mqueue_key;

/**
 * The ID of the server message queue
 */
static int mqueue_id;

/**
 * The file which holds a lock to indicate
 * that the daemon is running
 */
static int life;

/**
 * Whether the parent has died
 */
static volatile sig_atomic_t pdeath = 0;

/**
 * Whether the immortality protocol is enabled
 */
static volatile sig_atomic_t immortality = 1;

/**
 * Whether we should re-exec.
 */
static volatile sig_atomic_t reexec = 0;



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
 * A general signal handler
 * 
 * @param  signo  The caught signal
 */
static void sig_handler(int signo)
{
  if      (signo == SIGRTMIN)  pdeath = 1;
  else if (signo == SIGUSR1)   reexec = 1;
  else if (signo == SIGUSR2)   immortality = 0;
}


/**
 * Acquire and set the value for `mqueue_key`
 * 
 * @return  The value with which `main` should return
 */
static int get_mqueue_key(void)
{
  char buf[3 * sizeof(key_t) + 2];
  int fd = -1, saved_errno;
  ssize_t got;
  char* end;
  
  if (fd = open(RUNDIR "/" PKGNAME "/mqueue.key", O_RDONLY), fd < 0)
    return 1;
  if (got = read(fd, buf, sizeof(buf) - sizeof(char)), got < 0)
    goto fail;
  if (close(fd) < 0)
    perror(*argv);
  fd = -1;
  buf[got] = 0;
  if (memchr(buf, '\n', (size_t)got) == NULL)
    {
      fprintf(stderr, "%s: %s contains invalid data\n",
	      *argv, RUNDIR "/" PKGNAME "/mqueue.key");
      return errno = 0, 1;
    }
  mqueue_key = (key_t)strtoll(buf, &end, 10);
  if ((end[0] != '\n') || end[1])
    {
      fprintf(stderr, "%s: %s contains invalid data\n",
	      *argv, RUNDIR "/" PKGNAME "/mqueue.key");
      return errno = 0, 1;
    }
  
  return 0;
  
 fail:
  saved_errno = errno;
  if (fd >= 0)
    close(fd);
  return errno = saved_errno, 1;
}


/**
 * Initialise the daemon
 * 
 * @return  The value with which `main` should return
 */
static int initialise_daemon(void)
{
  int r;
  
  /* There is an unlikely race condition: during exec:s the program loses its
     lock, another instance of daemond could be started during this period. */
  life = open(RUNDIR "/" PKGNAME "/lifeline", O_CREAT | O_APPEND | O_RDWR | O_CLOEXEC, 0750);
  if (life < 0)
    return 1;
  if (flock(life, LOCK_EX | LOCK_NB) < 0)
    {
      if (errno == EWOULDBLOCK)
	{
	  fprintf(stderr, "%s: daemond is already running\n", *argv);
	  return errno = 0, 1;
	}
      return 1;
    }
  
  if ((signal(SIGRTMIN,     sig_handler) == SIG_ERR) ||
      (signal(SIGUSR1,      sig_handler) == SIG_ERR) ||
      (signal(SIGUSR2,      sig_handler) == SIG_ERR) ||
      (signal(SIGCHLD, noop_sig_handler) == SIG_ERR) ||
      (prctl(PR_SET_PDEATHSIG, SIGRTMIN) < 0)        ||
      (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0))
    return 1;
  
  if ((r = get_mqueue_key()))
    return r;
  if (mqueue_id = msgget(mqueue_key, 0750), mqueue_id < 0)
    return r;
  
  return 0;
}


/**
 * Mane procedure for the child process after the fork
 * to resurrect `daemond-resurrectd`
 * 
 * @return  The value with which `main` should return
 */
static int child_procedure(void)
{
  if (close(life) < 0)
    perror(*argv);
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
 * Resurrect `daemond-resurrectd`
 * 
 * @return  -1 on acceptable failure, otherwise the value
 *          with which the program should exit
 */
static int resurrect_parent(void)
{
  pid_t pid;
  int r;
  
  fprintf(stderr, "%s: daemond-resurrectd died, respawning\n", *argv);
  
  if (flock(life, LOCK_UN) < 0)
    perror(*argv);
  
  if ((signal(SIGCHLD, noop_sig_handler) == SIG_ERR) ||
      (pid = fork(), pid == -1))
    perror(*argv);
  else if (pid == 0)
    {
      prctl(PR_SET_CHILD_SUBREAPER, 0);
      r = child_procedure();
      return perror(*argv), r;
    }
  else
    if (parent_procedure(pid))
      {
	if (errno != EINTR)
	  perror(*argv);
      }
    else
      return 0; /* XXX: I wish we could send our children to the new `daemond-resurrectd`
		        and that `daemond-resurrectd` could forward them to the new
			`daemond` it creates. */
  
  if (flock(life, LOCK_EX) < 0)
    perror(*argv);
  
  return -1;
}


/**
 * Perform appropriate actions when an interruption has occurred
 * 
 * @return  The value `main` should return, -1 of `main` should not return
 */
static int handle_interruption(void)
{
  static int immortality_ = 1;
  int r;
  
  if (reexec)
    {
      fprintf(stderr, "%s: reexecuting\n", *argv);
      if (!immortality)
	fprintf(stderr, "%s: immortality protocol will be reenabled\n", *argv);
      execlp(LIBEXECDIR "/daemond", "daemond", "--reexecing", NULL);
      perror(*argv);
    }
  else if (pdeath && immortality)
    {
      pdeath = 0;
      if (r = resurrect_parent(), r >= 0)
	return r;
    }
  else if (immortality_ && !immortality)
    {
      fprintf(stderr, "%s: disabling immortality protocol\n", *argv);
      immortality_ = 0;
      if (kill(getppid(), SIGUSR2) < 0)
	perror(*argv);
    }
  
  return -1;
}


/**
 * Attempt to reap a child, if there are no zombies, handle interruption
 * 
 * @return  The return value for `main`, -1 if the called should not return
 */
static int reap(void)
{
  int r, status;
  pid_t pid;
  
  if (pid = waitpid(-1, &status, WNOHANG), pid < 1)
    {
      if ((errno != EINTR) && (errno != ECHILD))
	return perror(*argv), 1;
      else if ((errno == EINTR) || (pid == 0))
	if (r = handle_interruption(), r >= 0)
	  return r;
    }
  else
    printf("reaped %ji\n", (intmax_t)pid); /* TODO */
  
  return -1;
}


/**
 * Handle a received message
 * 
 * @param   message  The message
 * @param   length   The length of `message`
 * @return           The return value for `main`, -1 if the called should not return
 */
static int received_message(char* message, size_t length)
{
  char** arguments;
  size_t count = 0;
  size_t i, j;
  
  if ((length == 0) || (message[length - 1] != '\0'))
    return fprintf(stderr, "%s: received invalid message\n", *argv), -1;
  
  /* Simplify message, so arguments start at NUL:s. */
  memmove(message + 1, message, length - 1);
  message[0] = '\0';
  
  for (i = 0; i < length; i++)
    if (message[i] == '\0')
      count++;
  
  arguments = malloc((count + 1) * sizeof(char*));
  if (arguments == NULL)
    return perror(*argv), 1;
  
  for (i = j = 0; i < length; i++)
    if (message[i] == '\0')
      arguments[j++] = message + i;
  arguments[count] = NULL;
  
  /* Unsimplify message, so every argument is NUL-terminated. */
  memmove(message, message + 1, length - 1);
  message[length - 1] = '\0';
  
  /* TODO */
  
  return free(arguments), -1;
}


/**
 * The mane loop, manage daemons
 * 
 * @return  The return value for `main`
 */
static int mane_loop(void)
{
  struct { long mtype; char* mtext; } mqueue_buf;
  struct msqid_ds mqueue_info;
  ssize_t msg_size;
  int r;
  
  if (msgctl(mqueue_id, IPC_STAT, &mqueue_info) < 0)
    return perror(*argv), 1;
  
  mqueue_buf.mtype = 0;
  mqueue_buf.mtext = malloc(mqueue_info.msg_qbytes * sizeof(char));
  if (mqueue_buf.mtext == NULL)
    return perror(*argv), 1;
  
  for (;;)
    {
      msg_size = msgrcv(mqueue_id, &mqueue_buf, mqueue_info.msg_qbytes, 1, 0);
      if ((msg_size < 0) && (errno != EINTR))
	return perror(*argv), free(mqueue_buf.mtext), 1;
      else if (msg_size < 0)
	r = reap();
      else
	r = received_message(mqueue_buf.mtext, (size_t)msg_size / sizeof(char));
      if (r >= 0)
	return free(mqueue_buf.mtext), r;
    }
}


/**
 * Starts the daemon (-managing) daemon
 * 
 * @param   argc   The number of elements in `argv_`
 * @param   argv_  Command line arguments
 * @return         Zero on success, between 1 and 255 on error
 */
int main(int argc, char** argv_)
{
  int r, reexeced = 0;
  
  argv = argv_;
  if ((argc == 2) && !strcmp(argv[1], "--reexecing"))
    reexeced = 1;
  
  if ((r = initialise_daemon()))
    return errno ? (perror(*argv), r) : r;
  
  /* Signal `daemond-resurrectd` that we are running. */
  if (!reexeced)
    if (kill(getppid(), SIGCHLD) < 0)
      return perror(*argv), 1;
  
  return mane_loop();
}

