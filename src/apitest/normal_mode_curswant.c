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

MU_TEST(test_curswant_column2)
{
  mu_check(vimCursorGetLine() == 1);

  // Move one character right
  vimInput("l");

  mu_check(vimCursorGetColumn() == 1);
  mu_check(vimCursorGetLine() == 1);

  // Move two characters down
  vimInput("j");
  vimInput("j");

  mu_check(vimCursorGetColumn() == 0);
  mu_check(vimCursorGetLine() == 3);

  vimInput("j");
  mu_check(vimCursorGetColumn() == 1);
  mu_check(vimCursorGetLine() == 4);
}

MU_TEST(test_curswant_maxcolumn)
{
  mu_check(vimCursorGetLine() == 1);

  // Move all the way to the right
  vimInput("$");

  mu_check(vimCursorGetColumn() == 2);
  mu_check(vimCursorGetLine() == 1);

  // Move three characters down
  vimInput("j");
  vimInput("j");
  vimInput("j");

  // Cursor should be all the way to the right
  mu_check(vimCursorGetColumn() == 3);
  mu_check(vimCursorGetLine() == 4);
}

MU_TEST(test_curswant_reset)
{
  mu_check(vimCursorGetLine() == 1);

  // Move all the way to the right...
  vimInput("$");
  // And the once to the left...
  // This should reset curswant
  vimInput("h");

  mu_check(vimCursorGetColumn() == 1);
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumnWant() == 1);

  // Move three characters down
  vimInput("j");
  vimInput("j");
  vimInput("j");

  mu_check(vimCursorGetColumn() == 1);
  mu_check(vimCursorGetLine() == 4);
}

// The lines of collateral/curswant.txt are "abc", "ab", "a", "abcd".
//
// "$" wants the end of every line. A motion that then goes somewhere along
// the line wants the column it reached, and one of those is the column the
// cursor was in before the "$": curs_columns() had marked the column of the
// line's end valid without noting which cursor it was valid for, so a cursor
// back where it had last been looked at was taken to be at the end still.
MU_TEST(test_curswant_reset_by_zero)
{
  vimInput("$");
  vimInput("0");

  mu_check(vimCursorGetColumn() == 0);
  mu_check(vimCursorGetColumnWant() == 0);

  vimInput("j");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("j");
  vimInput("j");
  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_curswant_reset_by_caret)
{
  vimInput("$");
  vimInput("^");
  mu_check(vimCursorGetColumnWant() == 0);

  vimInput("j");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_curswant_reset_by_gg)
{
  vimInput("$");
  vimInput("g");
  vimInput("g");
  mu_check(vimCursorGetColumnWant() == 0);

  vimInput("j");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);
}

// Not only column 0: any column the cursor comes back to. "$h" from column 0
// was always right; from column 1, where "h" ends, it was not.
MU_TEST(test_curswant_reset_back_where_it_was)
{
  vimInput("l");
  mu_check(vimCursorGetColumnWant() == 1);

  vimInput("$");
  mu_check(vimCursorGetColumn() == 2);

  vimInput("h");
  mu_check(vimCursorGetColumn() == 1);
  mu_check(vimCursorGetColumnWant() == 1);

  vimInput("j");
  vimInput("j");
  vimInput("j");
  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == 1);
}

// On another line than the first, and going up.
MU_TEST(test_curswant_reset_on_last_line)
{
  vimInput("G");
  vimInput("0");
  vimInput("$");
  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == 3);

  vimInput("0");
  mu_check(vimCursorGetColumnWant() == 0);

  vimInput("k");
  vimInput("k");
  vimInput("k");
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);
}

// "$" is still wanted after going back to the end.
MU_TEST(test_curswant_end_again)
{
  vimInput("$");
  vimInput("0");
  vimInput("$");
  mu_check(vimCursorGetColumnWant() == MAXCOL);

  vimInput("j");
  vimInput("j");
  vimInput("j");
  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_setting_curswant_explicitly)
{
  mu_check(vimCursorGetLine() == 1);

  vimCursorSetColumnWant(MAXCOL);

  // Move three characters down
  vimInput("j");
  vimInput("j");
  vimInput("j");

  mu_check(vimCursorGetColumn() == 3);
  mu_check(vimCursorGetLine() == 4);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_curswant_column2);
  MU_RUN_TEST(test_curswant_maxcolumn);
  MU_RUN_TEST(test_curswant_reset);
  MU_RUN_TEST(test_curswant_reset_by_zero);
  MU_RUN_TEST(test_curswant_reset_by_caret);
  MU_RUN_TEST(test_curswant_reset_by_gg);
  MU_RUN_TEST(test_curswant_reset_back_where_it_was);
  MU_RUN_TEST(test_curswant_reset_on_last_line);
  MU_RUN_TEST(test_curswant_end_again);
  MU_RUN_TEST(test_setting_curswant_explicitly);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(5);
  win_setheight(100);

  vimBufferOpen("collateral/curswant.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
