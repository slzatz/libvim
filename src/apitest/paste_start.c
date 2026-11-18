#include "libvim.h"
#include "minunit.h"
#include "vim.h"

/*
 * <PasteStart> and <PasteEnd> are nothing.
 *
 * They are the codes a terminal puts around a bracketed paste. libvim has no
 * terminal and no end code to look for: <PasteStart> in Normal, Insert or
 * Command-line mode crashed (bracketed_paste() took the length of a NULL end
 * code), and in Visual mode deleted the selection and started Insert mode.
 * An embedder pastes by sending the text; the keys themselves do nothing,
 * however they arrive.
 *
 * A test that fails here most likely does so by crashing.
 */

static const char *first = "This is the first line of a test file";

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimExecute("mapclear");
  vimInput("g");
  vimInput("g");
  vimInput("0");
}

void test_teardown(void) {}

static int first_line_intact(void)
{
  return strcmp((char *)vimBufferGetLine(curbuf, 1), first) == 0;
}

MU_TEST(test_normal)
{
  vimKey("<PasteStart>");
  vimKey("<PasteEnd>");
  mu_check((vimGetMode() & NORMAL) == NORMAL);
  mu_check(first_line_intact());

  // nothing is held: the next key is a command as usual
  vimInput("x");
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1), first + 1) == 0);
}

MU_TEST(test_insert)
{
  vimInput("i");
  vimKey("<PasteStart>");
  vimInput("a");
  vimKey("<PasteEnd>");
  mu_check((vimGetMode() & INSERT) == INSERT);
  vimKey("<esc>");
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1),
                  "aThis is the first line of a test file") == 0);
}

MU_TEST(test_cmdline)
{
  vimInput(":");
  vimKey("<PasteStart>");
  mu_check((vimGetMode() & CMDLINE) == CMDLINE);
  vimInput("d");
  vimKey("<cr>");
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1),
                  "This is the second line of a test file") == 0);
}

MU_TEST(test_visual)
{
  vimInput("v");
  vimInput("e");
  vimKey("<PasteStart>");
  mu_check((vimGetMode() & VISUAL) == VISUAL);
  mu_check(first_line_intact());
}

MU_TEST(test_mapping_and_normal)
{
  vimExecute("nnoremap Q <PasteStart>x");
  vimSetMappingsEnabled(TRUE);
  vimInput("Q");
  vimSetMappingsEnabled(FALSE);
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1), first + 1) == 0);

  vimExecute("e!");
  vimExecute("execute \"normal i\\<PasteStart>b\\<Esc>\"");
  mu_check(strcmp((char *)vimBufferGetLine(curbuf, 1),
                  "bThis is the first line of a test file") == 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_normal);
  MU_RUN_TEST(test_insert);
  MU_RUN_TEST(test_cmdline);
  MU_RUN_TEST(test_visual);
  MU_RUN_TEST(test_mapping_and_normal);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  win_setwidth(80);
  win_setheight(100);

  vimBufferOpen("collateral/testfile.txt", 1, 0);

  MU_RUN_SUITE(test_suite);
  MU_REPORT();
  MU_RETURN();
}
