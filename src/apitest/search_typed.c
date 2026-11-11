#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * A search typed on the command line: "/pattern<cr>".
 *
 * While the pattern is typed 'incsearch' moves the cursor to the match. vim
 * puts it back when the line is finished and then runs the search. Upstream
 * libvim left it on the match and searched forward and back again from there,
 * which found the right place for a plain pattern typed with 'incsearch' on,
 * and otherwise: a wrap warning for each of the two searches and none for the
 * real one, "Pattern not found" for a pattern with an offset, a cursor that
 * moved when the search failed, and the wrong match with 'noincsearch'.
 *
 * The buffer:
 *   This is the first line of a test file
 *   This is the second line of a test file
 *   This is the third line of a test file
 */

static char said[4096];
static int messageCount = 0;
static int warningCount = 0;
static int errorCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
  messageCount++;
  if (priority == MSG_WARNING)
    warningCount++;
  if (priority == MSG_ERROR)
    errorCount++;
}

/* Type keys: a <name> goes through vimKey, anything else a byte at a time. */
static void type(char *s)
{
  char one[2] = {0, 0};

  while (*s)
  {
    if (*s == '<')
    {
      char key[32];
      char *end = strchr(s, '>');
      size_t n = end - s + 1;

      memcpy(key, s, n);
      key[n] = 0;
      vimKey((char_u *)key);
      s = end + 1;
    }
    else
    {
      one[0] = *s++;
      vimInput((char_u *)one);
    }
  }
}

static void from(linenr_T lnum, colnr_T col)
{
  pos_T pos;

  pos.lnum = lnum;
  pos.col = col;
  pos.coladd = 0;
  vimCursorSetPosition(pos);
  said[0] = 0;
  messageCount = 0;
  warningCount = 0;
  errorCount = 0;
}

static int at(linenr_T lnum, colnr_T col)
{
  pos_T pos = vimCursorGetPosition();

  if (pos.lnum != lnum || pos.col != col)
    printf("cursor is at %ld,%d, not %ld,%d\n", (long)pos.lnum, (int)pos.col, (long)lnum, (int)col);
  return pos.lnum == lnum && pos.col == col;
}

#define HIT_BOTTOM "[search hit BOTTOM, continuing at TOP]"
#define HIT_TOP "[search hit TOP, continuing at BOTTOM]"

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("set wrapscan incsearch");
  from(1, 0);
}

void test_teardown(void) {}

MU_TEST(test_forward_without_wrapping_says_nothing)
{
  type("/second<cr>");
  mu_check(at(2, 12));
  mu_check(messageCount == 0);

  from(1, 0);
  type("/line<cr>");
  mu_check(at(1, 18));
  mu_check(messageCount == 0);
}

MU_TEST(test_forward_wrap_is_reported_once)
{
  from(3, 0);
  type("/first<cr>");
  mu_check(at(1, 12));
  mu_check(messageCount == 1 && warningCount == 1);
  mu_check(strcmp(said, HIT_BOTTOM) == 0);

  // the only match is the one under the cursor: found again, by wrapping
  from(2, 12);
  type("/second<cr>");
  mu_check(at(2, 12));
  mu_check(strcmp(said, HIT_BOTTOM) == 0);
}

MU_TEST(test_backward)
{
  from(3, 0);
  type("?second<cr>");
  mu_check(at(2, 12));
  mu_check(messageCount == 0);

  from(1, 0);
  type("?second<cr>");
  mu_check(at(2, 12));
  mu_check(strcmp(said, HIT_TOP) == 0);
}

MU_TEST(test_count)
{
  type("2/line<cr>");
  mu_check(at(2, 19));
  mu_check(messageCount == 0);
}

MU_TEST(test_offset)
{
  type("/second/e<cr>");
  mu_check(at(2, 17));
  mu_check(messageCount == 0);

  from(1, 0);
  type("/first/+1<cr>");
  mu_check(at(2, 0));
  mu_check(messageCount == 0);
}

