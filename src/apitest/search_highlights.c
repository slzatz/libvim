#include "libvim.h"
#include "minunit.h"

static int stopSearchHighlightCount = 0;

void onStopSearchHighlight(void)
{
  stopSearchHighlightCount++;
}

static int errorCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|", title, msg);

  if (priority == MSG_ERROR)
  {
    errorCount++;
  }
};

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimBufferOpen("collateral/testfile.txt", 1, 0);

  vimExecute("e!");
  vimInput("g");
  vimInput("g");
  vimInput("0");
  errorCount = 0;
  stopSearchHighlightCount = 0;
}

void test_teardown(void) {}

MU_TEST(test_no_highlights_initially)
{
  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);

  mu_check(num == 0);
  mu_check(highlights == NULL);
}

MU_TEST(test_get_highlights)
{

  vimInput("/");
  vimInput("o");
  vimInput("f");

  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);

  mu_check(num == 3);
  mu_check(highlights[0].start.lnum == 1);
  mu_check(highlights[0].start.col == 23);
  mu_check(highlights[0].end.lnum == 1);
  mu_check(highlights[0].end.col == 25);

  mu_check(highlights[1].start.col == 24);

  mu_check(highlights[2].start.lnum == 3);
  mu_check(highlights[2].start.col == 23);
  mu_check(highlights[2].end.lnum == 3);
  mu_check(highlights[2].end.col == 25);

  vim_free(highlights);
  mu_check(num == 3);
}

MU_TEST(test_nohlsearch)
{
  mu_check(stopSearchHighlightCount == 0);
  vimExecute("nohlsearch");
  mu_check(stopSearchHighlightCount == 1);
}

MU_TEST(test_no_matching_highlights)
{
  vimInput("/");
  vimInput("a");
  vimInput("b");
  vimInput("c");

  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);

  mu_check(num == 0);
  mu_check(errorCount == 0);
}

MU_TEST(test_highlights_multiple_buffers)
{
  vimInput("/");
  // Both buffers we're testing have 'Line' or 'line' on every line...
  vimInput("i");
  vimInput("n");
  vimInput("e");

  buf_T *originalBuffer = curbuf;

  // Switch to an alternate file
  vimBufferOpen("collateral/lines_100.txt", 1, 0);
  mu_check(originalBuffer != curbuf);
  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);

  vim_free(highlights);
  mu_check(num == 100);
  mu_check(errorCount == 0);

  // Check original buffer
  vimSearchGetHighlights(originalBuffer, 0, 0, &num, &highlights);
  vim_free(highlights);
  mu_check(num == 3);
  mu_check(errorCount == 0);
}

/* Matches at the first column of the first line asked for, and matches that
 * start where the previous one ended or one column later, used to be left
 * out: searchit() looks after the position it is given. */
MU_TEST(test_adjacent_and_first_column_highlights)
{
  char_u *lines[] = {(char_u *)"foo foo foo", (char_u *)"foofoofoo", (char_u *)"xfoo  foo", (char_u *)"none here"};
  vimBufferSetLines(curbuf, 0, -1, lines, 4);

  vimInput("/");
  vimInput("f");
  vimInput("o");
  vimInput("o");
  vimKey("<cr>");

  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 1, 4, &num, &highlights);

  int expected[][2] = {{1, 0}, {1, 4}, {1, 8}, {2, 0}, {2, 3}, {2, 6}, {3, 1}, {3, 6}};
  mu_check(num == 8);
  for (int i = 0; i < num && i < 8; i++)
  {
    mu_check(highlights[i].start.lnum == expected[i][0]);
    mu_check(highlights[i].start.col == expected[i][1]);
    mu_check(highlights[i].end.lnum == expected[i][0]);
    mu_check(highlights[i].end.col == expected[i][1] + 3);
  }
  vim_free(highlights);

  /* a range that starts lower down begins with that line's first column,
   * and stops at its last line */
  vimSearchGetHighlights(curbuf, 2, 2, &num, &highlights);
  mu_check(num == 3);
  mu_check(highlights[0].start.lnum == 2);
  mu_check(highlights[0].start.col == 0);
  mu_check(highlights[2].start.col == 6);
  vim_free(highlights);

  /* the whole buffer */
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);
  mu_check(num == 8);
  vim_free(highlights);

  /* an empty match on every line does not loop */
  vimInput("/");
  vimInput("^");
  vimKey("<cr>");
  vimSearchGetHighlights(curbuf, 1, 4, &num, &highlights);
  mu_check(num == 4);
  vim_free(highlights);
  mu_check(errorCount == 0);
}

MU_TEST(test_highlight_enabled)
{
  vimExecute("set hlsearch");
  vimInput("/");
  vimInput("o");
  vimInput("f");
  vimKey("<cr>");
  mu_check(vimSearchGetHighlightEnabled() == TRUE);

  vimExecute("nohlsearch");
  mu_check(vimSearchGetHighlightEnabled() == FALSE);

  /* the next search command shows them again */
  vimInput("n");
  mu_check(vimSearchGetHighlightEnabled() == TRUE);

  vimExecute("set nohlsearch");
  mu_check(vimSearchGetHighlightEnabled() == FALSE);
  vimInput("n");
  mu_check(vimSearchGetHighlightEnabled() == FALSE);
  vimExecute("set hlsearch");
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_no_highlights_initially);
  MU_RUN_TEST(test_get_highlights);
  MU_RUN_TEST(test_nohlsearch);
  MU_RUN_TEST(test_no_matching_highlights);
  MU_RUN_TEST(test_highlights_multiple_buffers);
  MU_RUN_TEST(test_adjacent_and_first_column_highlights);
  MU_RUN_TEST(test_highlight_enabled);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(5);
  win_setheight(100);

  vimSetStopSearchHighlightCallback(&onStopSearchHighlight);
  vimSetMessageCallback(&onMessage);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
