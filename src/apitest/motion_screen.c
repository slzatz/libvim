#include "libvim.h"
#include "minunit.h"

void test_setup(void)
{
  vimBufferOpen("collateral/lines_100.txt", 1, 0);
  vimKey("<Esc>");
  vimKey("<Esc>");
  vimExecute("e!");
  vimInput("g");
  vimInput("g");
  vimInput("0");
  vimSetCursorMoveScreenColumnCallback(NULL);
}

void test_teardown(void) {}

void SimpleScreenLineCallback(screenLineMotion_T motion, int count, linenr_T startLine, linenr_T *outLine)
{
  switch (motion)
  {
  case MOTION_H:
    *outLine = 10;
    break;
  case MOTION_M:
    *outLine = 20;
    break;
  case MOTION_L:
    *outLine = 30;
    break;
  }
}

void ErroneousScreenLineCallback(screenLineMotion_T motion, int count, linenr_T startLine, linenr_T *outLine)
{
  switch (motion)
  {
  case MOTION_H:
    *outLine = -1;
    break;
  case MOTION_M:
    *outLine = 101;
    break;
  case MOTION_L:
    *outLine = 999;
    break;
  }
}

void SimplePositionCallback(int dir, int count, linenr_T srcLine, colnr_T srcColumn, colnr_T curswant, linenr_T *destLine, colnr_T *destColumn)
{
  if (dir == BACKWARD)
  {
    *destLine = 1;
    *destColumn = 0;
  }
  else
  {
    *destLine = 100;
    *destColumn = 0;
  }
}

void SameLinePositionCallback(int dir, int count, linenr_T srcLine, colnr_T srcColumn, colnr_T curswant, linenr_T *destLine, colnr_T *destColumn)
{
  if (dir == BACKWARD)
  {
    *destLine = srcLine;
    *destColumn = 0;
  }
  else
  {
    *destLine = srcLine;
    *destColumn = srcColumn + 1;
  }
}

void MaxColPositionCallback(int dir, int count, linenr_T srcLine, colnr_T srcColumn, colnr_T curswant, linenr_T *destLine, colnr_T *destColumn)
{
  if (dir == BACKWARD)
  {
    *destLine = srcLine - 1;
    *destColumn = MAXCOL;
  }
  else
  {
    *destLine = srcLine + 1;
    *destColumn = MAXCOL;
  }
}

void ErroneousPositionCallback(int dir, int count, linenr_T srcLine, colnr_T srcColumn, colnr_T curswant, linenr_T *destLine, colnr_T *destColumn)
{
  if (dir == BACKWARD)
  {
    *destLine = srcLine;
    *destColumn = -1;
  }
  else
  {
    *destLine = srcLine;
    *destColumn = 10000;
  }
}

/*
 * g0, g^, gm and g$. An embedder whose screen is screenWidth bytes wide, and
 * which wraps a line there whatever the character: "Line 1" is the screen
 * lines "Lin" and "e 1" at width 3, "Line" and " 1" at width 4.
 */
static int screenWidth = 3;

void WidthColumnCallback(screenColumnMotion_T motion, linenr_T lnum, colnr_T col, colnr_T *destCol)
{
  colnr_T len = (colnr_T)strlen((char *)vimBufferGetLine(curbuf, lnum));
  colnr_T start = col / screenWidth * screenWidth;
  colnr_T end = start + screenWidth - 1;

  if (end > len - 1)
    end = len - 1;
  if (end < start)
    end = start;

  switch (motion)
  {
  case MOTION_SCREEN_START:
    *destCol = start;
    break;
  case MOTION_SCREEN_MIDDLE:
    *destCol = start + screenWidth / 2 > end ? end : start + screenWidth / 2;
    break;
  case MOTION_SCREEN_END:
    *destCol = end;
    break;
  }
}

void ErroneousColumnCallback(screenColumnMotion_T motion, linenr_T lnum, colnr_T col, colnr_T *destCol)
{
  *destCol = motion == MOTION_SCREEN_START ? -1 : 10000;
}

/* Answers with the second byte of the two-byte character at byte 1. */
void InsideCharacterColumnCallback(screenColumnMotion_T motion, linenr_T lnum, colnr_T col, colnr_T *destCol)
{
  *destCol = 2;
}

MU_TEST(test_no_callback)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenLineCallback(NULL);

  vimInput("H");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("L");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("M");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("j");

  vimInput("H");

  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("L");

  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("M");

  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_simple_callback)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenLineCallback(&SimpleScreenLineCallback);

  vimInput("H");

  mu_check(vimCursorGetLine() == 10);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("L");

  mu_check(vimCursorGetLine() == 30);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("M");

  mu_check(vimCursorGetLine() == 20);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_erroneous_callback)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenLineCallback(&ErroneousScreenLineCallback);

  vimInput("H");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("L");

  mu_check(vimCursorGetLine() == 100);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("M");

  mu_check(vimCursorGetLine() == 100);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_gj_gk_motion)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenPositionCallback(&SimplePositionCallback);

  vimInput("gj");

  mu_check(vimCursorGetLine() == 100);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("gk");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_gk_motion_same_line)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenPositionCallback(&SameLinePositionCallback);

  vimInput("3l");
  vimInput("d");
  vimInput("gk");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "e 1") == 0);
}

MU_TEST(test_gj_motion_same_line)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenPositionCallback(&SameLinePositionCallback);

  vimInput("3l");
  mu_check(vimCursorGetColumn() == 3);

  vimInput("d");
  vimInput("gj");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 3);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "Lin 1") == 0);
}

