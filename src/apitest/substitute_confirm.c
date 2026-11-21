#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * ":s" with the "c" flag asks the embedder.
 *
 * vim highlights each match, writes "replace with X (y/n/a/q/l/^E/^Y)?" and
 * waits for a key. libvim has no window to show the match in, and no key can
 * arrive while a command runs, so do_sub() calls back instead
 * (vimSetSubstituteConfirmCallback()): the callback is given the match and
 * returns the key. Upstream refused the flag.
 *
 * The callback here answers from a list and writes down what it was asked.
 */

static char said[4096];

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
}

static char answers[32]; /* the keys to answer with, in order; then <Esc> */
static int asked;        /* how many questions there were */
static char seen[2048];  /* each: "lnum:col-endLnum:endCol sub line|" */
static int highlights;   /* the matches of the pattern, counted during the last question */
static int interrupt_at; /* the question to call vimInterrupt() during, from 1 */

static int answer(linenr_T lnum, colnr_T col, linenr_T endLnum, colnr_T endCol, char_u *sub)
{
  int key = asked < (int)strlen(answers) ? answers[asked] : ESC;
  searchHighlight_T *found = NULL;

  asked++;
  snprintf(seen + strlen(seen), sizeof(seen) - strlen(seen), "%ld:%d-%ld:%d %s %s|",
           (long)lnum, (int)col, (long)endLnum, (int)endCol, sub,
           vimBufferGetLine(curbuf, lnum));
  /* the cursor is on the match, for an embedder that draws it there */
  if (vimCursorGetLine() != lnum || vimCursorGetColumn() != col)
    snprintf(seen + strlen(seen), sizeof(seen) - strlen(seen), "cursor at %ld,%d|",
             (long)vimCursorGetLine(), (int)vimCursorGetColumn());
  /* what an embedder asks for when it draws: allowed during the question */
  vimSearchGetHighlights(curbuf, 1, 0, &highlights, &found);
  vim_free(found);

  if (asked == interrupt_at)
    vimInterrupt();
  return key;
}

static void text(char *a, char *b, char *c)
{
  char_u *lines[3] = {(char_u *)a, (char_u *)b, (char_u *)c};

  vimBufferSetLines(curbuf, 0, -1, lines, 3);
  vimUndoClear(curbuf);
  vimInput("g");
  vimInput("g");
  said[0] = 0;
}

static int line_is(linenr_T lnum, char *want)
{
  char *got = (char *)vimBufferGetLine(curbuf, lnum);
  int same = lnum <= (linenr_T)vimBufferGetLineCount(curbuf) && strcmp(got, want) == 0;

  if (!same)
    printf("line %ld is |%s|, not |%s|\n", (long)lnum, got, want);
  return same;
}

static int lines_are(char *a, char *b, char *c)
{
  int n = (int)vimBufferGetLineCount(curbuf);

  if (n != 3)
    printf("%d lines, not 3\n", n);
  /* each checked, so that each wrong one is printed */
  return (n == 3) & line_is(1, a) & line_is(2, b) & line_is(3, c);
}

static int is(char *what, char *got, char *want)
{
  if (strcmp(got, want) != 0)
  {
    printf("%s is |%s|, not |%s|\n", what, got, want);
    return FALSE;
  }
  return TRUE;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimSetMappingsEnabled(FALSE);
  vimExecute("mapclear");
  vimSetSubstituteConfirmCallback(&answer);
  answers[0] = 0;
  asked = 0;
  seen[0] = 0;
  highlights = 0;
  interrupt_at = 0;
  said[0] = 0;
}

void test_teardown(void) {}

MU_TEST(test_each_match_is_asked_about)
{
  text("foo bar foo", "foo", "bar foo");
  strcpy(answers, "ynyq");
  vimExecute("%s/foo/X/gc");

  mu_check(lines_are("X bar foo", "X", "bar foo"));
  mu_check(asked == 4);
  /* The match, the replacement as typed, and the line as it stands: at the
   * second question the first change is in the line already, and the
   * columns are that line's. */
  mu_check(is("asked", seen,
              "1:0-1:3 X foo bar foo|"
              "1:6-1:9 X X bar foo|"
              "2:0-2:3 X foo|"
              "3:4-3:7 X bar foo|"));
  /* no prompt of vim's own: the embedder writes one */
  mu_check(strstr(said, "replace with") == NULL);
}

MU_TEST(test_the_line_is_shown_as_changed_so_far)
{
  /* a replacement longer than the match moves what follows it */
  text("a a a", "b", "c");
  strcpy(answers, "yny");
  vimExecute("s/a/aaa/gc");

  mu_check(lines_are("aaa a aaa", "b", "c"));
  mu_check(is("asked", seen,
              "1:0-1:1 aaa a a a|"
              "1:4-1:5 aaa aaa a a|"
              "1:6-1:7 aaa aaa a a|"));
}

