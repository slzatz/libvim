#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * ":normal {keys}": the keys are run as Normal mode commands, all of them,
 * and a command they leave unfinished is ended.
 *
 * vim translates some commands into others and puts the translation in the
 * stuff buffer to be read next: "x" is "dl", "X" is "dh", "D" is "d$", "C"
 * is "c$", "s" is "cl", "S" is "cc", "Y" is "yy", "&" is ":s\r". Upstream ran
 * ":normal" for as long as the typeahead buffer had keys, so the translation
 * of the last key was left unread: ":normal x" did nothing, and with a range
 * each line ran the "dl" of the line before it.
 *
 * And a command left unfinished -- ":normal Afoo" is still in Insert mode --
 * was thrown away where vim ends it with <Esc> (CTRL-C on a command line).
 * The insert was then never ended, and "." afterwards stayed in Insert mode.
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
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimSetMappingsEnabled(FALSE);
  type("gg0");
}

void test_teardown(void) {}

/* Commands vim translates */

MU_TEST(test_x)
{
  vimExecute("normal x");
  mu_check(line_is(1, "his is the first line of a test file"));
  mu_check(cursor_is(1, 0));
}

MU_TEST(test_x_on_every_line)
{
  vimExecute("%normal x");
  // each line loses its own first letter, the first line too
  mu_check(line_is(1, "his is the first line of a test file"));
  mu_check(line_is(2, "his is the second line of a test file"));
  mu_check(line_is(3, "his is the third line of a test file"));

  // and nothing is left over to run with the next key
  type("l");
  mu_check(line_is(3, "his is the third line of a test file"));
}

MU_TEST(test_the_other_translated_commands)
{
  vimExecute("normal wD");
  mu_check(line_is(1, "This "));

  vimExecute("2normal $X");
  mu_check(line_is(2, "This is the second line of a test fie"));

  vimExecute("3normal Y");
  mu_check(evaluates_to("@\"", LINE3 "\n"));

  vimExecute("3s/third/3rd/");
  vimExecute("2normal &");
  mu_check(line_is(2, "This is the second line of a test fie"));
  vimExecute("1normal ithird ");
  vimExecute("1normal &");
  mu_check(line_is(1, "3rd This "));
}

MU_TEST(test_translated_commands_that_insert)
{
  vimExecute("normal sX");
  mu_check(line_is(1, "Xhis is the first line of a test file"));

  vimExecute("2normal wCY");
  mu_check(line_is(2, "This Y"));

  vimExecute("3normal SZ");
  mu_check(line_is(3, "Z"));
  mu_check((vimGetMode() & INSERT) == 0);
}

MU_TEST(test_a_command_after_leaving_insert_mode)
{
  vimExecute("exe \"normal ifoo\\<Esc>x\"");
  mu_check(line_is(1, "fo" LINE1));
}

MU_TEST(test_x_twice)
{
  vimExecute("normal xx");
  mu_check(line_is(1, "is is the first line of a test file"));
}

MU_TEST(test_typed)
{
  type(":normal x<cr>");
  mu_check(line_is(1, "his is the first line of a test file"));
  mu_check((vimGetMode() & NORMAL) == NORMAL);

  // the keys typed after it are not taken by it
  type(":normal x<cr>jx");
  mu_check(line_is(1, "is is the first line of a test file"));
  mu_check(line_is(2, "his is the second line of a test file"));
}

MU_TEST(test_in_a_mapping)
{
  vimSetMappingsEnabled(TRUE);
  vimExecute("nnoremap Q :normal x<CR>");
  type("Q");
  mu_check(line_is(1, "his is the first line of a test file"));
  type("jQ");
  mu_check(line_is(2, "his is the second line of a test file"));
}

MU_TEST(test_a_search)
{
  vimExecute("exe \"normal /second\\<CR>D\"");
  mu_check(line_is(2, "This is the "));
}

