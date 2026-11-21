#include "libvim.h"
#include "minunit.h"

static int updateCount = 0;
static int lastLnum = 0;
static int lastLnume = 0;
static long lastXtra = 0;
static long lastVersionAtUpdateTime = 0;

void onBufferUpdate(bufferUpdate_T update)
{
  lastLnum = update.lnum;
  lastLnume = update.lnume;
  lastXtra = update.xtra;
  lastVersionAtUpdateTime = vimBufferGetLastChangedTick(curbuf);

  updateCount++;
}

static int errorCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  if (priority == MSG_ERROR)
    errorCount++;
}

/* the number an expression gives: line("'a") */
static long evalNumber(char *expr)
{
  char_u *result = vimEval((char_u *)expr);
  long n = result == NULL ? -1 : atol((char *)result);

  vim_free(result);
  return n;
}

static buf_T *fileBuffer;

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");

  vimBufferSetCurrent(fileBuffer);
  vimExecute("e!");
  vimExecute("delmarks!");
  errorCount = 0;

  vimInput("g");
  vimInput("g");

  updateCount = 0;
  lastLnum = 0;
  lastLnume = 0;
  lastXtra = 0;
}

void test_teardown(void) {}

MU_TEST(test_append_before_buffer)
{
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, 0, lines, 1);

  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "one") == 0);
  printf("LINE 2: %s\n", vimBufferGetLine(curbuf, 2));
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "This is the first line of a test file") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 4);
}

MU_TEST(test_append_after_buffer)
{
  char_u *lines[] = {"after"};
  vimBufferSetLines(curbuf, 3, 4, lines, 1);

  printf("LINE 3: %s\n", vimBufferGetLine(curbuf, 3));
  printf("LINE 4: %s\n", vimBufferGetLine(curbuf, 4));
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "after") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "This is the third line of a test file") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 4);
}

MU_TEST(test_append_after_first_line)
{

  char_u *lines[] = {"after first line"};
  vimBufferSetLines(curbuf, 1, 1, lines, 1);

  printf("LINE 1: %s\n", vimBufferGetLine(curbuf, 1));
  printf("LINE 2: %s\n", vimBufferGetLine(curbuf, 2));
  printf("LINE 3: %s\n", vimBufferGetLine(curbuf, 3));
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "This is the first line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "after first line") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "This is the second line of a test file") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 4);
}

MU_TEST(test_replace_second_line_multiple_lines)
{
  char_u *lines[] = {"new first line", "new second line"};
  vimBufferSetLines(curbuf, 1, 1, lines, 2);

  printf("LINE 1: %s\n", vimBufferGetLine(curbuf, 1));
  printf("LINE 2: %s\n", vimBufferGetLine(curbuf, 2));
  printf("LINE 3: %s\n", vimBufferGetLine(curbuf, 3));
  printf("LINE 4: %s\n", vimBufferGetLine(curbuf, 4));
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "This is the first line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "new first line") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "new second line") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "This is the second line of a test file") == 0);
}

MU_TEST(test_replace_entire_buffer_from_zero)
{
  char_u *lines[] = {"abc"};
  vimBufferSetLines(curbuf, 0, 3, lines, 1);
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "abc") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 1);
}

MU_TEST(test_replace_entire_buffer_after_first_line)
{
  char_u *lines[] = {"abc"};
  vimBufferSetLines(curbuf, 1, 3, lines, 1);
  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "This is the first line of a test file") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "abc") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 2);
}

MU_TEST(test_replace_entire_buffer_with_more_lines)
{
  char_u *lines[] = {"line1", "line2", "line3", "line4", "line5"};
  vimBufferSetLines(curbuf, 0, 3, lines, 5);
  mu_check(vimBufferGetLineCount(curbuf) == 5);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "line1") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 5), "line5") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 5);
}

MU_TEST(test_replace_entire_buffer_with_more_lines_again)
{
  char_u *lines[] = {"line1", "line2", "line3", "line4", "line5"};
  vimBufferSetLines(curbuf, 0, -1, lines, 5);
  mu_check(vimBufferGetLineCount(curbuf) == 5);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "line1") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 5), "line5") == 0);

  mu_check(vimBufferGetLineCount(curbuf) == 5);
}

MU_TEST(test_version_is_incremented)
{
  int initialVersion = vimBufferGetLastChangedTick(curbuf);
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, 0, lines, 1);

  int afterVersion = vimBufferGetLastChangedTick(curbuf);
  mu_check(afterVersion > initialVersion);
}

