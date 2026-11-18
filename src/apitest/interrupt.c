#include <pthread.h>
#include <unistd.h>

#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * vimInterrupt() stops what vim is running, as CTRL-C does in vim.
 *
 * A command can run for ever: ":while 1 | endwhile", a mapping that types
 * itself again. libvim reads no keys while it runs one, so the user's CTRL-C
 * cannot reach it as a key. vimInterrupt() is called from another thread (or
 * a signal handler) instead; the call that was running returns, and the next
 * one works.
 *
 * Here a second thread interrupts after 200 ms. A test that fails here most
 * likely does so by never finishing: the alarm in main() ends the program
 * after 20 seconds.
 */

static char said[4096];

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  snprintf(said + strlen(said), sizeof(said) - strlen(said), "[%s]", msg);
}

static void *interrupt_soon(void *unused)
{
  usleep(200 * 1000);
  vimInterrupt();
  return NULL;
}

static pthread_t interrupter;

static void interrupt_in_a_moment(void)
{
  pthread_create(&interrupter, NULL, interrupt_soon, NULL);
}

static void interrupted(void)
{
  pthread_join(interrupter, NULL);
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

static int line_is(int n, char *want)
{
  char *got = (char *)vimBufferGetLine(curbuf, n);
  if (strcmp(got, want) != 0)
  {
    printf("line %d is |%s|, not |%s|\n", n, got, want);
    return 0;
  }
  return 1;
}

/* After an interrupt: nothing is left of it, and the next key is a key. The
 * CTRL-C vim makes of an interrupt is not taken as one typed in Normal mode,
 * which says how to quit vim. */
static int works_afterwards(void)
{
  if (strstr(said, ":qa") != NULL)
  {
    printf("the interrupt was taken as a typed CTRL-C: %s\n", said);
    return 0;
  }
  int ok = got_int == FALSE && vimInputPending() == 0;
  ok = ok && (vimGetMode() & NORMAL) == NORMAL;
  vimExecute("e!");
  type("gg0x");
  return ok && line_is(1, "his is the first line of a test file");
}

void test_setup(void)
{
  vimSetMappingsEnabled(FALSE);
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  type("gg0");
  said[0] = 0;
}

void test_teardown(void) {}

MU_TEST(test_while_from_execute)
{
  interrupt_in_a_moment();
  vimExecute("while 1 | endwhile");
  interrupted();
  mu_check(works_afterwards());
}

MU_TEST(test_while_typed)
{
  interrupt_in_a_moment();
  type(":while 1 | endwhile<cr>");
  interrupted();
  mu_check(works_afterwards());
}

MU_TEST(test_while_typed_in_one_call)
{
  // the whole line in one vimInput(), as an embedder that keeps its own
  // command line sends it
  interrupt_in_a_moment();
  vimInput((char_u *)":while 1 | endwhile\r");
  interrupted();
  mu_check(works_afterwards());
}

MU_TEST(test_mapping_that_never_ends)
{
  // each Q moves down and back and types Q again
  vimExecute("nmap Q jkQ");
  vimSetMappingsEnabled(TRUE);
  interrupt_in_a_moment();
  type("Q");
  interrupted();
  vimSetMappingsEnabled(FALSE);
  mu_check(works_afterwards());
}

MU_TEST(test_mapping_in_insert_mode)
{
  vimExecute("imap Q aQ");
  vimSetMappingsEnabled(TRUE);
  interrupt_in_a_moment();
  type("iQ");
  interrupted();
  vimSetMappingsEnabled(FALSE);
  // the inserting stopped, and so did Insert mode, as in vim; what was
  // inserted stays
  mu_check((vimGetMode() & INSERT) == 0);
  mu_check(vimBufferGetLine(curbuf, 1)[0] == 'a');
  mu_check(works_afterwards());
}

MU_TEST(test_while_inside_normal)
{
  // the :normal inside the loop must not swallow the interrupt meant for
  // the loop around it
  interrupt_in_a_moment();
  vimExecute("while 1 | execute \"normal! jk\" | endwhile");
  interrupted();
  mu_check(works_afterwards());
}

MU_TEST(test_function_that_never_returns)
{
  vimExecute("function! Forever()\n  while 1\n  endwhile\nendfunction");
  interrupt_in_a_moment();
  vimExecute("call Forever()");
  interrupted();
  mu_check(works_afterwards());
}

MU_TEST(test_interrupt_between_calls_is_forgotten)
{
  // nothing was running: the next call is not interrupted by it
  vimInterrupt();
  type("x");
  mu_check(line_is(1, "his is the first line of a test file"));
  mu_check(got_int == FALSE);

  vimInterrupt();
  vimExecute("let g:ran = 'yes'");
  char_u *ran = vimEval((char_u *)"g:ran");
  mu_check(ran != NULL && strcmp((char *)ran, "yes") == 0);
  vim_free(ran);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_while_from_execute);
  MU_RUN_TEST(test_while_typed);
  MU_RUN_TEST(test_while_typed_in_one_call);
  MU_RUN_TEST(test_mapping_that_never_ends);
  MU_RUN_TEST(test_mapping_in_insert_mode);
  MU_RUN_TEST(test_while_inside_normal);
  MU_RUN_TEST(test_function_that_never_returns);
  MU_RUN_TEST(test_interrupt_between_calls_is_forgotten);
}

int main(int argc, char **argv)
{
  alarm(20);

  vimInit(argc, argv);

  vimSetMessageCallback(&onMessage);

  win_setwidth(80);
  win_setheight(100);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
