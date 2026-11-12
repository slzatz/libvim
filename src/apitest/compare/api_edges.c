/*
 * api_edges: call the C API with arguments an embedder would not mean to
 * give it -- line ranges that are negative, reversed or past the end, a
 * buffer that is not current, a cursor the buffer has shrunk under -- and
 * print what happens. See README.md.
 *
 * Nothing is asserted: the output is to be read. Each case runs in a child
 * process and is reported as "! HANG", "! CRASH signal <n>" or "! EXIT <n>"
 * if it ends that way. Several of these read memory they should not, which
 * shows only when libvim.a itself is built with AddressSanitizer (TODO.md
 * has the command).
 *
 *   ./api_edges.exe             every case
 *   ./api_edges.exe setlines    the cases whose name contains "setlines"
 *
 * Set LIBVIM_RUNTIME to a directory with spell/en.utf-8.spl for the spell
 * case.
 */
#include "libvim.h"
#include "vim.h"
#include <signal.h>
#include <sys/wait.h>

static buf_T *buf;

static void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("    msg[%d]: %s\n", priority, msg);
}

static void show(const char *what)
{
  linenr_T i;

  printf("  %-34s mode=0x%x cursor=%ld,%d n=%ld modified=%d |", what, vimGetMode(),
         (long)vimCursorGetLine(), (int)vimCursorGetColumn(),
         (long)vimBufferGetLineCount(curbuf), vimBufferGetModified(curbuf));
  for (i = 1; i <= (linenr_T)vimBufferGetLineCount(curbuf) && i <= 6; i++)
    printf("%s|", vimBufferGetLine(curbuf, i));
  printf("\n");
  fflush(stdout);
}

static void highlights(const char *what, buf_T *b, linenr_T start, linenr_T end)
{
  searchHighlight_T *found = NULL;
  char_u *pattern = vimSearchGetPattern();
  char_u *last = vimEval((char_u *)"@/");
  int n = 0;
  int i;

  vimSearchGetHighlights(b, start, end, &n, &found);
  printf("  %-34s vimSearchGetPattern=[%s] @/=[%s] lines %ld..%ld: %d:", what,
         pattern == NULL ? "(null)" : (char *)pattern, last == NULL ? "(null)" : (char *)last,
         (long)start, (long)end, n);
  for (i = 0; i < n && i < 8; i++)
    printf(" %ld,%d-%ld,%d", (long)found[i].start.lnum, (int)found[i].start.col,
           (long)found[i].end.lnum, (int)found[i].end.col);
  printf("\n");
  fflush(stdout);
  vim_free(found);
  vim_free(last);
}

static void setup(void)
{
  char *lines[] = {"alpha one", "bravo two", "charlie three", "delta four", "echo five"};
  char *runtime = getenv("LIBVIM_RUNTIME");

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
}

/* vimBufferSetLines() with the lines given as arguments */
#define SET_LINES(b, start, end, ...)                                  \
  do                                                                   \
  {                                                                    \
    char *set_lines_[] = {__VA_ARGS__};                                \
    vimBufferSetLines((b), (start), (end), (char_u **)set_lines_,      \
                      (int)(sizeof(set_lines_) / sizeof(*set_lines_))); \
  } while (0)

/* vimBufferSetLines() */

static void setlines_then_undo(void)
{
  vimInput("x");
  SET_LINES(buf, 0, 1, "NEW");
  show("x, then line 1 set to NEW");
  vimInput("u");
  show("u");
  vimKey("<c-r>");
  show("CTRL-R");
}

static void setlines_then_repeat(void)
{
  vimInput("x");
  SET_LINES(buf, 0, -1, "abc", "def");
  vimInput(".");
  show("x, two lines set, then .");
}

static void setlines_under_the_cursor(void)
{
  vimInput("G");
  vimInput("$");
  SET_LINES(buf, 0, -1, "x");
  show("cursor on line 5, one line set");
  vimInput("x");
  show("x");
  vimInput("j");
  show("j");
}

static void setlines_under_the_cursor_in_insert_mode(void)
{
  vimInput("A");
  vimInput("zz");
  SET_LINES(buf, 0, -1, "x");
  vimInput("y");
  vimKey("<esc>");
  show("Azz, one line set, y<Esc>");
}