MU_TEST(test_modified_is_set)
{
  mu_check(vimBufferGetModified(curbuf) == FALSE);
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, 0, lines, 1);
  mu_check(vimBufferGetModified(curbuf) == TRUE);
}

/*
 * The cursor and the marks go with their lines, as they do for vim's own
 * setbufline(), appendbufline() and deletebufline(), and the cursor is then
 * inside the text. Until 2026-11-21 none of it was looked at: a cursor left
 * on a line that was gone gave E315 on the next key, and in Insert mode a
 * read past the buffer's memory.
 */
MU_TEST(test_cursor_goes_with_its_line)
{
  vimInput("j");
  vimInput("w");
  mu_check(vimCursorGetLine() == 2);
  int col = vimCursorGetColumn();

  char_u *lines[] = {"above", "above too"};
  vimBufferSetLines(curbuf, 0, 0, lines, 2);

  mu_check(vimCursorGetLine() == 4);
  mu_check(vimCursorGetColumn() == col);
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "This is the second line of a test file") == 0);
}

MU_TEST(test_cursor_stays_on_a_line_that_is_replaced)
{
  vimInput("j");
  vimInput("$");

  char_u *lines[] = {"one", "two", "three"};
  vimBufferSetLines(curbuf, 0, -1, lines, 3);

  /* the same line, and the last character of what it holds now */
  mu_check(vimCursorGetLine() == 2);
  mu_check(vimCursorGetColumn() == 2);
}

MU_TEST(test_cursor_is_brought_inside_fewer_lines)
{
  vimInput("G");
  vimInput("$");

  char_u *lines[] = {"abc"};
  vimBufferSetLines(curbuf, 0, -1, lines, 1);

  mu_check(vimCursorGetLine() == 1);
  mu_check(vimCursorGetColumn() == 2);

  vimInput("x");
  mu_check(errorCount == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "ab") == 0);
}

MU_TEST(test_cursor_on_a_line_that_went)
{
  vimInput("j"); // line 2 of 3; lines 2 and 3 go
  char_u *lines[] = {"only"};
  vimBufferSetLines(curbuf, 0, -1, lines, 1);
  mu_check(vimCursorGetLine() == 1);

  /* in the middle: the cursor is on the line that followed the ones that went */
  char_u *five[] = {"1", "2", "3", "4", "5"};
  vimBufferSetLines(curbuf, 0, -1, five, 5);
  vimInput("3");
  vimInput("G");
  char_u *one[] = {"new 2"};
  vimBufferSetLines(curbuf, 1, 4, one, 1); // lines 2 to 4 become one line
  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(vimCursorGetLine() == 3);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "5") == 0);
}

MU_TEST(test_fewer_lines_in_insert_mode)
{
  vimInput("G");
  vimInput("A");
  vimInput("zz");

  char_u *lines[] = {"x"};
  vimBufferSetLines(curbuf, 0, -1, lines, 1);

  vimInput("y");
  vimKey("<bs>");
  vimInput("y");
  vimKey("<esc>");
  mu_check(errorCount == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "xy") == 0);

  /* where the insert started is a mark like the others afterwards */
  vimInput("`");
  vimInput("[");
  mu_check(errorCount == 0);
  mu_check(vimCursorGetLine() == 1);
}

MU_TEST(test_fewer_lines_in_visual_mode)
{
  /* the Visual area starts on line 3 and the cursor is on line 1 */
  vimInput("G");
  vimInput("$");
  vimInput("v");
  vimInput("gg");

  char_u *lines[] = {"abc"};
  vimBufferSetLines(curbuf, 0, -1, lines, 1);

  vimInput("d");
  mu_check(errorCount == 0);
  mu_check((vimGetMode() & VISUAL) == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "") == 0);
}

/*
 * A command that is half typed was typed for the text there was. An operator
 * waiting for its motion is forgotten: it holds the position it was typed
 * at, and "d CTRL-V", lines set, then a motion read past the end of a line
 * that had become shorter.
 */
