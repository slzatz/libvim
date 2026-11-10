#include "libvim.h"
#include "minunit.h"

/*
 * vim's ordinary reports -- "5 lines yanked", "4 fewer lines", "search hit
 * BOTTOM, continuing at TOP" -- reach the message callback. Upstream libvim
 * dropped everything that went through msg(); only errors and a few commands
 * converted one at a time were sent.
 */

#define MAX_TEST_MESSAGE 8192

static char_u lastMessage[MAX_TEST_MESSAGE];
static msgPriority_T lastPriority;
static int messageCount = 0;
static int errorCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  assert(strlen(msg) < MAX_TEST_MESSAGE);
  strcpy(lastMessage, msg);
  lastPriority = priority;
  messageCount++;
  if (priority == MSG_ERROR)
  {
    errorCount++;
  }
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");

  vimExecute("e!");
  vimInput("g");
  vimInput("g");
  vimInput("0");

  messageCount = 0;
  errorCount = 0;
  lastMessage[0] = '\0';
}

void test_teardown(void) {}

MU_TEST(test_yank_reports_lines)
{
  vimInput("5");
  vimInput("y");
  vimInput("y");

  mu_check(messageCount == 1);
  mu_check(lastPriority == MSG_INFO);
  mu_check(strcmp(lastMessage, "5 lines yanked") == 0);
}

MU_TEST(test_small_change_is_not_reported)
{
  /* fewer lines than 'report' (2): vim says nothing */
  vimInput("y");
  vimInput("y");
  vimInput("d");
  vimInput("d");

  mu_check(messageCount == 0);
}

MU_TEST(test_delete_and_put_report_lines)
{
  vimInput("4");
  vimInput("d");
  vimInput("d");
  mu_check(messageCount == 1);
  mu_check(lastPriority == MSG_INFO);
  mu_check(strcmp(lastMessage, "4 fewer lines") == 0);

  vimInput("p");
  mu_check(messageCount == 2);
  mu_check(strcmp(lastMessage, "4 more lines") == 0);
}

MU_TEST(test_substitute_reports_count)
{
  vimExecute("1,5s/Line/Row/");

  mu_check(messageCount == 1);
  mu_check(lastPriority == MSG_INFO);
  mu_check(strcmp(lastMessage, "5 substitutions on 5 lines") == 0);
}

/* A report must not silence the ones after it. vim "keeps" some messages
 * (the :s count among them) until its next redraw, and holds back line-count
 * reports while one is kept; with no redraw to clear it, that lasted. */
MU_TEST(test_reports_follow_one_another)
{
  vimExecute("1,5s/Line/Row/");
  mu_check(strcmp(lastMessage, "5 substitutions on 5 lines") == 0);

  vimInput("4");
  vimInput("d");
  vimInput("d");
  mu_check(strcmp(lastMessage, "4 fewer lines") == 0);

  vimExecute("1,3d");
  mu_check(strcmp(lastMessage, "3 fewer lines") == 0);

  vimExecute("1,3copy $");
  mu_check(strcmp(lastMessage, "3 more lines") == 0);
  mu_check(errorCount == 0);
}

MU_TEST(test_search_wrap_is_a_warning)
{
  /* "Line" under the cursor on line 1: the next one is on line 2 */
  vimInput("*");
  mu_check(messageCount == 0);

  /* from the last line the search has to go round */
  vimInput("G");
  vimInput("*");

  mu_check(messageCount == 1);
  mu_check(lastPriority == MSG_WARNING);
  mu_check(strstr(lastMessage, "search hit BOTTOM") != NULL);
  mu_check(errorCount == 0);
}

MU_TEST(test_visual_mode_gives_no_terminal_warning)
{
  vimInput("V");
  vimInput("j");
  vimKey("<esc>");

  mu_check(messageCount == 0);
}

MU_TEST(test_error_is_sent_once_as_an_error)
{
  vimInput("'");
  vimInput("z");

  mu_check(messageCount == 1);
  mu_check(errorCount == 1);
  mu_check(lastPriority == MSG_ERROR);
  mu_check(strcmp(lastMessage, "E20: Mark not set") == 0);
}

MU_TEST(test_silent_sends_nothing)
{
  vimExecute("silent 1,5s/Line/Row/");
  mu_check(messageCount == 0);

  vimExecute("silent 1,4d");
  mu_check(messageCount == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_yank_reports_lines);
  MU_RUN_TEST(test_small_change_is_not_reported);
  MU_RUN_TEST(test_delete_and_put_report_lines);
  MU_RUN_TEST(test_substitute_reports_count);
  MU_RUN_TEST(test_reports_follow_one_another);
  MU_RUN_TEST(test_search_wrap_is_a_warning);
  MU_RUN_TEST(test_visual_mode_gives_no_terminal_warning);
  MU_RUN_TEST(test_error_is_sent_once_as_an_error);
  MU_RUN_TEST(test_silent_sends_nothing);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  vimSetMessageCallback(&onMessage);

  win_setwidth(5);
  win_setheight(100);

  vimBufferOpen("collateral/lines_100.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
