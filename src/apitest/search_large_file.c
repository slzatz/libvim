#include "libvim.h"
#include "minunit.h"

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");

  vimExecute("e!");
  vimInput("g");
  vimInput("g");
  vimInput("0");
}

void test_teardown(void) {}

MU_TEST(test_search_in_large_file)
{

  vimInput("/");
  vimInput("e");

  int num;
  searchHighlight_T *highlights;
  vimSearchGetHighlights(curbuf, 0, 0, &num, &highlights);
  vim_free(highlights);
  /* The file has 16405 lower-case e's (grep -o e | wc -l). This expected
   * 15420 while vimSearchGetHighlights skipped any match starting within
   * two columns of the end of the previous one ("ee", "e e"). */
  mu_check(num == 16405);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_search_in_large_file);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(5);
  win_setheight(100);

  vimBufferOpen("collateral/large-c-file.c", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
