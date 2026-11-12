#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * CTRL-O in Insert mode: one Normal mode command, then Insert mode again.
 *
 * "One command" is vim's: Insert mode is not due while an operator waits for
 * its motion, while a register has been named for the command that follows
 * ("a), in Visual mode, or on a command line -- and it is due as soon as the
 * command is over, which for CTRL-O : and CTRL-O / is the <CR>.
 *
 * Upstream went back to Insert mode after any key that left nothing in the
 * stuff buffer, and did not look at all when a command line returned. So
 * CTRL-O v and CTRL-O "a were in Insert mode again at once, the "ld" or "yy"
 * that followed typed as text; and after CTRL-O :s/a/b/<CR> the next key was
 * run as a Normal mode command, and only then was it Insert mode.
 *
 * The buffer:
 *   This is the first line of a test file
 *   This is the second line of a test file
 *   This is the third line of a test file
 */

#define LINE1 "This is the first line of a test file"
#define LINE2 "This is the second line of a test file"

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

static int evaluates_to(char *expr, char *want)
{
  char_u *got = vimEval((char_u *)expr);
  int same = got != NULL && strcmp((char *)got, want) == 0;

  if (!same)
    printf("%s is |%s|, not |%s|\n", expr, got == NULL ? "(null)" : (char *)got, want);
  vim_free(got);
  return same;
}

static int inserting(void)
{
  return (vimGetMode() & INSERT) == INSERT;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimExecute("let @a = ''");
  vimSetMappingsEnabled(FALSE);
  type("gg0");
}

void test_teardown(void) {}

MU_TEST(test_a_command)
{
  type("i<c-o>");
  mu_check(!inserting());
  type("w");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "This Xis the first line of a test file"));
}

MU_TEST(test_an_operator_and_its_motion)
{
  type("i<c-o>d");
  mu_check(!inserting());
  type("w");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "Xis the first line of a test file"));
}

MU_TEST(test_a_translated_command)
{
  // "x" is "dl" in the stuff buffer: Insert mode comes after that
  type("i<c-o>x");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "Xhis is the first line of a test file"));
  // and no operator is left pending by the "d"
  mu_check(vimGetMode() == NORMAL);
}

MU_TEST(test_a_command_line)
{
  type("i<c-o>:");
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  type("s/first/1st/<cr>");
  // the <CR> ends the one command
  mu_check(inserting());
  mu_check(line_is(1, "This is the 1st line of a test file"));

  // so the next key is text, not a Normal mode command
  type("X");
  mu_check(line_is(1, "XThis is the 1st line of a test file"));
  type("<esc>");
  mu_check(vimGetMode() == NORMAL);
}

MU_TEST(test_a_command_line_abandoned)
{
  type("i<c-o>:s/first/1st/<esc>");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "X" LINE1));
}

MU_TEST(test_a_command_that_fails)
{
  type("i<c-o>:nosuchcommand<cr>");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "X" LINE1));
}

MU_TEST(test_a_search)
{
  type("i<c-o>/second<cr>");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(2, "This is the Xsecond line of a test file"));
}

MU_TEST(test_visual_mode)
{
  type("i<c-o>v");
  mu_check(!inserting());
  mu_check(vimVisualIsActive());
  type("l");
  mu_check(!inserting());
  // the operator on the selection is the end of the command
  type("d");
  mu_check(inserting());
  mu_check(!vimVisualIsActive());
  mu_check(line_is(1, "is is the first line of a test file"));
  type("X<esc>");
  mu_check(line_is(1, "Xis is the first line of a test file"));
}

MU_TEST(test_visual_mode_abandoned)
{
  type("i<c-o>v<esc>");
  mu_check(inserting());
  mu_check(!vimVisualIsActive());
  type("X<esc>");
  mu_check(line_is(1, "X" LINE1));
}

MU_TEST(test_a_register_for_the_command)
{
  type("i<c-o>\"");
  mu_check(!inserting());
  type("a");
  // the register is named; the command is still to come
  mu_check(!inserting());
  type("yy");
  mu_check(inserting());
  mu_check(evaluates_to("@a", LINE1 "\n"));
  type("X<esc>");
  mu_check(line_is(1, "X" LINE1));
}

MU_TEST(test_the_expression_register)
{
  type("i<c-o>\"=6*7<cr>");
  // the expression has been typed; the command that uses it has not
  mu_check(!inserting());
  type("P");
  mu_check(inserting());
  type("<esc>");
  mu_check(line_is(1, "42" LINE1));
}

MU_TEST(test_in_a_mapping)
{
  vimSetMappingsEnabled(TRUE);
  vimExecute("inoremap Q <C-o>:s/first/1st/<CR>");
  type("iQz<esc>");
  mu_check(line_is(1, "zThis is the 1st line of a test file"));
  mu_check(vimGetMode() == NORMAL);
}

MU_TEST(test_replace_mode)
{
  // it is Replace mode that CTRL-O was typed in, and Replace mode again
  type("RXY<c-o>l");
  mu_check(inserting());
  mu_check((vimGetMode() & REPLACE_FLAG) == REPLACE_FLAG);
  type("Z<esc>");
  mu_check(line_is(1, "XYiZ is the first line of a test file"));

  type("0gRXY<c-o>:s/first/1st/<cr>");
  mu_check((vimGetMode() & REPLACE_FLAG) == REPLACE_FLAG);
  type("Z<esc>");
  mu_check(line_is(1, "ZYiZ is the 1st line of a test file"));
}

MU_TEST(test_startinsert)
{
  // ":startinsert" asks for Insert mode the way CTRL-O does; typed, it now
  // gets it when its command line returns
  type(":startinsert<cr>");
  mu_check(inserting());
  type("X<esc>");
  mu_check(line_is(1, "X" LINE1));

  type(":startinsert!<cr>");
  mu_check(inserting());
  type("Y<esc>");
  mu_check(line_is(1, "X" LINE1 "Y"));

  type("0:startreplace<cr>");
  mu_check((vimGetMode() & REPLACE_FLAG) == REPLACE_FLAG);
  type("Z<esc>");
  mu_check(line_is(1, "Z" LINE1 "Y"));
}

MU_TEST(test_twice)
{
  type("A<c-o>0<c-o>wX<esc>");
  mu_check(line_is(1, "This Xis the first line of a test file"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_a_command);
  MU_RUN_TEST(test_an_operator_and_its_motion);
  MU_RUN_TEST(test_a_translated_command);
  MU_RUN_TEST(test_a_command_line);
  MU_RUN_TEST(test_a_command_line_abandoned);
  MU_RUN_TEST(test_a_command_that_fails);
  MU_RUN_TEST(test_a_search);
  MU_RUN_TEST(test_visual_mode);
  MU_RUN_TEST(test_visual_mode_abandoned);
  MU_RUN_TEST(test_a_register_for_the_command);
  MU_RUN_TEST(test_the_expression_register);
  MU_RUN_TEST(test_in_a_mapping);
  MU_RUN_TEST(test_replace_mode);
  MU_RUN_TEST(test_startinsert);
  MU_RUN_TEST(test_twice);
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