MU_TEST(test_not_found_is_one_error_and_the_cursor_stays)
{
  from(2, 5);
  type("/zzz<cr>");
  mu_check(at(2, 5));
  mu_check(errorCount == 1);
  mu_check(strstr(said, "[E486: Pattern not found: zzz]") != NULL);
}

MU_TEST(test_nowrapscan_failure_leaves_the_cursor)
{
  vimExecute("set nowrapscan");
  from(3, 0);
  type("/first<cr>");
  vimExecute("set wrapscan");

  // 'incsearch' had moved it to the match on line 1
  mu_check(at(3, 0));
  mu_check(errorCount == 1);
  mu_check(strstr(said, "E385") != NULL);
}

MU_TEST(test_without_incsearch)
{
  vimExecute("set noincsearch");
  type("/line<cr>");
  mu_check(at(1, 18));
  mu_check(messageCount == 0);

  from(3, 0);
  type("/first<cr>");
  mu_check(at(1, 12));
  mu_check(strcmp(said, HIT_BOTTOM) == 0);
}

MU_TEST(test_escape_puts_the_cursor_back)
{
  from(1, 5);
  type("/second<esc>");
  mu_check(at(1, 5));
  mu_check(messageCount == 0);
}

MU_TEST(test_the_mark_is_where_the_search_started)
{
  from(1, 5);
  type("/second<cr>");
  mu_check(at(2, 12));
  type("``");
  mu_check(at(1, 5));
}

MU_TEST(test_empty_pattern_repeats_the_last)
{
  type("/line<cr>");
  mu_check(at(1, 18));
  type("/<cr>");
  mu_check(at(2, 19));
  mu_check(messageCount == 0);
}

MU_TEST(test_n_and_N_follow)
{
  type("/line<cr>");
  type("n");
  mu_check(at(2, 19));
  type("N");
  type("N");
  mu_check(at(3, 18));
  mu_check(strcmp(said, HIT_TOP) == 0);
}

MU_TEST(test_ctrl_g_moves_the_start)
{
  type("/line<c-g><cr>");
  mu_check(at(2, 19));

  from(1, 0);
  type("/line<c-g><c-t><cr>");
  mu_check(at(1, 18));
}

MU_TEST(test_operator_takes_the_search_as_its_motion)
{
  type("d/second<cr>");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1), "second line of a test file") == 0);
  mu_check(messageCount == 0);
}

MU_TEST(test_visual_extends_to_the_match)
{
  pos_T start, end;

  type("v/second<cr>");
  mu_check((vimGetMode() & VISUAL) == VISUAL);
  vimVisualGetRange(&start, &end);
  mu_check(start.lnum == 1 && start.col == 0);
  mu_check(end.lnum == 2 && end.col == 12);
  mu_check(messageCount == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_forward_without_wrapping_says_nothing);
  MU_RUN_TEST(test_forward_wrap_is_reported_once);
  MU_RUN_TEST(test_backward);
  MU_RUN_TEST(test_count);
  MU_RUN_TEST(test_offset);
  MU_RUN_TEST(test_not_found_is_one_error_and_the_cursor_stays);
  MU_RUN_TEST(test_nowrapscan_failure_leaves_the_cursor);
  MU_RUN_TEST(test_without_incsearch);
  MU_RUN_TEST(test_escape_puts_the_cursor_back);
  MU_RUN_TEST(test_the_mark_is_where_the_search_started);
  MU_RUN_TEST(test_empty_pattern_repeats_the_last);
  MU_RUN_TEST(test_n_and_N_follow);
  MU_RUN_TEST(test_ctrl_g_moves_the_start);
  MU_RUN_TEST(test_operator_takes_the_search_as_its_motion);
  MU_RUN_TEST(test_visual_extends_to_the_match);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  vimSetMessageCallback(&onMessage);

  win_setwidth(80);
  win_setheight(100);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
