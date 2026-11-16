#include "libvim.h"
#include "minunit.h"

void test_setup(void)
{
  vimExecute("e!");

  vimInput("g");
  vimInput("g");
  vimInput("0");
}

void test_teardown(void) {}

MU_TEST(test_multiple_undo)
{
  // Delete first line
  vimInput("d");
  vimInput("d");

  // Delete second line
  vimInput("d");
  vimInput("d");

  buf_T *cur = vimBufferGetCurrent();
  int count = vimBufferGetLineCount(cur);
  mu_check(count == 1);

  // Undo last change - the second line should be back
  vimInput("u");

  count = vimBufferGetLineCount(cur);
  mu_check(count == 2);
  mu_check(strcmp(vimBufferGetLine(cur, 1),
                  "This is the second line of a test file") == 0);

  // Undo again - the first line should be back
  vimInput("u");

  count = vimBufferGetLineCount(cur);
  mu_check(count == 3);
  mu_check(strcmp(vimBufferGetLine(cur, 1),
                  "This is the first line of a test file") == 0);
}

MU_TEST(test_multiple_undo_redo)
{
  // Delete first line
  vimInput("d");
  vimInput("d");

  // Delete second line
  vimInput("d");
  vimInput("d");

  buf_T *cur = vimBufferGetCurrent();
  int count = vimBufferGetLineCount(cur);
  mu_check(count == 1);

  // Undo twice
  vimInput("u");
  vimInput("u");

  // Redo the last change
  vimKey("<C-r>");

  count = vimBufferGetLineCount(cur);
  mu_check(count == 2);

  // Redo again
  vimKey("<C-r>");

  count = vimBufferGetLineCount(cur);
  mu_check(count == 1);
}

MU_TEST(test_undo_save)
{
  // Save buffer before changing

  vimUndoSaveRegion(0, 3);

  // Replace first line with 'one'
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, 1, lines, 1);

  vimUndoSaveRegion(0, 3);

  char_u *linesAgain[] = {"two"};
  vimBufferSetLines(curbuf, 0, 1, linesAgain, 1);

  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "two") == 0);

  vimInput("u");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1),
                  "This is the first line of a test file") == 0);
}

MU_TEST(test_undo_sync)
{
  // Save buffer before changing

  vimUndoSaveRegion(0, 3);

  // Replace first line with 'one'
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, 1, lines, 1);

  // Create sync point (new undo level)
  vimUndoSync(0);
  vimUndoSaveRegion(0, 3);

  char_u *linesAgain[] = {"two"};
  vimBufferSetLines(curbuf, 0, 1, linesAgain, 1);

  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "two") == 0);

  vimInput("u");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1),
                  "one") == 0);

  vimInput("u");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1),
                  "This is the first line of a test file") == 0);
}

/*
 * vimBufferSetLines() saves nothing for undo. Without vimUndoSaveRegion()
 * before it, what was saved earlier describes lines that are gone or have
 * moved, and `u` puts a line of the old text over the new: that is what the
 * first test shows, and what vimUndoClear() is for.
 */
MU_TEST(test_set_lines_alone_leaves_a_stale_history)
{
  vimInput("x"); // an edit to undo, on line 1
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "his is the first line of a test file") == 0);

  char_u *lines[] = {"new one", "new two", "new three"};
  vimBufferSetLines(curbuf, 0, -1, lines, 3);

  vimInput("u");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "This is the first line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);
}

MU_TEST(test_clear_undo_does_not_bring_back_old_text)
{
  vimInput("x");
  char_u *lines[] = {"new one", "new two", "new three", "new four"};
  vimBufferSetLines(curbuf, 0, -1, lines, 4);
  vimUndoClear(curbuf);

  vimInput("u");
  mu_check(vimBufferGetLineCount(curbuf) == 4);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "new one") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "new three") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "new four") == 0);
}

/* Lines spliced in above an edit: the saved line number is one too low. */
MU_TEST(test_clear_undo_after_a_splice)
{
  vimInput("j");
  vimInput("x"); // line 2
  char_u *lines[] = {"spliced in"};
  vimBufferSetLines(curbuf, 0, 0, lines, 1);
  vimUndoClear(curbuf);

  vimInput("u");
  mu_check(vimBufferGetLineCount(curbuf) == 4);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "spliced in") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "This is the first line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "his is the second line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "This is the third line of a test file") == 0);
}

