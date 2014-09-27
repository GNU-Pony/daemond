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
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdint.h>


/**
 * Command line arguments
 */
static char** argv;

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
 * Use some proper randomness to seed the randomness
 */
static void seed_random(void)
{
  void* ap = malloc(1);
  void* bp = malloc(1);
  unsigned int a = (unsigned int)(long)ap;
  unsigned int b = (unsigned int)(long)bp;
  unsigned int c = (unsigned int)time(NULL);
  free(ap);
  free(bp);
  b = (b << (4 * sizeof(unsigned int)))
    | (b >> (4 * sizeof(unsigned int)));
  srand(a ^ b ^ c);
}


/**
 * Generate a System V IPC key
 * 
 * @return  The generated key
 */
static key_t generate_key(void)
{
  key_t key = IPC_PRIVATE;
  long long max = 1LL << (8 * sizeof(key_t) - 2);
  max |= max - 1;
  while (key == IPC_PRIVATE)
    {
      int rnd = rand();
      key = (key_t)((double)rnd / ((double)RAND_MAX + 1) * (double)(max - 1)) + 1;
    }
  return key;
}


/**
 * Create a directory recursively
 * 
 * @param   pathname  Follows the same rules as `mkdir`
 * @param   mode      Follows the same rules as `mkdir`
 * @return            Follows the same rules as `mkdir`
 */
static int mkdirs(const char* pathname, mode_t mode)
{
  char* path = strdup(pathname);
  size_t s, n = strlen(pathname);
  char* p;
  
  if (path == NULL)
    return -1;
  
  while ((p = strrchr(path, '/')))
    *p = '\0';
  
  for (; s = strlen(path), s < n; path[s] = '/')
    if (mkdir(path, mode) < 0)
      if (errno != EEXIST)
	return free(path), -1;
  
  return free(path), mkdir(pathname, mode);
}


/**
 * Create a System V message queue
 * 
 * @return  Zero on success, the value with which `main` should return on failure
 */
static int create_mqueue(void)
{
  char buf[3 * sizeof(key_t) + 2];
  int fd = -1, saved_errno, mqueue_id;
  key_t mqueue_key;
  size_t n;
  
  /* Create a System V message queue with a random key. */
  seed_random();
 retry_mqueue:
  mqueue_key = generate_key();
  mqueue_id = msgget(mqueue_key, 0750 | IPC_CREAT | IPC_EXCL);
  if (mqueue_id < 0)
    {
      if (errno == EEXIST)
	goto retry_mqueue;
      else
	return 1;
    }
  
  /* Store the key in a file. */
  fd = open(RUNDIR "/" PKGNAME "/mqueue.key", O_CREAT | O_EXCL | O_WRONLY, 0640);
  if (fd < 0)
    goto fail;
  sprintf(buf, "%ji\n", (intmax_t)mqueue_key);
  n = strlen(buf) * sizeof(char);
  if (write(fd, buf, n) < (ssize_t)n)
    {
      unlink(RUNDIR "/" PKGNAME "/mqueue.key");
      goto fail;
    }
  if (close(fd) < 0)
    perror(*argv);
  fd = -1;
  
  return 0;
  
 fail:
  saved_errno = errno;
  if (msgctl(mqueue_id, IPC_RMID, NULL) < 0)
    perror(*argv);
  if (fd >= 0)
    if (close(fd) < 0)
      perror(*argv);
  return errno = saved_errno, 1;
}


/**
 * Read an entire file, ignoring interruptions
 * 
 * @param   pathname  The pathname of the file
 * @param   n         The number of read characters
 * @return            The content of the file, `NULL` on error
 */
static char* read_file(const char* restrict pathname, size_t* restrict n)
{
  char* restrict content = NULL;
  char* restrict old = NULL;
  size_t ptr = 0;
  ssize_t got;
  int fd, saved_errno;
  
  if (fd = open(pathname, O_RDONLY), fd < 0)
    return NULL;
  
  for (*n = 0;;)
    {
      if (*n - ptr < 4096)
	{
	  content = realloc(old = content, (*n += 8 << 10) * sizeof(char));
	  if (content == NULL)
	    goto fail;
	  old = NULL;
	}
      got = read(fd, content + ptr, *n - ptr);
      if ((got < 0) && (errno == EINTR))
	continue;
      else if (got < 0)
	goto fail;
      else if (got == 0)
	break;
      else
	ptr += (size_t)got;
    }
  
  *n = ptr / sizeof(char);
  close(fd), fd = -1;
  return content;
  
 fail:
  saved_errno = errno;
  free(content);
  free(old);
  errno = saved_errno;
  return NULL;
}


/**
 * Parse the content of the environtab
 * 
 * @param   environtab  The content of the environtab
 * @return              `environ` as described by `environtab`, `NULL` on error
 */