MU_TEST(test_erroneous_position_callback)
{
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenPositionCallback(&ErroneousPositionCallback);

  vimInput("gk");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("gj");

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 5);
}

MU_TEST(test_curswant)
{
  vimBufferOpen("collateral/curswant.txt", 1, 0);
  // When no callback is set, the cursor should not move at all.
  vimSetCursorMoveScreenPositionCallback(&MaxColPositionCallback);

  vimInput("$");
  vimInput("gj");

  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 1);

  vimInput("gj");
  mu_check(vimCursorGetLine() == 3);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("gj");
  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_screen_column_no_callback)
{
  // With no callback they are worked out from libvim's own window, as
  // upstream: 'nowrap', and as wide as vimWindowSetWidth() says.
  int width = vimWindowGetWidth();
  vimWindowSetWidth(4);

  vimInput("g$");
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 3);

  vimInput("gm");
  mu_check(vimCursorGetColumn() == 2);

  vimInput("g0");
  mu_check(vimCursorGetColumn() == 0);

  vimWindowSetWidth(width);
}

MU_TEST(test_screen_column)
{
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  screenWidth = 3;

  // the first screen line, "Lin"
  vimInput("g$");
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 2);
  vimInput("gm");
  mu_check(vimCursorGetColumn() == 1);
  vimInput("g0");
  mu_check(vimCursorGetColumn() == 0);

  // the second, "e 1"
  vimInput("4l");
  vimInput("g0");
  mu_check(vimCursorGetColumn() == 3);
  vimInput("g$");
  mu_check(vimCursorGetColumn() == 5);
  vimInput("gm");
  mu_check(vimCursorGetColumn() == 4);
  mu_check(vimCursorGetLine() == 1);

  // <Home> and <End> are the same motions
  vimInput("g");
  vimKey("<End>");
  mu_check(vimCursorGetColumn() == 5);
  vimInput("g");
  vimKey("<Home>");
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_screen_column_first_nonblank)
{
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  screenWidth = 4;

  // the screen line " 1" starts with a blank
  vimInput("$");
  vimInput("g0");
  mu_check(vimCursorGetColumn() == 4);
  vimInput("g^");
  mu_check(vimCursorGetColumn() == 5);

  vimInput("0");
  vimInput("2l");
  vimInput("g^");
  mu_check(vimCursorGetColumn() == 0);
}

MU_TEST(test_screen_column_operator)
{
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  screenWidth = 3;

  // g$ is inclusive
  vimInput("d");
  vimInput("g$");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "e 1") == 0);

  // g0 is not: from the "2" of "Line 2", the "e " before it
  vimInput("j");
  vimInput("$");
  vimInput("d");
  vimInput("g0");
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "Lin2") == 0);
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_screen_column_visual)
{
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  screenWidth = 3;

  vimInput("l");
  vimInput("v");
  vimInput("g$");
  vimInput("d");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "Le 1") == 0);
}

MU_TEST(test_screen_column_curswant)
{
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  screenWidth = 3;

  // after g$ the column wanted is the one it went to, not the line's end
  vimInput("g$");
  vimInput("j");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 2);

  // and after $, g0 ends "the line's end"
  vimInput("$");
  vimInput("g0");
  vimInput("j");
  mu_check(vimCursorGetLine() == 3);
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_screen_column_count)
{
  // g$ with a count is the end of a screen line further down: that is the
  // position callback's, asked for count - 1 lines from "the end".
  vimSetCursorMoveScreenColumnCallback(&WidthColumnCallback);
  vimSetCursorMoveScreenPositionCallback(&MaxColPositionCallback);

  vimInput("2g$");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 5);

  // g0 and gm take no count
  vimInput("3g0");
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 3);
}

MU_TEST(test_screen_column_erroneous_callback)
{
  vimSetCursorMoveScreenColumnCallback(&ErroneousColumnCallback);

  vimInput("2l");
  vimInput("g0");
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 0);

  vimInput("g$");
  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 5);
}

MU_TEST(test_screen_column_inside_character)
{
  char_u *lines[] = {(char_u *)"a\xc3\xa9" "bc"};
  vimBufferSetLines(curbuf, 0, 1, lines, 1);
  vimSetCursorMoveScreenColumnCallback(&InsideCharacterColumnCallback);

  vimInput("$");
  vimInput("g0");
  mu_check(vimCursorGetColumn() == 1);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_no_callback);
  MU_RUN_TEST(test_simple_callback);
  MU_RUN_TEST(test_erroneous_callback);
  MU_RUN_TEST(test_gj_gk_motion);
  MU_RUN_TEST(test_gk_motion_same_line);
  MU_RUN_TEST(test_gj_motion_same_line);
  MU_RUN_TEST(test_erroneous_position_callback);
  MU_RUN_TEST(test_curswant);
  MU_RUN_TEST(test_screen_column_no_callback);
  MU_RUN_TEST(test_screen_column);
  MU_RUN_TEST(test_screen_column_first_nonblank);
  MU_RUN_TEST(test_screen_column_operator);
  MU_RUN_TEST(test_screen_column_visual);
  MU_RUN_TEST(test_screen_column_curswant);
  MU_RUN_TEST(test_screen_column_count);
  MU_RUN_TEST(test_screen_column_erroneous_callback);
  MU_RUN_TEST(test_screen_column_inside_character);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(5);
  win_setheight(100);

  vimBufferOpen("collateral/lines_100.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
