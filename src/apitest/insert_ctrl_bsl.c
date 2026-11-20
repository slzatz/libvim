#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * Insert mode: CTRL-\ CTRL-N, CTRL-\ CTRL-G and CTRL-\ CTRL-O, whose second
 * key vim waits for; and CTRL-N, CTRL-P and CTRL-X, whose completion is not
 * compiled in, and which insert nothing rather than a control character.
 *
 * The buffer:
 *   This is the first line of a test file
 *   This is the second line of a test file
 *   This is the third line of a test file
 */

static char said[4096];

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
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

static int in_mode(int mode) { return (vimGetMode() & mode) == mode; }

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimExecute("set nodigraph");
  type("gg0");
  said[0] = 0;
}

void test_teardown(void) { vimSetMappingsEnabled(FALSE); }

MU_TEST(test_ctrl_bsl_ctrl_n_leaves_insert)
{
  type("Ax<c-\\><c-n>");
  mu_check(in_mode(NORMAL));
  mu_check(line_is(1, "This is the first line of a test filex"));
  /* as <Esc> does: the cursor is on the x */
  mu_check(vimCursorGetColumn() == 37);
}

MU_TEST(test_ctrl_bsl_ctrl_g_leaves_insert)
{
  type("Ax<c-\\><c-g>");
  mu_check(in_mode(NORMAL));
}

MU_TEST(test_ctrl_bsl_ctrl_o_keeps_the_cursor)
{
  type("A<c-\\><c-o>");
  /* one command in Normal mode, and the cursor has not moved back */
  mu_check(vimCursorGetColumn() == 37);
  type("~");
  mu_check(in_mode(INSERT));
  type("<esc>");
}

MU_TEST(test_ctrl_bsl_then_another_key)
{
  type("A<c-\\>z<esc>"); /* the CTRL-\ is text, and z is typed */
  mu_check(line_is(1, "This is the first line of a test file\x1cz"));
}

MU_TEST(test_ctrl_bsl_then_esc)
{
  type("A<c-\\><esc>");
  mu_check(in_mode(NORMAL));
  mu_check(line_is(1, "This is the first line of a test file\x1c"));
}

MU_TEST(test_key_after_ctrl_bsl_is_not_mapped)
{
  vimSetMappingsEnabled(TRUE);
  vimExecute("inoremap <c-n> X");
  type("A<c-\\><c-n>");
  mu_check(in_mode(NORMAL));
  mu_check(line_is(1, "This is the first line of a test file"));
  type("A<c-n><esc>"); /* and the mapping works after */
  mu_check(line_is(1, "This is the first line of a test fileX"));
}

MU_TEST(test_completion_keys_insert_nothing)
{
  type("A<c-n><c-p><c-x>z<esc>");
  mu_check(line_is(1, "This is the first line of a test filez"));
  mu_check(strstr(said, "Completion is not available") != NULL);
}

MU_TEST(test_ctrl_x_takes_its_commands)
{
  /* each of these is a CTRL-X command in vim, and inserts nothing */
  type("Aa<c-x><c-u><c-x>s<c-x><c-i><c-x><c-t><c-x><c-l><c-x><c-e>b<esc>");
  mu_check(line_is(1, "This is the first line of a test fileab"));
  /* any other key ends CTRL-X mode and does what it does */
  type("A<c-x>z<esc>");
  mu_check(line_is(1, "This is the first line of a test fileabz"));
}

MU_TEST(test_completion_keys_can_be_mapped)
{
  vimSetMappingsEnabled(TRUE);
  vimExecute("inoremap <c-n> N");
  type("A<c-n><esc>");
  mu_check(line_is(1, "This is the first line of a test fileN"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_ctrl_bsl_ctrl_n_leaves_insert);
  MU_RUN_TEST(test_ctrl_bsl_ctrl_g_leaves_insert);
  MU_RUN_TEST(test_ctrl_bsl_ctrl_o_keeps_the_cursor);
  MU_RUN_TEST(test_ctrl_bsl_then_another_key);
  MU_RUN_TEST(test_ctrl_bsl_then_esc);
  MU_RUN_TEST(test_key_after_ctrl_bsl_is_not_mapped);
  MU_RUN_TEST(test_completion_keys_insert_nothing);
  MU_RUN_TEST(test_ctrl_x_takes_its_commands);
  MU_RUN_TEST(test_completion_keys_can_be_mapped);
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