/* What could be redone goes too. */
MU_TEST(test_clear_undo_leaves_nothing_to_redo)
{
  vimInput("x");
  vimInput("u");
  char_u *lines[] = {"new one", "new two"};
  vimBufferSetLines(curbuf, 0, -1, lines, 2);
  vimUndoClear(curbuf);

  vimKey("<c-r>");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "new one") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);
}

/* The history starts again: a change made afterwards is undone as usual,
 * and undo stops at the text that was set. */
MU_TEST(test_changes_after_clear_undo_are_undone)
{
  vimInput("x");
  char_u *lines[] = {"new one", "new two"};
  vimBufferSetLines(curbuf, 0, -1, lines, 2);
  vimUndoClear(curbuf);

  vimInput("x");
  vimInput("j");
  vimInput("d");
  vimInput("d");
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "ew one") == 0);

  vimInput("u");
  vimInput("u");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "new one") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);

  vimInput("u"); // nothing older is left
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "new one") == 0);

  vimKey("<c-r>");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "ew one") == 0);
}

/* The commands that walk the history find it empty and say so. */
MU_TEST(test_cleared_history_is_safe_to_ask_about)
{
  vimInput("x");
  vimInput("j");
  vimInput("x");
  char_u *lines[] = {"new one", "new two"};
  vimBufferSetLines(curbuf, 0, -1, lines, 2);
  vimUndoClear(curbuf);

  vimExecute("undolist");
  vimExecute("earlier 10s");
  vimExecute("earlier 1f");
  vimExecute("later 5");
  vimExecute("undo 0");
  vimInput("U");
  vimInput("g");
  vimInput("-");
  vimInput("g");
  vimInput("+");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "new one") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);
}

/* Nothing but the history goes: not the text, not the modified flag. */
MU_TEST(test_clear_undo_touches_only_the_history)
{
  mu_check(vimBufferGetModified(curbuf) == FALSE);
  long tick = vimBufferGetLastChangedTick(curbuf);
  vimUndoClear(curbuf);
  mu_check(vimBufferGetModified(curbuf) == FALSE);
  mu_check(vimBufferGetLastChangedTick(curbuf) == tick);
  mu_check(vimBufferGetLineCount(curbuf) == 3);

  vimInput("x");
  vimUndoClear(curbuf);
  mu_check(vimBufferGetModified(curbuf) == TRUE);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "his is the first line of a test file") == 0);
}

/* Called in the middle of an insert: the insert goes on, and undo afterwards
 * does not reach behind the call. */
MU_TEST(test_clear_undo_during_an_insert)
{
  vimInput("x");
  vimInput("i");
  vimInput("a");
  vimInput("b");
  char_u *lines[] = {"new one", "new two"};
  vimBufferSetLines(curbuf, 0, -1, lines, 2);
  vimUndoClear(curbuf);
  vimInput("c");
  vimInput("d");
  vimKey("<esc>");
  mu_check(vimBufferGetLineCount(curbuf) == 2);

  vimInput("u");
  vimInput("u");
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strstr((char *)vimBufferGetLine(curbuf, 1), "This is") == NULL);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new two") == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_multiple_undo);
  MU_RUN_TEST(test_multiple_undo_redo);
  MU_RUN_TEST(test_undo_save);
  MU_RUN_TEST(test_undo_sync);
  MU_RUN_TEST(test_set_lines_alone_leaves_a_stale_history);
  MU_RUN_TEST(test_clear_undo_does_not_bring_back_old_text);
  MU_RUN_TEST(test_clear_undo_after_a_splice);
  MU_RUN_TEST(test_clear_undo_leaves_nothing_to_redo);
  MU_RUN_TEST(test_changes_after_clear_undo_are_undone);
  MU_RUN_TEST(test_cleared_history_is_safe_to_ask_about);
  MU_RUN_TEST(test_clear_undo_touches_only_the_history);
  MU_RUN_TEST(test_clear_undo_during_an_insert);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(5);
  win_setheight(100);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
