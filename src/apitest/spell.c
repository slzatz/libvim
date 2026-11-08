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

/* Replace the whole current buffer with "lines". */
static void setLines(char **lines, int count)
{
  vimBufferSetLines(curbuf, 0, (linenr_T)vimBufferGetLineCount(curbuf), (char_u **)lines, count);
}

/* Errors in the whole current buffer. */
static spellError_T *getErrors(int *count)
{
  spellError_T *errors = NULL;

  vimSpellGetErrors(curbuf, 1, (linenr_T)vimBufferGetLineCount(curbuf), count, &errors);
  for (int i = 0; i < *count; i++)
    printf("  error %d: lnum=%ld col=%d len=%d type=%d\n", i, (long)errors[i].lnum,
           errors[i].col, errors[i].len, errors[i].type);
  return errors;
}

MU_TEST(test_api_no_errors)
{
  char *lines[] = {"The quick brown fox.", "It jumps over the dog."};
  int count = -1;

  setLines(lines, 2);
  spellError_T *errors = getErrors(&count);
  mu_check(count == 0);
  mu_check(errors == NULL);
}

MU_TEST(test_api_bad_words_in_order)
{
  char *lines[] = {"Hello wrold here.", "No errors.", "Twoo bad wurds."};
  int count = -1;

  setLines(lines, 3);
  spellError_T *errors = getErrors(&count);
  mu_check(count == 3);
  mu_check(errors[0].lnum == 1 && errors[0].col == 6 && errors[0].len == 5);
  mu_check(errors[0].type == SPELL_ERROR_BAD);
  mu_check(errors[1].lnum == 3 && errors[1].col == 0 && errors[1].len == 4);
  mu_check(errors[2].lnum == 3 && errors[2].col == 9 && errors[2].len == 5);
  vim_free(errors);
}

MU_TEST(test_api_caps_after_sentence_end_on_previous_line)
{
  char *lines[] = {"This ends here.", "  next line starts lowercase", "and so does this"};
  int count = -1;

  setLines(lines, 3);
  spellError_T *errors = getErrors(&count);
  /* Only line 2 follows a sentence end; line 3 continues line 2's sentence. */
  mu_check(count == 1);
  mu_check(errors[0].lnum == 2 && errors[0].col == 2 && errors[0].len == 4);
  mu_check(errors[0].type == SPELL_ERROR_CAPS);
  vim_free(errors);
}

MU_TEST(test_api_caps_on_first_line_and_after_blank_line)
{
  char *lines[] = {"lowercase start", "", "again lowercase"};
  int count = -1;

  setLines(lines, 3);
  spellError_T *errors = getErrors(&count);
  mu_check(count == 2);
  mu_check(errors[0].lnum == 1 && errors[0].col == 0 && errors[0].type == SPELL_ERROR_CAPS);
  mu_check(errors[1].lnum == 3 && errors[1].col == 0 && errors[1].type == SPELL_ERROR_CAPS);
  vim_free(errors);
}

MU_TEST(test_api_range_uses_line_before_range_for_caps)
{
  char *lines[] = {"Sentence one.", "wrold two"};
  spellError_T *errors = NULL;
  int count = -1;

  setLines(lines, 2);
  vimSpellGetErrors(curbuf, 2, 2, &count, &errors);
  /* "wrold" is both misspelled and missing its capital: reported once. */
  mu_check(count == 1);
  mu_check(errors[0].lnum == 2 && errors[0].col == 0 && errors[0].len == 5);
  vim_free(errors);
}

MU_TEST(test_api_range_is_clamped)
{
  char *lines[] = {"Hello wrold."};
  spellError_T *errors = NULL;
  int count = -1;

  setLines(lines, 1);
  vimSpellGetErrors(curbuf, -5, 1000, &count, &errors);
  mu_check(count == 1);
  mu_check(errors[0].lnum == 1 && errors[0].col == 6);
  vim_free(errors);
}

MU_TEST(test_api_columns_are_bytes)
{
  /* The curly quotes are three bytes each in UTF-8. */
  char *lines[] = {"\xe2\x80\x9cHello\xe2\x80\x9d wrold."};
  int count = -1;

  setLines(lines, 1);
  spellError_T *errors = getErrors(&count);
  mu_check(count == 1);
  mu_check(errors[0].col == 12 && errors[0].len == 5);
  vim_free(errors);
}

MU_TEST(test_api_local_word)
{
  char *lines[] = {"The color is nice."};
  int count = -1;

  vimExecute("set spelllang=en_gb");
  setLines(lines, 1);
  spellError_T *errors = getErrors(&count);
  mu_check(count == 1);
  mu_check(errors[0].col == 4 && errors[0].len == 5 && errors[0].type == SPELL_ERROR_LOCAL);
  vim_free(errors);
}

MU_TEST(test_api_nothing_when_spell_off)
{
  char *lines[] = {"Hello wrold."};
  spellError_T *errors = NULL;
  int count = -1;

  setLines(lines, 1);
  vimExecute("set nospell");
  vimSpellGetErrors(curbuf, 1, 1, &count, &errors);
  mu_check(count == 0);
  mu_check(errors == NULL);
}