static void setlines_and_marks(void)
{
  vimInput("G");
  vimInput("ma");
  vimInput("gg");
  SET_LINES(buf, 0, -1, "x", "y");
  vimInput("'");
  vimInput("a");
  show("mark a on line 5, two lines set, 'a");
}

static void setlines_negative_start(void)
{
  SET_LINES(buf, -3, 2, "x");
  printf("  start -3, end 2: %ld lines\n", (long)vimBufferGetLineCount(curbuf));
  fflush(stdout);
  show("the lines");
}

static void setlines_end_before_start(void)
{
  SET_LINES(buf, 3, 1, "x");
  show("start 3, end 1");
}

static void setlines_past_the_end(void)
{
  long tick = vimBufferGetLastChangedTick(buf);

  SET_LINES(buf, 50, 60, "x");
  show("start 50, end 60");
  printf("  changed tick went from %ld to %ld\n", tick, vimBufferGetLastChangedTick(buf));
  SET_LINES(buf, 3, 99, "x");
  show("start 3, end 99");
}

static void setlines_buffer_never_current(void)
{
  buf_T *other = vimBufferNew(0);

  SET_LINES(other, 0, -1, "in the other buffer", "second line");
  printf("  the other buffer: %ld lines, changed tick %ld\n", (long)vimBufferGetLineCount(other),
         vimBufferGetLastChangedTick(other));
  vimBufferSetCurrent(other);
  show("made current afterwards");
}

static void setlines_newline_in_a_line(void)
{
  SET_LINES(buf, 0, 1, "a\nb");
  show("line 1 set to a, newline, b");
}

/* lines and the cursor */

static void getline_out_of_range(void)
{
  printf("  line 0 [%s], line 99 [%s], line -1 [%s]\n", vimBufferGetLine(buf, 0),
         vimBufferGetLine(buf, 99), vimBufferGetLine(buf, -1));
}

static void cursor_out_of_range(void)
{
  pos_T pos = {99, 99, 0};

  vimCursorSetPosition(pos);
  show("cursor set to 99,99");
  pos.lnum = 0;
  pos.col = -5;
  vimCursorSetPosition(pos);
  show("cursor set to 0,-5");
  vimInput("x");
  show("x");
}

/* search highlights */

static void highlights_and_the_last_search(void)
{
  highlights("no search yet", buf, 0, 0);
  vimExecute("let @/ = 'two'");
  highlights("let @/ = 'two'", buf, 0, 0);

  vimInput("/");
  vimInput("two");
  vimKey("<cr>");
  highlights("typed /two", buf, 0, 0);
  vimExecute("keeppatterns s/bravo/BRAVO/");
  highlights(":keeppatterns s/bravo/BRAVO/", buf, 0, 0);
  vimInput("n");
  highlights("n", buf, 0, 0);
  vimExecute("call search('three')");
  highlights("call search('three')", buf, 0, 0);
  vimExecute("let @/ = 'charlie'");
  highlights("let @/ = 'charlie'", buf, 0, 0);
  vimExecute("let @/ = ''");
  highlights("let @/ = ''", buf, 0, 0);

  /* "*" does not use 'smartcase': n visits all three */
  SET_LINES(buf, 0, -1, "Alpha alpha one", "ALPHA two");
  vimExecute("set ignorecase smartcase");
  vimInput("gg");
  vimInput("*");
  highlights("ic scs, * on Alpha", buf, 0, 0);
}

static void highlights_line_ranges(void)
{
  vimInput("/");
  vimInput("o");
  vimKey("<cr>");
  highlights("all", buf, 0, 0);
  highlights("3..99", buf, 3, 99);
  highlights("4..2", buf, 4, 2);
  highlights("99..100", buf, 99, 100);
  highlights("-5..-1", buf, -5, -1);
  highlights("2..2", buf, 2, 2);
  highlights("NULL buffer", NULL, 0, 0);
}

/* spelling */