static char** environtab_parse(char* restrict environtab)
{
  size_t saved_i = 0, saved_n = 0, n;
  char** restrict saved_env = NULL;
  char** restrict saved_env_old = NULL;
  char* end;
  char* begin;
  char* restrict var;
  char* restrict entry;
  char* restrict old;
  int saved_errno;
  
  for (begin = environtab;; begin = end + 1)
    {
      if (saved_i == saved_n)
	{
	  saved_env_old = saved_env;
	  saved_env = realloc(saved_env, (saved_n += 8) * sizeof(char*));
	  if (saved_env == NULL)
	    goto fail;
	  saved_env_old = NULL;
	}
      if (end = strchr(begin, '\n'), end == NULL)
	break;
      if (*end = '\0', *begin == '\0')
	continue;
      if (strchr(begin, '='))
	{
	  saved_env[saved_i] = strdup(begin);
	  if (saved_env[saved_i++] == NULL)
	    goto fail;
	}
      else if ((var = getenv(begin)))
	{
	  n = strlen(begin) + strlen(var) + 2;
	  if (old = strdup(begin), old == NULL)
	    goto fail;
	  if (entry = realloc(old, n * sizeof(char)), entry == NULL)
	    goto fail;
	  saved_env[saved_i++] = entry, old = NULL;
	  n = strlen(entry);
	  entry[n++] = '=';
	  memcpy(entry + n, var, (strlen(var) + 1) * sizeof(char));
	}
    }
  
  return saved_env;
  
 fail:
  saved_errno = errno;
  free(old);
  free(saved_env);
  free(saved_env_old);
  errno = saved_errno;
  return NULL;
}


/**
 * Clear the environment variables and fill it with new/stored values
 * 
 * @param   new_environ  The new environment variables with their associated values
 * @param   n            The number of elements in `new_environ`
 * @return               Zero on success, -1 on error
 */
static int set_environ(char* const* restrict new_environ, size_t n)
{
  size_t i;
  
  if (clearenv() != 0)
    return -1;
  
  for (i = 0; i < n; i++)
    {
      char* var = new_environ[i];
      char* val = strchr(var, '=');
      if (val == NULL)
	abort();
      *val++ = '\0';
      if (setenv(var, val, 1) < 0)
	return -1;
    }
  
  return 0;
}


/**
 * Remove unrecognised environment variables,
 * and potentially add new environment variables
 * 
 * @return  Zero on success, -1 on error
 */
static int sanitise_environment(void)
{
  char* restrict old = NULL;
  char* restrict environtab = NULL;
  int fd = -1, rc = 0, saved_errno = 0;
  char** restrict saved_env = NULL;
  size_t i, n, saved_n = 0;
  
  /* Get environment table. */
  if (access(SYSCONFDIR "/" PKGNAME ".d/environtab", F_OK) == 0)
    {
      environtab = read_file(SYSCONFDIR "/" PKGNAME ".d/environtab", &n);
      if (environtab == NULL)
	return -1;
    }
  else if (errno != ENOENT)
    return -1;
  else
    /* Default table. */
    if (environtab = strdup("PATH\n"), environtab == NULL)
      goto fail;
    else
      n = strlen("PATH\n");
  
  /* Make sure the table ends with a new line. */
  if (environtab[n - 1] != '\n')
    {
      old = environtab;
      environtab = realloc(environtab, (n + 1) * sizeof(char));
      if (environtab == NULL)
	goto fail;
      environtab[n++] = '\n';
      old = NULL;
    }
  
  /* Store environment variables before clearing and find new values. */
  if (saved_env = environtab_parse(environtab), saved_env == NULL)
    goto fail;
  
  /* Clear unwanted environment variables and add new ones. */
  if (set_environ(saved_env, saved_n) < 0)
    goto fail;
  
  goto done;
  
 fail:
  saved_errno = errno;
  rc = -1;
  if (fd >= 0)
    close(fd);
  free(old);
 done:
  free(environtab);
  for (i = 0; i < saved_n; i++)
    free(saved_env[i]);
  free(saved_env);
  errno = saved_errno;
  return rc;
}


/**
 * Do some initialisation for the daemon
 * 
 * @return  The value with which `main` should return
 */
static int initialise_daemon(void)
{
  int r;
  
  umask(022);
  
  if (mkdirs(RUNDIR "/" PKGNAME, 0750) < 0)
    if (errno != EEXIST)
      return 1;
  
  if (access(RUNDIR "/" PKGNAME "/mqueue.key", F_OK) < 0)
    {
      if (errno == ENOENT)
	if ((r = create_mqueue()))
	  return r;
      if (errno != ENOENT)
	return 1;
    }
  
  if (signal(SIGCHLD, parent_handle_signal) == SIG_ERR)
    return 1;
  
  if (sanitise_environment() < 0)
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
 * @param   argc   The number of elements in `argv_`
 * @param   argv_  Command line arguments
 * @return         Zero on success, between 1 and 255 on error
 */
int main(int argc, char** argv_)
{
  int r;
  
  (void) argc;
  argv = argv_;
  
  if ((r = initialise_daemon()))
    return errno ? (perror(*argv), r) : r;
  
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

