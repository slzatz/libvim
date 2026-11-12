#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * "v", "V" and CTRL-V between an operator and its motion: the motion is taken
 * characterwise, linewise or blockwise, whatever it is by itself. "dvj"
 * deletes from the cursor to the same column of the next line, not two lines;
 * "dVw" deletes the line; "d<C-V>j" a column of two characters.
 *
 * The key is not the motion: the operator goes on waiting. Upstream ran the
 * operator when it came, with the "v" as its motion, and the motion typed
 * next was a command of its own: "dvj" deleted one character and moved down.
 *
 * The buffer:
 *   This is the first line of a test file
 *   This is the second line of a test file
 *   This is the third line of a test file
 */

#define LINE1 "This is the first line of a test file"
#define LINE2 "This is the second line of a test file"
#define LINE3 "This is the third line of a test file"

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

static int line_is(linenr_T lnum, char *text)
{
  char_u *line = vimBufferGetLine(curbuf, lnum);

  if (strcmp((char *)line, text) != 0)
    printf("line %ld is |%s|, not |%s|\n", (long)lnum, line, text);
  return strcmp((char *)line, text) == 0;
}

static int cursor_is(linenr_T lnum, colnr_T col)
{
  pos_T pos = vimCursorGetPosition();

  if (pos.lnum != lnum || pos.col != col)
    printf("cursor is %ld,%d, not %ld,%d\n", (long)pos.lnum, pos.col, (long)lnum, col);
  return pos.lnum == lnum && pos.col == col;
}

static int evaluates_to(char *expr, char *want)
{
  char_u *got = vimEval((char_u *)expr);
  int same = got != NULL && strcmp((char *)got, want) == 0;

  if (!same)
    printf("%s is |%s|, not |%s|\n", expr, got == NULL ? "(null)" : (char *)got, want);
  vim_free(got);
  return same;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  type("gg0");
}

void test_teardown(void) {}

MU_TEST(test_characterwise)
{
  // "j" is linewise; with "v" it is from here to the same column below,
  // which leaves out the character it lands on
  type("wdvj");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(line_is(1, "This is the second line of a test file"));
  mu_check(line_is(2, LINE3));
  mu_check(cursor_is(1, 5));
  mu_check(evaluates_to("getregtype('\"')", "v"));
  mu_check(evaluates_to("@\"", "is the first line of a test file\nThis "));
}

MU_TEST(test_linewise)
{
  // "w" is characterwise; with "V" the whole line goes
  type("wdVw");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(line_is(1, LINE2));
  mu_check(cursor_is(1, 0));
}

MU_TEST(test_blockwise)
{
  type("d<c-v>j");
  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(line_is(1, "his is the first line of a test file"));
  mu_check(line_is(2, "his is the second line of a test file"));
  mu_check(line_is(3, LINE3));
  mu_check(cursor_is(1, 0));
}

MU_TEST(test_inclusive_becomes_exclusive)
{
  // "e" includes the character it stops on; "v" turns that around
  type("dve");
  mu_check(line_is(1, "s is the first line of a test file"));

  // and "l" leaves it out, until "v"
  type("dvl");
  mu_check(line_is(1, "is the first line of a test file"));
}

MU_TEST(test_the_operator_goes_on_waiting)
{
  pendingOp_T pendingOp;

  type("d");
  mu_check((vimGetMode() & OP_PENDING) == OP_PENDING);
  type("v");
  mu_check((vimGetMode() & OP_PENDING) == OP_PENDING);
  mu_check(!vimVisualIsActive());
  mu_check(vimGetPendingOperator(&pendingOp) == TRUE);
  mu_check(pendingOp.op_type == OP_DELETE);
  mu_check(line_is(1, LINE1));

  type("j");
  mu_check((vimGetMode() & OP_PENDING) == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 2);
}

MU_TEST(test_abandoned)
{
  type("dv<esc>");
  mu_check((vimGetMode() & OP_PENDING) == 0);
  mu_check(line_is(1, LINE1));

  // the next command is not taken linewise or as a motion
  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
  type("dw");
  mu_check(line_is(1, "is the first line of a test file"));
}

MU_TEST(test_the_last_one_counts)
{
  type("dVvj");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(line_is(1, LINE2));
}

MU_TEST(test_counts)
{
  // before the operator and after the "v"
  type("dv2l");
  mu_check(line_is(1, "s is the first line of a test file"));
  type("2dvl");
  mu_check(line_is(1, "s the first line of a test file"));
}

MU_TEST(test_other_operators)
{
  type("wyvj");
  mu_check(evaluates_to("getregtype('\"')", "v"));
  mu_check(evaluates_to("@\"", "is the first line of a test file\nThis "));

  type("0yVw");
  mu_check(evaluates_to("getregtype('\"')", "V"));

  type("cVwX<esc>");
  mu_check(line_is(1, "X"));
  mu_check(line_is(2, LINE2));

  type("jgUvj");
  mu_check(line_is(2, "THIS IS THE SECOND LINE OF A TEST FILE"));
  mu_check(line_is(3, LINE3));
}

MU_TEST(test_repeat)
{
  type("dvj.");
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(line_is(1, LINE3));
}

MU_TEST(test_a_search_as_the_motion)
{
  type("dV/third<cr>");
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(line_is(1, ""));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_characterwise);
  MU_RUN_TEST(test_linewise);
  MU_RUN_TEST(test_blockwise);
  MU_RUN_TEST(test_inclusive_becomes_exclusive);
  MU_RUN_TEST(test_the_operator_goes_on_waiting);
  MU_RUN_TEST(test_abandoned);
  MU_RUN_TEST(test_the_last_one_counts);
  MU_RUN_TEST(test_counts);
  MU_RUN_TEST(test_other_operators);
  MU_RUN_TEST(test_repeat);
  MU_RUN_TEST(test_a_search_as_the_motion);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(80);
  win_setheight(100);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
