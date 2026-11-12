/*
 * probe: run key sequences through libvim, one per line of a file, and print
 * what each leaves behind. See README.md.
 *
 * Every case runs in a child process, from a buffer of five known lines, so
 * that a crash, a hang or a call to exit() ends that case and no other.
 *
 * A line is typed as a user would type it: <name> goes to vimKey(), anything
 * else to vimInput() a character at a time. Three markers:
 *
 *   {EX}cmd     at the start: the rest is given to vimExecute()
 *   {KEY}text   at the start: the rest is given to vimKey() in one piece
 *   {TO}        anywhere: vimInputTimeout(), as if 'timeoutlen' had passed
 *
 * Output, for each case:
 *
 *   CASE <the line>
 *     msg[<priority>]: <a message sent to the message callback>
 *     mode=0x<vimGetMode()> pending=<vimInputPending()>
 *     = cursor=<line>,<col> n=<lines> |<line 1>|<line 2>|...|     (six at most)
 *     ! HANG, ! CRASH signal <n>, ! EXIT <n>, ! exit() was called
 *
 * The "=" line is the one vim-oracle.sh prints for the same keys in vim.
 *
 * Environment: LIBVIM_RUNTIME, a directory to add to 'runtimepath' (for the
 * spell files); PROBE_TIMEOUT, seconds allowed for a case (default 15).
 */
#include "libvim.h"
#include "vim.h"
#include <signal.h>
#include <sys/wait.h>

#define MAX_CASES 20000

static void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("  msg[%d]: %s\n", priority, msg);
}

/* The length of a <name> at "s" (letters, digits, '-' and '_'), or 0. */
static size_t key_name_len(const char *s)
{
  const char *p = s + 1;

  if (*s != '<')
    return 0;
  while ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
    p++;
  return (*p == '>' && p > s + 1) ? (size_t)(p - s) + 1 : 0;
}

static void type(const char *s)
{
  char buf[64];

  while (*s)
  {
    size_t n;

    if (strncmp(s, "{TO}", 4) == 0)
    {
      vimInputTimeout();
      s += 4;
      continue;
    }

    n = key_name_len(s);
    if (n > 0 && n < sizeof(buf))
    {
      memcpy(buf, s, n);
      buf[n] = 0;
      vimKey((char_u *)buf);
      s += n;
      continue;
    }

    n = (size_t)(*mb_ptr2len)((char_u *)s);
    if (n == 0 || n >= sizeof(buf))
      n = 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    vimInput((char_u *)buf);
    s += n;
  }
}

static void run_case(const char *line)
{
  char *lines[] = {"alpha one", "bravo two", "charlie three", "delta four", "echo five"};
  char *runtime = getenv("LIBVIM_RUNTIME");
  buf_T *buf;
  linenr_T i;

  vimInit(0, NULL);
  vimSetMessageCallback(onMessage);
  buf = vimBufferNew(0);
  vimBufferSetCurrent(buf);
  vimBufferSetLines(buf, 0, -1, (char_u **)lines, 5);
  if (runtime != NULL && *runtime != 0)
  {
    char cmd[2048];

    snprintf(cmd, sizeof(cmd), "set runtimepath+=%s", runtime);
    vimExecute((char_u *)cmd);
  }
  vimSetMappingsEnabled(TRUE);

  if (strncmp(line, "{EX}", 4) == 0)
    vimExecute((char_u *)line + 4);
  else if (strncmp(line, "{KEY}", 5) == 0)
    vimKey((char_u *)line + 5);
  else
    type(line);

  printf("  mode=0x%x pending=%d\n", vimGetMode(), vimInputPending());
  printf("  = cursor=%ld,%d n=%ld |", (long)vimCursorGetLine(), (int)vimCursorGetColumn(),
         (long)vimBufferGetLineCount(curbuf));
  for (i = 1; i <= (linenr_T)vimBufferGetLineCount(curbuf) && i <= 6; i++)
    printf("%s|", vimBufferGetLine(curbuf, i));
  printf("\n");
}

static void note_exit(void)
{
  printf("  ! exit() was called\n");
  fflush(stdout);
}

int main(int argc, char **argv)
{
  static char *cases[MAX_CASES];
  char line[8192];
  char *timeout = getenv("PROBE_TIMEOUT");
  int seconds = timeout != NULL && atoi(timeout) > 0 ? atoi(timeout) : 15;
  int count = 0;
  int i;
  FILE *f;

  if (argc != 2 || (f = fopen(argv[1], "r")) == NULL)
  {
    fprintf(stderr, "usage: probe <file of cases>\n");
    return 2;
  }

  /* Read them all first: a child that exits would otherwise move this
   * process's place in the file. */
  while (count < MAX_CASES && fgets(line, sizeof(line), f) != NULL)
  {
    size_t n = strlen(line);

    if (n > 0 && line[n - 1] == '\n')
      line[--n] = 0;
    if (n == 0 || line[0] == '#')
      continue;
    cases[count++] = strdup(line);
  }
  fclose(f);

  for (i = 0; i < count; i++)
  {
    pid_t pid;
    int status = 0;

    printf("CASE %s\n", cases[i]);
    fflush(stdout);

    pid = fork();
    if (pid == 0)
    {
      alarm((unsigned)seconds);
      atexit(note_exit);
      run_case(cases[i]);
      fflush(stdout);
      _exit(0);
    }
    waitpid(pid, &status, 0);

    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM)
      printf("  ! HANG\n");
    else if (WIFSIGNALED(status))
      printf("  ! CRASH signal %d\n", WTERMSIG(status));
    else if (WEXITSTATUS(status) != 0)
      printf("  ! EXIT %d\n", WEXITSTATUS(status));
    fflush(stdout);
  }

  return 0;
}
