#include "libvim.h"
#include "minunit.h"

/*
 * Spell checking, restored from upstream vim. These tests drive the engine
 * through vim's own spellbadword()/spellsuggest() functions, using the English
 * dictionary shipped in runtime/spell (tests run from src/apitest).
 */

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("set runtimepath=../../runtime");
  vimExecute("set spelllang=en");
  vimExecute("set spell");
}

void test_teardown(void) {}

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
}

/* Evaluate "expr" and check it equals "expected"; frees the result. */
static int evalEquals(char *expr, char *expected)
{
  char_u *result = vimEval((char_u *)expr);
  int ok = result != NULL && strcmp((char *)result, expected) == 0;

  printf("%s => |%s| (expected |%s|)\n", expr, result ? (char *)result : "(null)", expected);
  vim_free(result);
  return ok;
}

MU_TEST(test_has_spell)
{
  mu_check(evalEquals("has('spell')", "1"));
}

MU_TEST(test_correct_text_has_no_bad_word)
{
  mu_check(evalEquals("join(spellbadword('The quick brown fox.'), '|')", "|"));
}

MU_TEST(test_misspelled_word_is_bad)
{
  mu_check(evalEquals("join(spellbadword('hello wrold'), '|')", "wrold|bad"));
}

MU_TEST(test_lowercase_sentence_start_is_caps)
{
  mu_check(evalEquals("join(spellbadword('Hello there. this is fine'), '|')", "this|caps"));
}

MU_TEST(test_suggestions_include_correction)
{
  mu_check(evalEquals("index(spellsuggest('wrold', 10), 'world') >= 0", "1"));
}

MU_TEST(test_spell_off_reports_nothing)
{
  vimExecute("set nospell");
  mu_check(evalEquals("join(spellbadword('hello wrold'), '|')", "|"));
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_has_spell);
  MU_RUN_TEST(test_correct_text_has_no_bad_word);
  MU_RUN_TEST(test_misspelled_word_is_bad);
  MU_RUN_TEST(test_lowercase_sentence_start_is_caps);
  MU_RUN_TEST(test_suggestions_include_correction);
  MU_RUN_TEST(test_spell_off_reports_nothing);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);
  vimSetMessageCallback(&onMessage);

  win_setwidth(80);
  win_setheight(40);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
