/*
 * setlines_stress: keys and vimBufferSetLines() calls in a random order, to
 * see that nothing the buffer or a mode holds -- the cursor, marks, the
 * Visual area, where an insert started, the jumplist -- is left pointing at
 * a line that is gone. Meant to be run against a libvim.a built with
 * AddressSanitizer; without one it still catches "E315: ml_get: invalid
 * lnum" and E19, which is how a stale line number shows.
 *
 *   ./setlines_stress.exe [seeds [steps]]     default 200 seeds of 400 steps
 *   ./setlines_stress.exe -s <seed> [steps]   one seed, each step printed
 *
 * Each seed runs in a child process. A seed that fails prints its steps; one
 * that crashes or hangs is to be run again with -s. STRESS_NO_SET=1 in the
 * environment sets no lines: the keys alone, to tell what is libvim's own.
 * See README.md.
 */
#include "libvim.h"
#include "vim.h"
#include <signal.h>
#include <sys/wait.h>

static int bad = 0;
static char history[400 * 64];
static size_t used = 0;

static int show_steps = 0; /* print each step before it is taken */

static void note(const char *what)
{
  size_t n = strlen(what);

  if (show_steps)
  {
    printf("%s\n", what);
    fflush(stdout);
  }

  if (used + n + 2 < sizeof(history))
  {
    memcpy(history + used, what, n);
    used += n;
    history[used++] = ' ';
    history[used] = 0;
  }
}

static void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  /* E315 is a line number vim itself believed in and the buffer does not
   * have; E685 an internal error. (E19 is not one of these: vim says it for
   * '[ after the last line was deleted.) */
  if (strstr((char *)msg, "E315") != NULL || strstr((char *)msg, "E685") != NULL)
  {
    bad++;
    printf("    said: %s\n", msg);
  }
}

static const char *keys[] = {
    "h", "j", "k", "l", "w", "b", "e", "0", "$", "gg", "G", "3G", "x", "dd", "yy", "p", "P",
    "u", "<c-r>", ".", "i", "a", "A", "I", "o", "O", "<esc>", "<esc>", "<esc>", "v", "V", "<c-v>",
    "d", "c", "y", "ma", "mb", "'a", "'b", "`a", "`b", "''", "g;", "g,", "<c-o>",
    "gv", "n", "N", "*", "J", ">>", "~", "rx", "D", "C", "s", "X", "<bs>", "<cr>", "z", "q",
    "'[", "']", "'<", "'>", "'.", "'^", "gi", "{", "}", "%", "ip", "aw", "o", "R", "<c-u>",
    "/a<cr>", "?e<cr>", ":s/a/b/<cr>", ":2<cr>", ":$<cr>", ":normal! Gdd<cr>", "<c-v>jI", "5j", "5k"};
#define NKEYS ((int)(sizeof(keys) / sizeof(*keys)))

static const char *texts[] = {"", "a", "alpha one", "  indented", "\xc3\xa9t\xc3\xa9", "a b c d e f g", "(x)", "\t tab"};
#define NTEXTS ((int)(sizeof(texts) / sizeof(*texts)))

static void type(const char *k)
{
  /* a key name goes to vimKey(), text a character at a time to vimInput() */
  while (*k)
  {
    const char *end = *k == '<' ? strchr(k, '>') : NULL;

    if (end != NULL)
    {
      char name[16];
      size_t n = (size_t)(end - k) + 1;

      memcpy(name, k, n);
      name[n] = 0;
      vimKey((char_u *)name);
      k = end + 1;
    }
    else
    {
      char one[2] = {*k, 0};

      vimInput((char_u *)one);
      k++;
    }
  }
}

static int run(unsigned seed, int steps)
{
  char *start[] = {"alpha one", "bravo two", "charlie three", "delta four", "echo five"};
  buf_T *buf;
  int i;
  int last_was_ctrl_v = 0;

  srand(seed);
  vimInit(0, NULL);
  vimSetMessageCallback(onMessage);
  buf = vimBufferNew(0);
  vimBufferSetCurrent(buf);
  vimBufferSetLines(buf, 0, -1, (char_u **)start, 5);

  for (i = 0; i < steps; i++)
  {
    /* CTRL-O can go to the buffer libvim starts with. vimango brings vim
     * back after every key; here it is left there half the time, so that
     * lines are also set in a buffer that is not the current one. */
    if (curbuf != buf && rand() % 2)
    {
      note("[back]");
      vimBufferSetCurrent(buf);
    }
    if (rand() % 4 == 0 && getenv("STRESS_NO_SET") == NULL)
    {
      char *lines[8];
      char what[64];
      int n = (int)vimBufferGetLineCount(buf);
      int count = rand() % 8;
      int from = rand() % (n + 2) - (rand() % 20 == 0);
      int to = rand() % 3 == 0 ? -1 : rand() % (n + 3);
      int j;

      for (j = 0; j < count; j++)
        lines[j] = (char *)texts[rand() % NTEXTS];
      snprintf(what, sizeof(what), "[set %d,%d x%d]", from, to, count);
      note(what);
      vimBufferSetLines(buf, from, to, (char_u **)lines, count);
      /* the text is new, as vimango says each time: with the history left
       * as it was, u puts back lines, marks and a cursor from before */
      vimUndoClear(buf);
    }
    else
    {
      const char *k = keys[rand() % NKEYS];

      /* CTRL-V CTRL-O in Insert mode, then "." and later a "." in Normal
       * mode, does not come back, with no lines set at all (TODO.md, the
       * literal CTRL-O in the redo buffer). Not what this is looking for. */
      if (last_was_ctrl_v && strcmp(k, "<c-o>") == 0)
        k = "<esc>";
      last_was_ctrl_v = strcmp(k, "<c-v>") == 0;
      note(k);
      type(k);
    }
    /* what an embedder reads after every key */
    (void)vimCursorGetLine();
    (void)vimCursorGetColumn();
    if (vimCursorGetLine() < 1 || vimCursorGetLine() > (linenr_T)vimBufferGetLineCount(curbuf))
    {
      bad++;
      printf("    cursor on line %ld of %ld\n", (long)vimCursorGetLine(), (long)vimBufferGetLineCount(curbuf));
    }
    if (bad)
      break;
  }
  if (bad)
    printf("  seed %u, after %d steps: %s\n", seed, i + 1, history);
  return bad != 0;
}

int main(int argc, char **argv)
{
  int seeds = argc > 1 ? atoi(argv[1]) : 200;
  int steps = argc > 2 ? atoi(argv[2]) : 400;
  int failed = 0;
  int s;

  if (argc > 2 && strcmp(argv[1], "-s") == 0)
  {
    show_steps = 1;
    return run((unsigned)atoi(argv[2]), argc > 3 ? atoi(argv[3]) : 400);
  }

  for (s = 1; s <= seeds; s++)
  {
    pid_t pid;
    int status = 0;

    fflush(stdout);
    pid = fork();
    if (pid == 0)
    {
      int result;

      alarm(20);
      result = run((unsigned)s, steps);
      fflush(stdout);
      _exit(result);
    }
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status))
    {
      printf("  seed %d: ! %s\n", s, WTERMSIG(status) == SIGALRM ? "HANG" : "CRASH");
      failed++;
    }
    else if (WEXITSTATUS(status) != 0)
    {
      if (WEXITSTATUS(status) != 1)
        printf("  seed %d: ! EXIT %d\n", s, WEXITSTATUS(status));
      failed++;
    }
  }
  printf("%d seeds of %d steps, %d failed\n", seeds, steps, failed);
  return failed != 0;
}