MU_TEST(test_pending_operator_is_forgotten)
{
  vimInput("$");
  vimInput("d");
  vimKey("<c-v>");

  char_u *lines[] = {"a", "b", "c"};
  vimBufferSetLines(curbuf, 0, -1, lines, 3);

  vimInput("j"); // a motion, and no more than that
  mu_check(errorCount == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "a") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "b") == 0);
  mu_check(vimCursorGetLine() == 2);

  /* a count and a register too */
  vimInput("\"");
  vimInput("a");
  vimInput("2");
  vimBufferSetLines(curbuf, 0, -1, lines, 3);
  vimInput("x");
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "") == 0);
  mu_check(strcmp((char *)vimEval((char_u *)"getreg('a')"), "") == 0);
}

/*
 * A blockwise insert copies what was typed to the other lines of the block
 * when Insert mode ends. The block is lines and columns of the text there
 * was: with the lines set meanwhile nothing is copied, where it used to
 * give E315 for every line of the block that was gone.
 */
MU_TEST(test_blockwise_insert_is_not_copied_into_new_lines)
{
  vimKey("<c-v>");
  vimInput("j");
  vimInput("j");
  vimInput("I");
  vimInput("ab");

  /* the text is new: with the history as it was, what the insert saved
   * for undo is lines that are gone (E440) */
  char_u *lines[] = {"one"};
  vimBufferSetLines(curbuf, 0, -1, lines, 1);
  vimUndoClear(curbuf);
  vimInput("c");
  vimKey("<esc>");
  mu_check(errorCount == 0);
  mu_check(vimBufferGetLineCount(curbuf) == 1);
  mu_check((vimGetMode() & INSERT) == 0);

  /* the same number of lines: the other lines are left as they were set */
  vimInput("gg");
  vimInput("0");
  char_u *three[] = {"one", "two", "three"};
  vimBufferSetLines(curbuf, 0, -1, three, 3);
  vimKey("<c-v>");
  vimInput("j");
  vimInput("j");
  vimInput("c");
  vimInput("X");
  char_u *again[] = {"uno", "dos", "tres"};
  vimBufferSetLines(curbuf, 0, -1, again, 3);
  vimUndoClear(curbuf);
  vimKey("<esc>");
  mu_check(errorCount == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "dos") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "tres") == 0);

  /* and without lines set, it is copied as before */
  vimInput("gg");
  vimInput("0");
  vimBufferSetLines(curbuf, 0, -1, three, 3);
  vimKey("<c-v>");
  vimInput("j");
  vimInput("j");
  vimInput("I");
  vimInput("- ");
  vimKey("<esc>");
  mu_check(strcmp(vimBufferGetLine(curbuf, 1), "- one") == 0);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "- three") == 0);
}

MU_TEST(test_marks_keep_their_lines)
{
  vimInput("ma"); // line 1
  vimInput("j");
  vimInput("mb"); // line 2
  vimInput("G");
  vimInput("mc"); // line 3

  /* one for one: every mark stays */
  char_u *three[] = {"one", "two", "three"};
  vimBufferSetLines(curbuf, 0, -1, three, 3);
  mu_check(evalNumber("line(\"'a\")") == 1);
  mu_check(evalNumber("line(\"'b\")") == 2);
  mu_check(evalNumber("line(\"'c\")") == 3);

  /* fewer lines: a mark on a line that went is not set any more, where it
   * used to be a mark with an invalid line number (E19) */
  char_u *two[] = {"one", "two"};
  vimBufferSetLines(curbuf, 0, -1, two, 2);
  mu_check(evalNumber("line(\"'a\")") == 1);
  mu_check(evalNumber("line(\"'b\")") == 2);
  mu_check(evalNumber("line(\"'c\")") == 0);

  vimInput("gg");
  vimInput("'");
  vimInput("c");
  mu_check(errorCount == 1); // E20: Mark not set
  mu_check(vimCursorGetLine() == 1);
}

MU_TEST(test_marks_go_with_lines_that_move)
{
  vimInput("G");
  vimInput("ma"); // line 3

  char_u *above[] = {"above"};
  vimBufferSetLines(curbuf, 0, 0, above, 1);
  mu_check(evalNumber("line(\"'a\")") == 4);

  /* lines 1 to 3 become one line: line 4 is line 2 */
  char_u *one[] = {"one"};
  vimBufferSetLines(curbuf, 0, 3, one, 1);
  mu_check(evalNumber("line(\"'a\")") == 2);
  mu_check(strcmp(vimBufferGetLine(curbuf, 2), "This is the third line of a test file") == 0);

  vimInput("gg");
  vimInput("'");
  vimInput("a");
  mu_check(errorCount == 0);
  mu_check(vimCursorGetLine() == 2);
}