static void setCursor(linenr_T lnum, colnr_T col)
{
  pos_T pos = {lnum, col, 0};
  vimCursorSetPosition(pos);
}

static int cursorAt(linenr_T lnum, colnr_T col)
{
  printf("  cursor: %ld,%d (expected %ld,%d)\n", (long)vimCursorGetLine(), vimCursorGetColumn(),
         (long)lnum, col);
  return vimCursorGetLine() == lnum && vimCursorGetColumn() == col;
}

static int errorCount(void)
{
  spellError_T *errors = NULL;
  int count = 0;

  vimSpellGetErrors(curbuf, 1, (linenr_T)vimBufferGetLineCount(curbuf), &count, &errors);
  vim_free(errors);
  return count;
}

MU_TEST(test_next_and_previous_error)
{
  char *lines[] = {"Hello wrold and", "more wurds here."};

  setLines(lines, 2);
  setCursor(1, 0);
  vimInput("]");
  vimInput("s");
  mu_check(cursorAt(1, 6));
  vimInput("]");
  vimInput("s");
  mu_check(cursorAt(2, 5));
  vimInput("[");
  vimInput("s");
  mu_check(cursorAt(1, 6));
}

MU_TEST(test_next_error_with_count)
{
  char *lines[] = {"Hello wrold and", "more wurds here."};

  setLines(lines, 2);
  setCursor(1, 0);
  vimInput("2");
  vimInput("]");
  vimInput("s");
  mu_check(cursorAt(2, 5));
}

MU_TEST(test_next_error_when_none_stays_put)
{
  char *lines[] = {"Nothing wrong here."};

  setLines(lines, 1);
  setCursor(1, 3);
  vimInput("]");
  vimInput("s");
  mu_check(cursorAt(1, 3));
  mu_check(vimGetMode() & NORMAL);
}

MU_TEST(test_zG_accepts_word_and_zuG_undoes_it)
{
  char *lines[] = {"Hello wrold."};

  setLines(lines, 1);
  mu_check(errorCount() == 1);
  setCursor(1, 7);
  vimInput("z");
  vimInput("G");
  mu_check(errorCount() == 0);
  vimInput("z");
  vimInput("u");
  vimInput("G");
  mu_check(errorCount() == 1);
}

MU_TEST(test_zW_marks_good_word_bad)
{
  char *lines[] = {"Hello there."};

  setLines(lines, 1);
  mu_check(errorCount() == 0);
  setCursor(1, 6);
  vimInput("z");
  vimInput("W");
  mu_check(errorCount() == 1);
  vimInput("z");
  vimInput("u");
  vimInput("W");
  mu_check(errorCount() == 0);
}

MU_TEST(test_zg_writes_spellfile_and_zug_undoes_it)
{
  char *lines[] = {"Hello zxqvord."};

  vimExecute("let &spellfile = tempname() . '.utf-8.add'");
  setLines(lines, 1);
  mu_check(errorCount() == 1);
  setCursor(1, 6);
  vimInput("z");
  vimInput("g");
  mu_check(errorCount() == 0);
  mu_check(evalEquals("filereadable(&spellfile)", "1"));
  mu_check(evalEquals("readfile(&spellfile)[0]", "zxqvord"));
  vimInput("z");
  vimInput("u");
  vimInput("g");
  mu_check(errorCount() == 1);
}

MU_TEST(test_zu_with_invalid_key_is_ignored)
{
  char *lines[] = {"Hello wrold and", "more wurds here."};

  setLines(lines, 2);
  setCursor(1, 0);
  vimInput("z");
  vimInput("u");
  vimInput("x");
  mu_check(vimGetMode() & NORMAL);
  vimInput("]");
  vimInput("s");
  mu_check(cursorAt(1, 6));
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

  MU_RUN_TEST(test_api_no_errors);
  MU_RUN_TEST(test_api_bad_words_in_order);
  MU_RUN_TEST(test_api_caps_after_sentence_end_on_previous_line);
  MU_RUN_TEST(test_api_caps_on_first_line_and_after_blank_line);
  MU_RUN_TEST(test_api_range_uses_line_before_range_for_caps);
  MU_RUN_TEST(test_api_range_is_clamped);
  MU_RUN_TEST(test_api_columns_are_bytes);
  MU_RUN_TEST(test_api_local_word);
  MU_RUN_TEST(test_api_nothing_when_spell_off);

  MU_RUN_TEST(test_next_and_previous_error);
  MU_RUN_TEST(test_next_error_with_count);
  MU_RUN_TEST(test_next_error_when_none_stays_put);
  MU_RUN_TEST(test_zG_accepts_word_and_zuG_undoes_it);
  MU_RUN_TEST(test_zW_marks_good_word_bad);
  MU_RUN_TEST(test_zg_writes_spellfile_and_zug_undoes_it);
  MU_RUN_TEST(test_zu_with_invalid_key_is_ignored);
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