MU_TEST(test_a_is_all_the_rest_and_l_is_the_last)
{
  text("foo foo", "foo", "foo foo");
  strcpy(answers, "na");
  vimExecute("%s/foo/X/gc");
  mu_check(lines_are("foo X", "X", "X X"));
  mu_check(asked == 2);
  mu_check(strstr(said, "4 substitutions on 3 lines") != NULL);

  asked = 0;
  text("foo foo", "foo", "foo foo");
  strcpy(answers, "nl");
  vimExecute("%s/foo/X/gc");
  mu_check(lines_are("foo X", "foo", "foo foo"));
  mu_check(asked == 2);
}

MU_TEST(test_esc_and_ctrl_c_stop_like_q)
{
  char stop[3] = {ESC, Ctrl_C, 'q'};
  int i;

  for (i = 0; i < 3; i++)
  {
    asked = 0;
    text("foo", "foo", "foo");
    answers[0] = 'y';
    answers[1] = stop[i];
    answers[2] = 0;
    vimExecute("%s/foo/X/c");
    mu_check(lines_are("X", "foo", "foo"));
    mu_check(asked == 2);
    mu_check((vimGetMode() & NORMAL) == NORMAL);
  }
}

MU_TEST(test_another_key_is_asked_again)
{
  text("foo", "bar", "baz");
  strcpy(answers, "x?y");
  vimExecute("%s/foo/X/c");

  mu_check(lines_are("X", "bar", "baz"));
  mu_check(asked == 3);
  mu_check(is("asked", seen, "1:0-1:3 X foo|1:0-1:3 X foo|1:0-1:3 X foo|"));
}

MU_TEST(test_a_match_across_a_line_break)
{
  text("a b", "c d", "b");
  strcpy(answers, "y");
  vimExecute("%s/b\\nc/Z/gc");

  mu_check(vimBufferGetLineCount(curbuf) == 2);
  mu_check(line_is(1, "a Z d"));
  mu_check(line_is(2, "b"));
  /* from byte 2 of line 1 up to byte 1 of line 2 */
  mu_check(is("asked", seen, "1:2-2:1 Z a b|"));
}

MU_TEST(test_the_replacement_is_vims)
{
  /* "&", a back reference and a line break: nothing about the question
   * changes what is put in */
  text("foo bar", "x", "y");
  strcpy(answers, "y");
  vimExecute("s/\\(f\\)oo/[&]\\r\\1/c");

  mu_check(vimBufferGetLineCount(curbuf) == 4);
  mu_check(line_is(1, "[foo]"));
  mu_check(line_is(2, "f bar"));
}

MU_TEST(test_one_undo_takes_it_all_back)
{
  text("foo bar foo", "foo", "bar foo");
  strcpy(answers, "yyyy");
  vimExecute("%s/foo/X/gc");
  mu_check(lines_are("X bar X", "X", "bar X"));

  vimInput("u");
  mu_check(lines_are("foo bar foo", "foo", "bar foo"));
}

MU_TEST(test_by_every_route)
{
  /* under :g */
  text("foo bar", "x", "foo");
  strcpy(answers, "ny");
  vimExecute("g/foo/s//X/c");
  mu_check(lines_are("foo bar", "x", "X"));
  mu_check(asked == 2);

  /* with a command after it, which runs when the asking is over */
  asked = 0;
  text("foo bar", "x", "foo");
  strcpy(answers, "yy");
  vimExecute("let g:after = 'not yet'");
  vimExecute("%s/foo/X/gc | let g:after = 'ran'");
  mu_check(lines_are("X bar", "x", "X"));
  {
    char_u *after = vimEval("g:after");
    mu_check(is("g:after", (char *)after, "ran"));
    vim_free(after);
  }

  /* in a function */
  asked = 0;
  text("foo bar", "x", "foo");
  strcpy(answers, "yn");
  vimExecute("function! SubstituteAsking()\n%s/foo/X/gc\nendfunction");
  vimExecute("call SubstituteAsking()");
  mu_check(lines_are("X bar", "x", "foo"));
  mu_check(asked == 2);

  /* typed, a key at a time */
  asked = 0;
  text("foo bar", "x", "foo");
  strcpy(answers, "ny");
  vimInput(":");
  vimInput("%");
  vimInput("s");
  vimInput("/");
  vimInput("f");
  vimInput("o");
  vimInput("o");
  vimInput("/");
  vimInput("X");
  vimInput("/");
  vimInput("c");
  vimKey("<cr>");
  mu_check(lines_are("foo bar", "x", "X"));
  mu_check(asked == 2);

  /* from a mapping */
  asked = 0;
  text("foo bar", "x", "foo");
  strcpy(answers, "yy");
  vimExecute("nnoremap Q :%s/foo/X/gc<CR>");
  vimSetMappingsEnabled(TRUE);
  vimInput("Q");
  mu_check(lines_are("X bar", "x", "X"));
  mu_check(asked == 2);

  /* "&&" keeps the flag, and asks */
  asked = 0;
  text("foo bar", "x", "foo");
  strcpy(answers, "y");
  vimExecute("&&");
  mu_check(lines_are("X bar", "x", "foo"));
  mu_check(asked == 1);
}

