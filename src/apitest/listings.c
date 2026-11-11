#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * What a command lists reaches the message callback, as one message.
 *
 * vim writes a listing -- ":registers", ":map", ":print", ":let" -- a piece
 * at a time, with msg_puts(), msg_putchar() and msg_advance(), and upstream
 * libvim dropped the pieces: those commands said nothing. Some never
 * returned, because msg_advance() writes blanks until msg_col reaches a
 * column and msg_col stood still.
 */

#define MAX_MESSAGES 16
#define MAX_TEST_MESSAGE 16384

static char_u messages[MAX_MESSAGES][MAX_TEST_MESSAGE];
static msgPriority_T priorities[MAX_MESSAGES];
static int messageCount = 0;
static int versionCallbackCount = 0;

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  assert(strlen((char *)msg) + strlen((char *)title) < MAX_TEST_MESSAGE);
  if (messageCount < MAX_MESSAGES)
  {
    strcpy((char *)messages[messageCount], (char *)title);
    strcat((char *)messages[messageCount], (char *)msg);
    priorities[messageCount] = priority;
  }
  messageCount++;
}

void onVersion(void)
{
  versionCallbackCount++;
}

static int has(int i, char *text)
{
  return i < messageCount && strstr((char *)messages[i], text) != NULL;
}

static int is(int i, char *text)
{
  return i < messageCount && strcmp((char *)messages[i], text) == 0;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");

  vimExecute("e!");
  vimExecute("mapclear");
  vimExecute("mapclear!");
  vimExecute("set history=50");
  vimInput("g");
  vimInput("g");
  vimInput("0");

  messageCount = 0;
  versionCallbackCount = 0;
}

void test_teardown(void) {}

MU_TEST(test_registers)
{
  vimInput("\"");
  vimInput("a");
  vimInput("y");
  vimInput("w");
  messageCount = 0;

  vimExecute("registers a");

  mu_check(messageCount == 1);
  mu_check(priorities[0] == MSG_INFO);
  // no blank line in front, nothing after the last character
  mu_check(is(0, "--- Registers ---\n\"a   This"));
}

MU_TEST(test_map_lists_mappings)
{
  vimExecute("nnoremap ,d dd");
  vimExecute("inoremap jk <Esc>");
  messageCount = 0;

  vimExecute("map");
  mu_check(messageCount == 1);
  mu_check(is(0, "n  ,d          * dd"));

  messageCount = 0;
  vimExecute("imap");
  mu_check(messageCount == 1);
  mu_check(is(0, "i  jk          * <Esc>"));
}

MU_TEST(test_print_puts_each_line_on_its_own)
{
  vimExecute("1,3print");
  mu_check(messageCount == 1);
  mu_check(is(0, "This is the first line of a test file\n"
                 "This is the second line of a test file\n"
                 "This is the third line of a test file"));

  messageCount = 0;
  vimExecute("2,3number");
  mu_check(messageCount == 1);
  mu_check(is(0, "  2 This is the second line of a test file\n"
                 "  3 This is the third line of a test file"));

  messageCount = 0;
  vimExecute("g/second/p");
  mu_check(messageCount == 1);
  mu_check(is(0, "This is the second line of a test file"));
}

MU_TEST(test_listing_a_variable_returns)
{
  // each of these used to loop for ever in msg_advance()
  vimExecute("let g:listing_test = 5");
  messageCount = 0;

  vimExecute("let g:listing_test");
  mu_check(messageCount == 1);
  mu_check(is(0, "g:listing_test        #5"));

  messageCount = 0;
  vimExecute("let");
  mu_check(messageCount == 1);
  mu_check(has(0, "listing_test          #5\n"));
  mu_check(has(0, "\nv:version "));

  messageCount = 0;
  vimExecute("highlight");
  mu_check(messageCount == 1);
  mu_check(has(0, "ErrorMsg "));
}

MU_TEST(test_history)
{
  vimExecute("call histadd(':', 'echo 1')");
  vimExecute("call histadd(':', 'echo 2')");
  messageCount = 0;

  vimExecute("history");
  mu_check(messageCount == 1);
  mu_check(has(0, "cmd history"));
  mu_check(has(0, "echo 1\n"));
  mu_check(has(0, "echo 2"));

  // by name, another kind
  messageCount = 0;
  vimExecute("history /");
  mu_check(messageCount == 1);
  mu_check(has(0, "search history"));
  mu_check(!has(0, "echo 1"));
}