/* Commands left unfinished */

MU_TEST(test_insert_mode_is_ended)
{
  vimExecute("normal Ax");
  mu_check(line_is(1, LINE1 "x"));
  mu_check((vimGetMode() & INSERT) == 0);
  // <Esc> moves the cursor back onto the last character
  mu_check(cursor_is(1, (colnr_T)strlen(LINE1)));
}

MU_TEST(test_the_insert_can_be_repeated)
{
  vimExecute("normal Ax");
  type("j.");
  mu_check(line_is(2, LINE2 "x"));
  // "." is over: back in Normal mode, where "u" is undo
  mu_check((vimGetMode() & INSERT) == 0);
  type("u");
  mu_check(line_is(2, LINE2));
  type("u");
  mu_check(line_is(1, LINE1));
}

MU_TEST(test_insert_on_every_line)
{
  vimExecute("%normal A;");
  mu_check(line_is(1, LINE1 ";"));
  mu_check(line_is(2, LINE2 ";"));
  mu_check(line_is(3, LINE3 ";"));
  mu_check((vimGetMode() & INSERT) == 0);
  mu_check(evaluates_to("@.", ";"));
}

MU_TEST(test_a_command_line_is_abandoned)
{
  vimExecute("normal :s/first/1st/");
  mu_check(line_is(1, LINE1));
  mu_check((vimGetMode() & CMDLINE) == 0);

  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
}

MU_TEST(test_a_search_is_abandoned)
{
  vimExecute("normal /second");
  mu_check(cursor_is(1, 0));
  mu_check((vimGetMode() & CMDLINE) == 0);
}

MU_TEST(test_an_operator_is_dropped)
{
  vimExecute("normal d");
  mu_check((vimGetMode() & OP_PENDING) == 0);

  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
}

MU_TEST(test_a_register_name_still_to_come)
{
  // CTRL-R in Insert mode, and no name: <Esc> answers for it, and another
  // ends the insert
  vimExecute("exe \"normal ifoo\\<C-R>\"");
  mu_check(line_is(1, "foo" LINE1));
  mu_check((vimGetMode() & INSERT) == 0);

  type("x");
  mu_check(line_is(1, "fo" LINE1));
}

MU_TEST(test_an_expression_still_to_come)
{
  vimExecute("exe 'normal \"='");
  mu_check((vimGetMode() & CMDLINE) == 0);

  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
}

MU_TEST(test_the_state_it_was_called_from_is_kept)
{
  // from Insert mode, through CTRL-O: Insert mode goes on afterwards, where
  // ":normal" left the cursor -- on the "x", when its insert was ended
  type("i<c-o>:normal Ax<cr>");
  mu_check((vimGetMode() & INSERT) == INSERT);
  type("y<esc>");
  mu_check(line_is(1, LINE1 "yx"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_x);
  MU_RUN_TEST(test_x_on_every_line);
  MU_RUN_TEST(test_the_other_translated_commands);
  MU_RUN_TEST(test_translated_commands_that_insert);
  MU_RUN_TEST(test_a_command_after_leaving_insert_mode);
  MU_RUN_TEST(test_x_twice);
  MU_RUN_TEST(test_typed);
  MU_RUN_TEST(test_in_a_mapping);
  MU_RUN_TEST(test_a_search);

  MU_RUN_TEST(test_insert_mode_is_ended);
  MU_RUN_TEST(test_the_insert_can_be_repeated);
  MU_RUN_TEST(test_insert_on_every_line);
  MU_RUN_TEST(test_a_command_line_is_abandoned);
  MU_RUN_TEST(test_a_search_is_abandoned);
  MU_RUN_TEST(test_an_operator_is_dropped);
  MU_RUN_TEST(test_a_register_name_still_to_come);
  MU_RUN_TEST(test_an_expression_still_to_come);
  MU_RUN_TEST(test_the_state_it_was_called_from_is_kept);
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