MU_TEST(test_keys_already_typed_are_the_answer)
{
  /* as in vim: what follows the command in a ":normal" answers it, and when
   * that runs out the answer is <Esc>. Nobody is asked. */
  text("foo", "foo", "foo");
  vimExecute("normal :%s/foo/X/gc\ryn");
  mu_check(lines_are("X", "foo", "foo"));
  mu_check(asked == 0);

  text("foo", "foo", "foo");
  vimExecute("normal :%s/foo/X/gc\rnyy");
  mu_check(lines_are("foo", "X", "X"));
  mu_check(asked == 0);

  /* the rest of a mapping answers too; a mapping is typed, though, so when
   * its keys run out the user is asked */
  text("foo", "foo", "foo");
  strcpy(answers, "y");
  vimExecute("nnoremap Q :%s/foo/X/gc<CR>yn");
  vimSetMappingsEnabled(TRUE);
  vimInput("Q");
  mu_check(lines_are("X", "foo", "X"));
  mu_check(asked == 1);
  mu_check(is("asked", seen, "3:0-3:3 X foo|"));
}

MU_TEST(test_no_question_without_the_flag_or_a_match)
{
  text("foo", "foo", "bar");
  vimExecute("%s/foo/X/g");
  mu_check(lines_are("X", "X", "bar"));

  /* "n" only counts, whatever else is asked for */
  text("foo", "foo", "bar");
  vimExecute("%s/foo/X/gcn");
  mu_check(lines_are("foo", "foo", "bar"));

  vimExecute("%s/zzz/X/gc");
  mu_check(strstr(said, "E486") != NULL);
  mu_check(asked == 0);
}

MU_TEST(test_an_interrupt_ends_the_asking)
{
  /* CTRL-C while the next match is looked for: vimInterrupt(), from the
   * thread that reads keys. Here it comes during the first question, which
   * is answered "n"; there is no second question. */
  text("foo", "foo", "foo");
  strcpy(answers, "nyy");
  interrupt_at = 1;
  vimExecute("%s/foo/X/gc");
  mu_check(lines_are("foo", "foo", "foo"));
  mu_check(asked == 1);

  /* and it is over: the next command runs */
  interrupt_at = 0;
  asked = 0;
  strcpy(answers, "y");
  vimExecute("%s/foo/X/c");
  mu_check(lines_are("X", "foo", "foo"));
}

MU_TEST(test_the_embedder_may_look_while_it_is_asked)
{
  /* the matches of the pattern, asked for during the question as an
   * embedder that draws them would */
  text("foo foo", "foo", "bar");
  strcpy(answers, "n");
  vimExecute("%s/foo/X/gc");
  mu_check(highlights == 3);
  mu_check(lines_are("foo foo", "foo", "bar"));
}

MU_TEST(test_with_nobody_to_ask_it_is_an_error)
{
  vimSetSubstituteConfirmCallback(NULL);
  text("foo", "foo", "foo");
  vimExecute("%s/foo/X/gc");
  mu_check(lines_are("foo", "foo", "foo"));
  mu_check(strstr(said, "nobody to ask") != NULL);

  /* keys already typed still answer */
  text("foo", "foo", "foo");
  vimExecute("normal :%s/foo/X/gc\ryy");
  mu_check(lines_are("X", "X", "foo"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_each_match_is_asked_about);
  MU_RUN_TEST(test_the_line_is_shown_as_changed_so_far);
  MU_RUN_TEST(test_a_is_all_the_rest_and_l_is_the_last);
  MU_RUN_TEST(test_esc_and_ctrl_c_stop_like_q);
  MU_RUN_TEST(test_another_key_is_asked_again);
  MU_RUN_TEST(test_a_match_across_a_line_break);
  MU_RUN_TEST(test_the_replacement_is_vims);
  MU_RUN_TEST(test_one_undo_takes_it_all_back);
  MU_RUN_TEST(test_by_every_route);
  MU_RUN_TEST(test_keys_already_typed_are_the_answer);
  MU_RUN_TEST(test_no_question_without_the_flag_or_a_match);
  MU_RUN_TEST(test_an_interrupt_ends_the_asking);
  MU_RUN_TEST(test_the_embedder_may_look_while_it_is_asked);
  MU_RUN_TEST(test_with_nobody_to_ask_it_is_an_error);
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