static void spell_suggestions(void)
{
  char_u *bad = NULL;
  char_u **suggestions = NULL;
  spellError_T *errors = NULL;
  int n = 0;
  int ok;

  ok = vimSpellGetSuggestions(&bad, &n, &suggestions);
  printf("  'spell' off: suggestions %s, %d\n", ok == OK ? "OK" : "FAIL", n);
  vimSpellGetErrors(buf, 1, 5, &n, &errors);
  printf("  'spell' off: %d errors\n", n);

  vimExecute("set spell spelllang=en");
  SET_LINES(buf, 0, -1, "helo wrld. this is a tst", "", "last");
  vimSpellGetErrors(buf, -4, 99, &n, &errors);
  printf("  lines -4..99: %d errors\n", n);
  vim_free(errors);
  vimSpellGetErrors(buf, 3, 1, &n, &errors);
  printf("  lines 3..1: %d errors\n", n);
  vimSpellGetErrors(NULL, 1, 1, &n, &errors);
  printf("  NULL buffer: %d errors\n", n);

  ok = vimSpellGetSuggestions(&bad, &n, &suggestions);
  printf("  on \"helo\": %s, [%s], %d, first [%s]\n", ok == OK ? "OK" : "FAIL",
         bad == NULL ? "(null)" : (char *)bad, n, n > 0 ? (char *)suggestions[0] : "");
  vimInput("j");
  ok = vimSpellGetSuggestions(&bad, &n, &suggestions);
  printf("  on an empty line: %s, %d\n", ok == OK ? "OK" : "FAIL", n);
  vimInput("gg");
  vimInput("v");
  vimInput("G");
  ok = vimSpellGetSuggestions(&bad, &n, &suggestions);
  printf("  a selection of three lines: %s, %d\n", ok == OK ? "OK" : "FAIL", n);
  vimKey("<esc>");
  vimExecute("set spellsuggest=0");
  ok = vimSpellGetSuggestions(&bad, &n, &suggestions);
  printf("  'spellsuggest' 0: %s, %d\n", ok == OK ? "OK" : "FAIL", n);
}

/* the rest */

static void register_names(void)
{
  int names[] = {'a', 0, -1, '=', ':', '%', '/', 999};
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(*names); i++)
  {
    char_u **lines = NULL;
    int n = -1;

    vimRegisterGet(names[i], &n, &lines);
    printf("  register %d: %d lines\n", names[i], n);
  }
}

static void window_sizes(void)
{
  vimWindowSetWidth(0);
  vimWindowSetHeight(0);
  show("width 0, height 0");
  vimWindowSetWidth(-5);
  vimWindowSetHeight(-5);
  show("width -5, height -5");
  vimWindowSetTopLeft(-1, -1);
  vimWindowSetTopLeft(999, 999);
  vimInput("G");
  show("top and left -1, then 999; G");
}

static void window_size_very_large(void)
{
  vimWindowSetWidth(100000);
  vimWindowSetHeight(100000);
  show("width and height 100000");
}

static void init_twice(void)
{
  vimInput("x");
  vimInit(0, NULL);
  show("x, vimInit() again");
  vimInput("x");
  show("x");
}

static struct
{
  const char *name;
  void (*run)(void);
} cases[] = {
#define CASE(name) {#name, name}
    CASE(setlines_then_undo),
    CASE(setlines_then_repeat),
    CASE(setlines_under_the_cursor),
    CASE(setlines_under_the_cursor_in_insert_mode),
    CASE(setlines_and_marks),
    CASE(setlines_negative_start),
    CASE(setlines_end_before_start),
    CASE(setlines_past_the_end),
    CASE(setlines_buffer_never_current),
    CASE(setlines_newline_in_a_line),
    CASE(getline_out_of_range),
    CASE(cursor_out_of_range),
    CASE(highlights_and_the_last_search),
    CASE(highlights_line_ranges),
    CASE(spell_suggestions),
    CASE(register_names),
    CASE(window_sizes),
    CASE(window_size_very_large),
    CASE(init_twice),
};

int main(int argc, char **argv)
{
  size_t i;

  for (i = 0; i < sizeof(cases) / sizeof(*cases); i++)
  {
    pid_t pid;
    int status = 0;

    if (argc > 1 && strstr(cases[i].name, argv[1]) == NULL)
      continue;

    printf("CASE %s\n", cases[i].name);
    fflush(stdout);

    pid = fork();
    if (pid == 0)
    {
      alarm(15);
      setup();
      cases[i].run();
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
