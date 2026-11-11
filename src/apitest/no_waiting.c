#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * Nothing waits for a key.
 *
 * vim asks the user things from deep inside a command: input() and its
 * relatives read a line, the debugger reads a command. Each waits for keys,
 * and in libvim none can come -- keys arrive between calls, through
 * vimInput(). These never returned. Now the reader is told at once that
 * nothing was typed, as vim tells it inside ":normal", and gives up: the
 * question is answered as <Esc> would answer it.
 *
 * A test that fails here most likely does so by never finishing.
 */

static char said[4096];
static int messageCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
  messageCount++;
}

/* Type keys: a <name> goes through vimKey, anything else a byte at a time. */
static void type(char *s)
{
  char one[2] = {0, 0};

  while (*s)
  {
    if (*s == '<')
    {
      char key[32];
      char *end = strchr(s, '>');
      size_t n = end - s + 1;

      memcpy(key, s, n);
      key[n] = 0;
      vimKey((char_u *)key);
      s = end + 1;
    }
    else
    {
      one[0] = *s++;
      vimInput((char_u *)one);
    }
  }
}

static int evaluates_to(char *expr, char *want)
{
  char_u *got = vimEval((char_u *)expr);
  int same = got != NULL && strcmp((char *)got, want) == 0;

  if (!same)
    printf("%s is |%s|, not |%s|\n", expr, got == NULL ? "(null)" : (char *)got, want);
  vim_free(got);
  return same;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimExecute("let g:answer = 'unset'");
  type("gg0");
  said[0] = 0;
  messageCount = 0;
}

void test_teardown(void) {}

MU_TEST(test_input_returns_nothing)
{
  vimExecute("let g:answer = input('name: ')");
  mu_check(evaluates_to("g:answer", ""));

  // even when a default was offered: nobody accepted it
  vimExecute("let g:answer = input('name: ', 'default')");
  mu_check(evaluates_to("g:answer", ""));

  vimExecute("let g:answer = inputsecret('password: ')");
  mu_check(evaluates_to("g:answer", ""));

  vimExecute("let g:answer = inputdialog('name: ')");
  mu_check(evaluates_to("g:answer", ""));
}

MU_TEST(test_inputlist_returns_zero)
{
  vimExecute("let g:answer = inputlist(['Pick one:', '1. this', '2. that'])");
  mu_check(evaluates_to("g:answer", "0"));
}

MU_TEST(test_the_script_goes_on)
{
  // not an interrupt: what follows the question still runs
  vimExecute("let g:answer = input('name: ') | let g:after = 'ran'");
  mu_check(evaluates_to("g:after", "ran"));
  mu_check(got_int == FALSE);

  vimExecute("function! Ask()\n  let l:name = input('name: ')\n  return 'hello [' . l:name . ']'\nendfunction");
  mu_check(evaluates_to("Ask()", "hello []"));
}

MU_TEST(test_input_from_a_typed_command)
{
  type(":let g:answer = 'x' . input('name: ')<cr>");
  mu_check((vimGetMode() & NORMAL) == NORMAL);
  mu_check(evaluates_to("g:answer", "x"));
}

MU_TEST(test_input_from_a_mapping)
{
  vimExecute("nnoremap Q :let g:answer = 'q' . input('name: ')<CR>");
  vimExecute("inoremap <F2> <C-r>=input('name: ')<CR>!");
  vimSetMappingsEnabled(TRUE);
  type("Q");
  mu_check(evaluates_to("g:answer", "q"));

  // The "!" after the <CR> is not inserted: it is typed already, so it is
  // what input() reads -- vim does the same, and has inputsave() for it.
  // Then nothing more comes, and the question is given up.
  type("i<f2>");
  vimSetMappingsEnabled(FALSE);
  mu_check((vimGetMode() & INSERT) == INSERT);
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1), "This is the first line of a test file") == 0);
}

MU_TEST(test_input_from_normal)
{
  vimExecute("normal! :let g:answer = 'n' . input('name: ')\r");
  mu_check(evaluates_to("g:answer", "n"));
}

MU_TEST(test_keys_already_typed_are_the_answer)
{
  // giving up is for when there is nothing to read: keys that follow the
  // question in the same ":normal" are its answer, as in vim
  vimExecute("normal! :let g:answer = input('name: ')\rfed\r");
  mu_check(evaluates_to("g:answer", "fed"));
}

MU_TEST(test_debug_mode_goes_on)
{
  vimExecute("debug let g:answer = 'debugged'");
  mu_check(evaluates_to("g:answer", "debugged"));
  // and is over
  vimExecute("let g:answer = 'after'");
  mu_check(evaluates_to("g:answer", "after"));
}

MU_TEST(test_getchar_and_confirm)
{
  vimExecute("let g:answer = getchar()");
  mu_check(evaluates_to("g:answer", "0"));
  vimExecute("let g:answer = confirm('sure?', \"&Yes\\n&No\")");
  mu_check(evaluates_to("g:answer", "0"));
}

MU_TEST(test_a_held_key_still_waits)
{
  // the one thing that does wait, between calls: the first key of a mapping
  vimExecute("inoremap jk <Esc>");
  vimSetMappingsEnabled(TRUE);
  type("ij");
  mu_check(vimInputPending() == 1);
  mu_check((vimGetMode() & INSERT) == INSERT);
  type("k");
  vimSetMappingsEnabled(FALSE);
  mu_check((vimGetMode() & NORMAL) == NORMAL);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_input_returns_nothing);
  MU_RUN_TEST(test_inputlist_returns_zero);
  MU_RUN_TEST(test_the_script_goes_on);
  MU_RUN_TEST(test_input_from_a_typed_command);
  MU_RUN_TEST(test_input_from_a_mapping);
  MU_RUN_TEST(test_input_from_normal);
  MU_RUN_TEST(test_keys_already_typed_are_the_answer);
  MU_RUN_TEST(test_debug_mode_goes_on);
  MU_RUN_TEST(test_getchar_and_confirm);
  MU_RUN_TEST(test_a_held_key_still_waits);
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
