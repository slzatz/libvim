#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * CTRL-R: the contents of a register, in Insert mode and on the command
 * line; and the expression register from Normal mode, "= and @=.
 *
 * vim reads the register name -- and, for "=", a whole command line -- by
 * waiting for keys. libvim is handed its keys and cannot wait. Upstream took
 * the waiting code out of Insert mode and the command line, where CTRL-R was
 * then a character like any other, and left it in Normal mode, where "= never
 * returned. Each state now remembers that it is owed a key.
 *
 * The buffer:
 *   This is the first line of a test file
 *   This is the second line of a test file
 *   This is the third line of a test file
 */

#define LINE1 "This is the first line of a test file"

static char said[4096];
static int messageCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
  messageCount++;
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

static int line_is(linenr_T lnum, char *text)
{
  char_u *line = vimBufferGetLine(curbuf, lnum);

  if (strcmp((char *)line, text) != 0)
    printf("line %ld is |%s|, not |%s|\n", (long)lnum, line, text);
  return strcmp((char *)line, text) == 0;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimExecute("let @a = 'REG'");
  vimExecute("let @b = \"two\\nlines\\n\"");
  vimExecute("let @c = \"a\\<C-H>b\""); // a, a backspace, b
  type("gg0");
  said[0] = 0;
  messageCount = 0;
}

void test_teardown(void) {}

/* Insert mode */

MU_TEST(test_insert_register)
{
  type("i<c-r>a");
  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, "REG" LINE1));

  // and typing goes on after it
  type("x<esc>");
  mu_check(line_is(1, "REGx" LINE1));
}

MU_TEST(test_insert_register_of_lines)
{
  type("i<c-r>b");
  mu_check(vimBufferGetLineCount(curbuf) == 5);
  mu_check(line_is(1, "two"));
  mu_check(line_is(2, "lines"));
  mu_check(line_is(3, LINE1));
}

MU_TEST(test_insert_as_typed_and_literally)
{
  // as if typed: the backspace in the register takes the a away
  type("i<c-r>c<esc>");
  mu_check(line_is(1, "b" LINE1));

  // literally: it is a character
  type("u");
  type("i<c-r><c-r>c<esc>");
  mu_check(line_is(1, "a\bb" LINE1));

  type("u");
  type("i<c-r><c-o>a<esc>");
  mu_check(line_is(1, "REG" LINE1));

  type("u");
  type("i<c-r><c-p>a<esc>");
  mu_check(line_is(1, "REG" LINE1));
}

MU_TEST(test_insert_special_registers)
{
  type("yiw");
  type("A <c-r>0 <c-r>\"<esc>");
  mu_check(line_is(1, LINE1 " This This"));

  // the text inserted last
  type("ohello<esc>");
  type("o<c-r>.<esc>");
  mu_check(line_is(3, "hello"));
}

MU_TEST(test_insert_register_that_is_not_one)
{
  type("i<c-r>z");
  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, LINE1));

  // <Esc> for the name: no register, and still Insert mode
  type("<c-r><esc>");
  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, LINE1));
  type("x");
  mu_check(line_is(1, "x" LINE1));
}

MU_TEST(test_insert_expression)
{
  type("i<c-r>=");
  // the expression is typed on a command line
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  mu_check(vimCommandLineGetType() == '=');
  type("6*7");
  mu_check(strcmp((char *)vimCommandLineGetText(), "6*7") == 0);
  type("<cr>");

  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, "42" LINE1));

  // an empty line is the expression used last
  type(" <c-r>=<cr><esc>");
  mu_check(line_is(1, "42 42" LINE1));
}

MU_TEST(test_insert_expression_abandoned)
{
  type("i<c-r>=6*7<esc>");
  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, LINE1));
  type("x<esc>");
  mu_check(line_is(1, "x" LINE1));
}

MU_TEST(test_insert_register_in_a_mapping)
{
  vimExecute("inoremap <F2> <C-r>a");
  vimExecute("inoremap <F3> <C-r>=toupper('date')<CR>!");
  vimSetMappingsEnabled(TRUE);
  type("i<f2>-<f3>");
  vimSetMappingsEnabled(FALSE);

  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(line_is(1, "REG-DATE!" LINE1));
}

MU_TEST(test_register_name_is_not_mapped)
{
  vimExecute("inoremap a X");
  vimSetMappingsEnabled(TRUE);
  type("i<c-r>a");
  mu_check(line_is(1, "REG" LINE1));
  // and mapping is back on afterwards
  type("a");
  vimSetMappingsEnabled(FALSE);
  mu_check(line_is(1, "REGX" LINE1));
}

MU_TEST(test_insert_register_repeats_and_undoes)
{
  type("i<c-r>a<esc>");
  type("j0.");
  mu_check(line_is(2, "REGThis is the second line of a test file"));
  type("u");
  type("u");
  mu_check(line_is(1, LINE1));
}

/* The command line */

MU_TEST(test_cmdline_register)
{
  type(":echo '<c-r>a'");
  mu_check(strcmp((char *)vimCommandLineGetText(), "echo 'REG'") == 0);
  type("<cr>");
  mu_check(strcmp(said, "[REG]") == 0);
}

