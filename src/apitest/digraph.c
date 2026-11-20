#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * Digraphs: CTRL-K and two characters, in Insert mode and on the command
 * line, and the 'digraph' option. vim reads the two characters by waiting
 * for keys; here each state remembers that it is owed them. They were not
 * compiled in before 2026-11-20, and CTRL-K was a character.
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

MU_TEST(test_insert_digraph)
{
  type("A <c-k>a:<c-k>e'<c-k>Eu<esc>");
  mu_check(line_is(1, "This is the first line of a test file \xc3\xa4\xc3\xa9\xe2\x82\xac"));
  mu_check(in_mode(NORMAL));
}

MU_TEST(test_digraph_with_the_characters_reversed)
{
  type("A<c-k>:a<esc>"); /* vim tries the other order too */
  mu_check(line_is(1, "This is the first line of a test file\xc3\xa4"));
}

MU_TEST(test_digraph_that_is_none)
{
  type("A<c-k>xy<esc>"); /* no digraph: the second character is inserted */
  mu_check(line_is(1, "This is the first line of a test filey"));
}

MU_TEST(test_digraph_cancelled_stays_in_insert)
{
  type("A<c-k><esc>");
  mu_check(in_mode(INSERT));
  type("<c-k>a<esc>");
  mu_check(in_mode(INSERT));
  type("z<esc>");
  mu_check(line_is(1, "This is the first line of a test filez"));
  mu_check(in_mode(NORMAL));
}

MU_TEST(test_digraph_special_key)
{
  type("A<c-k><left>X<esc>"); /* a special key after CTRL-K is inserted as its name */
  mu_check(line_is(1, "This is the first line of a test file<Left>X"));
}

MU_TEST(test_digraph_repeats_and_undoes)
{
  type("A<c-k>a:<esc>j.");
  mu_check(line_is(2, "This is the second line of a test file\xc3\xa4"));
  type("u");
  mu_check(line_is(2, "This is the second line of a test file"));
}

MU_TEST(test_digraph_characters_are_not_mapped)
{
  vimSetMappingsEnabled(TRUE);
  vimExecute("inoremap a X");
  type("A<c-k>a:a<esc>");
  mu_check(line_is(1, "This is the first line of a test file\xc3\xa4X"));
}

MU_TEST(test_insert_ends_while_a_digraph_is_awaited)
{
  /* the no_mapping the CTRL-K took is given back: mappings work after */
  vimSetMappingsEnabled(TRUE);
  vimExecute("inoremap q Q");
  type("A<c-k>");
  vimExecute("stopinsert");
  type("<esc>"); /* not a digraph's: the insert ends */
  mu_check(in_mode(NORMAL));
  type("Aq<esc>");
  mu_check(line_is(1, "This is the first line of a test fileQ"));
}

MU_TEST(test_cmdline_digraph)
{
  type(":echo '<c-k>a:<c-k>Eu'");
  mu_check(in_mode(CMDLINE));
  mu_check(strcmp((char *)vimCommandLineGetText(), "echo '\xc3\xa4\xe2\x82\xac'") == 0);
  type("<esc>");
}

MU_TEST(test_cmdline_digraph_cancelled)
{
  type(":echo<c-k>a<esc>");
  mu_check(in_mode(CMDLINE));
  mu_check(strcmp((char *)vimCommandLineGetText(), "echo") == 0);
  type("<esc>");
  mu_check(in_mode(NORMAL));
}

MU_TEST(test_digraph_option)
{
  /* 'digraph': a, backspace, : is a digraph too */
  vimExecute("set digraph");
  type("Aa<bs>:<esc>");
  mu_check(line_is(1, "This is the first line of a test file\xc3\xa4"));
}

MU_TEST(test_digraphs_lists)
{
  vimExecute("digraphs");
  mu_check(strstr(said, "a:") != NULL);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_insert_digraph);
  MU_RUN_TEST(test_digraph_with_the_characters_reversed);
  MU_RUN_TEST(test_digraph_that_is_none);
  MU_RUN_TEST(test_digraph_cancelled_stays_in_insert);
  MU_RUN_TEST(test_digraph_special_key);
  MU_RUN_TEST(test_digraph_repeats_and_undoes);
  MU_RUN_TEST(test_digraph_characters_are_not_mapped);
  MU_RUN_TEST(test_insert_ends_while_a_digraph_is_awaited);
  MU_RUN_TEST(test_cmdline_digraph);
  MU_RUN_TEST(test_cmdline_digraph_cancelled);
  MU_RUN_TEST(test_digraph_option);
  MU_RUN_TEST(test_digraphs_lists);
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