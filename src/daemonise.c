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
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/prctl.h>



/**
 * Command line arguments
 */
extern char** argv;



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
 * Close all file descriptor except stdin, stdout and stderr
 */
static void close_nonstd_fds(void)
{
  DIR* dir;
  struct dirent* file;
  int fd;
  
  if (dir = opendir(SELF_FD), dir != NULL)
    while ((file = readdir(dir)) != NULL)
      if (strcmp(file->d_name, ".") && strcmp(file->d_name, ".."))
        {
          fd = atoi(file->d_name);
          if ((fd != STDIN_FILENO) && (fd != STDOUT_FILENO) && (fd != STDERR_FILENO))
            close(fd);
        }
  closedir(dir);
}


/**
 * Read the value in a PID file
 * 
 * @param   pathname  The PID file's pathname
 * @return            The PID stored in the file, -1 on error (-1 in waitpid means any PID)
 */
static pid_t read_pid(const char* pathname)
{
  char buf[3 * sizeof(pid_t) + 1];
  int fd;
  ssize_t got;
  
  if (fd = open(pathname, O_RDONLY), fd < 0)
    return -1;
  got = read(fd, buf, sizeof(buf) - sizeof(char));
  if (close(fd) < 0)
    perror(*argv);
  fd = -1;
  buf[got] = 0;
  return (pid_t)atoi(buf);
}


/**
 * Daemonise the process and start a daemon
 * 
 * @param   arguments  `NULL`-terminated list of command line arguments,
 *                     the verb first, then the name of the daemon, followed
 *                     by optional additional script-dependent arguments
 * @return             The function call not return, it will
 *                     however exit the image with a return
 *                     as an unlikely fallback
 */
int start_daemon(char** arguments)
{
#define return  exit
#define t(cond)  if (cond) goto fail
  
  char* daemon_name = arguments[1];
  char buf[3 * sizeof(pid_t) + 2];
  int i, r, fd = -1, saved_errno;
  sigset_t set;
  char* pid_pathname = NULL;
  size_t n;
  pid_t pid, child;
  
  /* Get pathname of PID file. */
  pid_pathname = malloc((strlen(RUNDIR "/.pid") + strlen(daemon_name) + 1) * sizeof(char));
  t (pid_pathname == NULL);
  sprintf(pid_pathname, RUNDIR "/%s.pid", daemon_name);
  
  /* Close all file descriptors but stdin, stdout and stderr. */
  close_nonstd_fds();
  
  /* Reset all signals to SIG_DFL. */
  for (i = 1; i < _NSIG; i++)
    signal(i, SIG_DFL);
  
  /* Reset signal mask. */
  sigfillset(&set);
  sigprocmask(SIG_UNBLOCK, &set, NULL);
  
  /* Mark daemon with its name. */
  t (setenv(ENV_DAEMON_NAME_TAG, daemon_name, 1) < 0);
  
  /* Set to child subreaper and set SIGCHLD listening. */
  t (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0);
  t (signal(SIGCHLD, noop_sig_handler) == SIG_ERR);
  
  /* Fork */
  t ((pid = fork(), pid == -1));
  if (pid)
    goto wait_for_completion;
  
  /* Create session leader. */
  setsid();
  
  /* Reset some thinks. */
  prctl(PR_SET_CHILD_SUBREAPER, 0);
  
  /* Fork again, and exit first child synchronously. */
  if (pid = fork(), pid > 0)
    {
      pause();
      exit(1); /* Failure, if the grandchild dies first */
    }
  t (signal(SIGCHLD, noop_sig_handler) == SIG_ERR);
  t (prctl(PR_SET_PDEATHSIG, SIGCHLD) < 0);
  t (kill(getppid(), SIGCHLD) < 0);
  pause();
  
  /* Reset some thinks. */
  signal(SIGCHLD, SIG_DFL);
  
  /* Replace stdin and stdout, but not stderr, with /dev/null. */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  fd = open(DEV_NULL, O_RDWR);
  if ((fd >= 0) && (fd != STDIN_FILENO))
    {
      dup2(fd, STDIN_FILENO);
      close(fd);
    }
  dup2(STDIN_FILENO, STDOUT_FILENO);
  fd = -1;
  
  /* Set umask to zero. */
  umask(0);
  
  /* Write PID file. */
  fd = open(pid_pathname, O_WRONLY | O_CREAT | O_TRUNC, 644);
  t (fd < 0);
  sprintf(buf, "%ji\n", (intmax_t)getpid());
  n = strlen(buf) * sizeof(char);
  if (write(fd, buf, n) < (ssize_t)n)
    {
      saved_errno = errno, unlink(pid_pathname), errno = saved_errno;
      goto fail;
    }
  close(fd), fd = -1;
  free(pid_pathname), pid_pathname = NULL;
  
  /* `cd` into root. */
  if (*SYSCONFDIR == '/')
    chdir("/");
  
  /* Execute into daemon. */
  arguments[1] = arguments[0];
  arguments[0] = daemon_name;
  execvp(SYSCONFDIR "/" PKGNAME ".d/daemon-base", arguments);
  
 fail:
  perror(*argv);
  if (fd >= 0)
    close(fd);
  free(pid_pathname);
  return (1);
  
 wait_for_completion:
  pause(), pause(); /* Wait for child and grandchild. */
  /* Exit like the grandchild. */
  child = read_pid(pid_pathname);
  pid = waitpid(child, &r, WNOHANG);
  if (pid == -1)
    goto fail;
  r = pid ? WIFEXITED(r) ? WEXITSTATUS(r) : WTERMSIG(r) : 0;
  free(pid_pathname);
  return (r);
  
#undef t
#undef return
}