MU_TEST(test_cmdline_word_and_line_under_the_cursor)
{
  type("w");
  type(":echo '<c-r><c-w>'<cr>");
  mu_check(strcmp(said, "[is]") == 0);

  said[0] = 0;
  type(":echo '<c-r><c-l>'<cr>");
  mu_check(strcmp(said, "[" LINE1 "]") == 0);

  type(":s/<c-r><c-w>/was/<cr>");
  mu_check(line_is(1, "Thwas is the first line of a test file"));
}

MU_TEST(test_cmdline_register_in_a_search)
{
  vimExecute("let @a = 'second'");
  type("/<c-r>a<cr>");
  pos_T pos = vimCursorGetPosition();
  mu_check(pos.lnum == 2 && pos.col == 12);
}

MU_TEST(test_cmdline_expression)
{
  type(":echo '<c-r>=");
  // the command line is put aside for the expression's
  mu_check(vimCommandLineGetType() == '=');
  type("6*7");
  mu_check(strcmp((char *)vimCommandLineGetText(), "6*7") == 0);
  type("<cr>");
  mu_check(vimCommandLineGetType() == ':');
  mu_check(strcmp((char *)vimCommandLineGetText(), "echo '42") == 0);
  type("'<cr>");
  mu_check(strcmp(said, "[42]") == 0);
}

MU_TEST(test_cmdline_expression_abandoned)
{
  type(":echo '<c-r>=6*7<esc>");
  // <Esc> ends the expression only
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  mu_check(vimCommandLineGetType() == ':');
  mu_check(strcmp((char *)vimCommandLineGetText(), "echo '") == 0);
  type("x'<cr>");
  mu_check(strcmp(said, "[x]") == 0);
}

MU_TEST(test_cmdline_register_of_a_line_is_not_run)
{
  // A yanked line ends in a line break. vim puts it on the command line as
  // the text and a literal <CR> (it types CTRL-V <CR>), to be looked at;
  // the <CR> used to end the command line, and the line was run.
  vimExecute("call setline(1, '2,3d')");
  type("yy:<c-r>0");
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  mu_check(strcmp((char *)vimCommandLineGetText(), "2,3d\r") == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 3);

  type("<esc>");
  mu_check(vimBufferGetLineCount(curbuf) == 3);

  // two lines: a <CR> after each
  type("yj:<c-r>0");
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  mu_check(strcmp((char *)vimCommandLineGetText(), "2,3d\rThis is the second line of a test file\r") == 0);
  type("<esc>");

  // and in a replacement, where a <CR> breaks the line
  vimExecute("call setline(1, 'one two')");
  vimExecute("let @b = \"X\\n\"");
  type(":s/ /<c-r>b/<cr>");
  mu_check(vimBufferGetLineCount(curbuf) == 4);
  mu_check(line_is(1, "oneX"));
  mu_check(line_is(2, "two"));
}

MU_TEST(test_cmdline_register_abandoned)
{
  type(":echo '<c-r><esc>y'<cr>");
  mu_check(strcmp(said, "[y]") == 0);
}

/* Normal mode */

MU_TEST(test_put_an_expression)
{
  type("\"=");
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  mu_check(vimCommandLineGetType() == '=');
  type("6*7<cr>");
  mu_check((vimGetMode() & NORMAL) == NORMAL);
  type("P");
  mu_check(line_is(1, "42" LINE1));

  // an empty line is the expression used last (P left the cursor on the 2)
  type("\"=<cr>P");
  mu_check(line_is(1, "4422" LINE1));
}

MU_TEST(test_expression_register_abandoned)
{
  type("\"=6*7<esc>");
  mu_check((vimGetMode() & NORMAL) == NORMAL);
  // the next command is an ordinary one
  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
}

MU_TEST(test_execute_an_expression)
{
  type("@='x'<cr>");
  mu_check(line_is(1, "his is the first line of a test file"));
  type("2@='x'<cr>");
  mu_check(line_is(1, "s is the first line of a test file"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_insert_register);
  MU_RUN_TEST(test_insert_register_of_lines);
  MU_RUN_TEST(test_insert_as_typed_and_literally);
  MU_RUN_TEST(test_insert_special_registers);
  MU_RUN_TEST(test_insert_register_that_is_not_one);
  MU_RUN_TEST(test_insert_expression);
  MU_RUN_TEST(test_insert_expression_abandoned);
  MU_RUN_TEST(test_insert_register_in_a_mapping);
  MU_RUN_TEST(test_register_name_is_not_mapped);
  MU_RUN_TEST(test_insert_register_repeats_and_undoes);

  MU_RUN_TEST(test_cmdline_register);
  MU_RUN_TEST(test_cmdline_word_and_line_under_the_cursor);
  MU_RUN_TEST(test_cmdline_register_in_a_search);
  MU_RUN_TEST(test_cmdline_expression);
  MU_RUN_TEST(test_cmdline_expression_abandoned);
  MU_RUN_TEST(test_cmdline_register_of_a_line_is_not_run);
  MU_RUN_TEST(test_cmdline_register_abandoned);

  MU_RUN_TEST(test_put_an_expression);
  MU_RUN_TEST(test_expression_register_abandoned);
  MU_RUN_TEST(test_execute_an_expression);
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