MU_TEST(test_version_is_vims_without_a_callback)
{
  vimExecute("version");
  mu_check(versionCallbackCount == 0);
  mu_check(messageCount >= 1);
  mu_check(has(0, "VIM - Vi IMproved"));
  mu_check(has(messageCount - 1, "+eval"));

  // an embedder with a version of its own is asked for it, as before
  vimSetDisplayVersionCallback(&onVersion);
  messageCount = 0;
  vimExecute("version");
  vimSetDisplayVersionCallback(NULL);
  mu_check(versionCallbackCount == 1);
  mu_check(messageCount == 0);
}

MU_TEST(test_messages_lists_what_was_said)
{
  vimExecute("messages clear");
  vimExecute("echomsg 'said once'");
  vimExecute("echomsg 'said twice'");
  messageCount = 0;

  vimExecute("messages");
  // one listing: the old messages are not sent again as new ones
  mu_check(messageCount == 1);
  mu_check(is(0, "said once\nsaid twice"));

  messageCount = 0;
  vimExecute("1messages");
  mu_check(messageCount == 1);
  mu_check(is(0, "said twice"));

  vimExecute("messages clear");
  messageCount = 0;
  vimExecute("messages");
  mu_check(messageCount == 0);
}

MU_TEST(test_listing_and_messages_keep_their_order)
{
  vimInput("\"");
  vimInput("a");
  vimInput("y");
  vimInput("w");
  messageCount = 0;

  vimExecute("echo 'before' | registers a | echo 'after'");

  mu_check(messageCount == 3);
  mu_check(is(0, "before"));
  mu_check(has(1, "--- Registers ---"));
  mu_check(is(2, "after"));
}

MU_TEST(test_echoerr_is_one_error)
{
  vimExecute("echoerr 'wrong'");
  mu_check(messageCount == 1);
  mu_check(priorities[0] == MSG_ERROR);
  mu_check(has(0, "wrong"));
}

MU_TEST(test_commands_with_a_message_of_their_own_say_it_once)
{
  // these send their text themselves and still write it to the screen
  vimExecute("echo 'one'");
  mu_check(messageCount == 1);
  mu_check(is(0, "one"));

  vimInput("m");
  vimInput("a");
  messageCount = 0;
  vimExecute("marks a");
  mu_check(messageCount == 1);

  messageCount = 0;
  vimExecute("1,3s/This/That/");
  mu_check(messageCount == 1);
  mu_check(is(0, "3 substitutions on 3 lines"));

  messageCount = 0;
  vimExecute("nonexistentcommand");
  mu_check(messageCount == 1);
  mu_check(priorities[0] == MSG_ERROR);
}

MU_TEST(test_echon_pieces_are_one_message)
{
  vimExecute("echon 'x' | echon 'y'");
  mu_check(messageCount == 1);
  mu_check(is(0, "xy"));
}

MU_TEST(test_silent_lists_nothing)
{
  vimExecute("silent registers");
  mu_check(messageCount == 0);

  // and execute() still captures the listing for the script
  vimExecute("let g:listing_test = execute('1print')");
  mu_check(messageCount == 0);
  vimExecute("echo g:listing_test");
  mu_check(messageCount == 1);
  mu_check(has(0, "This is the first line of a test file"));
}

MU_TEST(test_command_that_lists_nothing_sends_nothing)
{
  vimExecute("1,2>");
  vimExecute("2");
  vimExecute("let g:listing_test = 6");
  vimExecute("normal! yy");
  mu_check(messageCount == 0);
}

MU_TEST(test_keys_outside_a_command_send_no_listing)
{
  // "n" echoes its pattern on vim's command line; that is not a message
  vimInput("/");
  vimInput("l");
  vimInput("i");
  vimInput("n");
  vimInput("e");
  vimKey("<cr>");
  messageCount = 0;

  vimInput("n");
  mu_check(messageCount == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_registers);
  MU_RUN_TEST(test_map_lists_mappings);
  MU_RUN_TEST(test_print_puts_each_line_on_its_own);
  MU_RUN_TEST(test_listing_a_variable_returns);
  MU_RUN_TEST(test_history);
  MU_RUN_TEST(test_version_is_vims_without_a_callback);
  MU_RUN_TEST(test_messages_lists_what_was_said);
  MU_RUN_TEST(test_listing_and_messages_keep_their_order);
  MU_RUN_TEST(test_echoerr_is_one_error);
  MU_RUN_TEST(test_commands_with_a_message_of_their_own_say_it_once);
  MU_RUN_TEST(test_echon_pieces_are_one_message);
  MU_RUN_TEST(test_silent_lists_nothing);
  MU_RUN_TEST(test_command_that_lists_nothing_sends_nothing);
  MU_RUN_TEST(test_keys_outside_a_command_send_no_listing);
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