MU_TEST(test_arguments_out_of_range)
{
  long tick = vimBufferGetLastChangedTick(curbuf);
  char_u *lines[] = {"x"};

  /* nothing to set them in: not done, and not counted as a change */
  vimBufferSetLines(curbuf, -3, 2, lines, 1);
  vimBufferSetLines(curbuf, 0, 1, lines, -1);
  vimBufferSetLines(curbuf, 0, 1, NULL, 1);
  vimBufferSetLines(curbuf, 2, 2, lines, 0);
  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(vimBufferGetLastChangedTick(curbuf) == tick);
  mu_check(vimBufferGetModified(curbuf) == FALSE);

  /* past the end is the end */
  vimBufferSetLines(curbuf, 50, 60, lines, 1);
  mu_check(vimBufferGetLineCount(curbuf) == 4);
  mu_check(strcmp(vimBufferGetLine(curbuf, 4), "x") == 0);

  /* an end before the start is the start: lines are put in, none go */
  vimBufferSetLines(curbuf, 2, 1, lines, 1);
  mu_check(vimBufferGetLineCount(curbuf) == 5);
  mu_check(strcmp(vimBufferGetLine(curbuf, 3), "x") == 0);
}

MU_TEST(test_buffer_that_is_not_current)
{
  char_u *five[] = {"1", "2", "3", "4", "5"};
  buf_T *other = vimBufferNew(0);

  /* never loaded: it has no lines to set, as for setbufline() */
  long tick = vimBufferGetLastChangedTick(other);
  vimBufferSetLines(other, 0, -1, five, 5);
  mu_check(vimBufferGetLastChangedTick(other) == tick);

  vimBufferSetCurrent(other);
  vimBufferSetLines(other, 0, -1, five, 5);
  vimInput("4");
  vimInput("G");
  vimInput("ma");
  vimBufferSetCurrent(fileBuffer);

  /* the context mark of the window is this buffer's, on line 3 */
  vimInput("G");
  vimInput("gg");

  char_u *above[] = {"above", "above too"};
  vimBufferSetLines(other, 0, 0, above, 2);
  mu_check(vimBufferGetLineCount(other) == 7);
  mu_check(vimBufferGetLineCount(curbuf) == 3);
  mu_check(vimCursorGetLine() == 1);
  vimInput("'");
  vimInput("'");
  mu_check(vimCursorGetLine() == 3);

  /* and the other buffer's mark and cursor went with their line */
  vimBufferSetCurrent(other);
  mu_check(evalNumber("line(\"'a\")") == 6);
  mu_check(vimCursorGetLine() == 6);
  mu_check(errorCount == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_append_before_buffer);
  MU_RUN_TEST(test_append_after_buffer);
  MU_RUN_TEST(test_append_after_first_line);
  MU_RUN_TEST(test_replace_second_line_multiple_lines);
  MU_RUN_TEST(test_replace_entire_buffer_from_zero);
  MU_RUN_TEST(test_replace_entire_buffer_after_first_line);
  MU_RUN_TEST(test_replace_entire_buffer_with_more_lines);
  MU_RUN_TEST(test_replace_entire_buffer_with_more_lines_again);
  MU_RUN_TEST(test_version_is_incremented);
  MU_RUN_TEST(test_modified_is_set);
  MU_RUN_TEST(test_cursor_goes_with_its_line);
  MU_RUN_TEST(test_cursor_stays_on_a_line_that_is_replaced);
  MU_RUN_TEST(test_cursor_is_brought_inside_fewer_lines);
  MU_RUN_TEST(test_cursor_on_a_line_that_went);
  MU_RUN_TEST(test_fewer_lines_in_insert_mode);
  MU_RUN_TEST(test_fewer_lines_in_visual_mode);
  MU_RUN_TEST(test_pending_operator_is_forgotten);
  MU_RUN_TEST(test_blockwise_insert_is_not_copied_into_new_lines);
  MU_RUN_TEST(test_marks_keep_their_lines);
  MU_RUN_TEST(test_marks_go_with_lines_that_move);
  MU_RUN_TEST(test_arguments_out_of_range);
  MU_RUN_TEST(test_buffer_that_is_not_current);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  vimSetBufferUpdateCallback(&onBufferUpdate);
  vimSetMessageCallback(&onMessage);

  win_setwidth(5);
  win_setheight(100);

  fileBuffer = vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
