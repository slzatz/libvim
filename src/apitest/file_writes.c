#include "libvim.h"
#include "minunit.h"

#include <sys/stat.h>
#include <unistd.h>

/*
 * vimSetFileWritesEnabled(FALSE): vim writes no file, by any route, except
 * the 'spellfile' word list. And restricted mode starts no program, including
 * the two routes that reached the shell without asking: ":diffpatch" and an
 * external diff.
 */

#define MAX_TEST_MESSAGE 8192

static char_u lastError[MAX_TEST_MESSAGE];
static int errorCount = 0;
static int restrictedErrors = 0; /* E145s */

static char dir[256];

void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("onMessage - title: |%s| contents: |%s|\n", title, msg);
  if (priority == MSG_ERROR)
  {
    assert(strlen((char *)msg) < MAX_TEST_MESSAGE);
    strcpy((char *)lastError, (char *)msg);
    errorCount++;
    if (strstr((char *)msg, "E145") != NULL)
      restrictedErrors++;
  }
}

/* path of a file in the test's own directory; a different buffer each call */
static char *in_dir(const char *name)
{
  static char bufs[4][512];
  static int next = 0;
  char *buf = bufs[next++ % 4];
  snprintf(buf, 512, "%s/%s", dir, name);
  return buf;
}

static int exists(const char *path)
{
  struct stat st;
  return stat(path, &st) == 0;
}

/* run an ex command in which every %s is the path */
static void ex(const char *format, const char *path)
{
  char cmd[2048];
  snprintf(cmd, sizeof(cmd), format, path, path, path);
  vimExecute((char_u *)cmd);
}

static void write_text(const char *path, const char *text, int mode)
{
  FILE *f = fopen(path, "w");
  mu_check(f != NULL);
  fputs(text, f);
  fclose(f);
  chmod(path, mode);
}

static int file_is(const char *path, const char *want)
{
  char got[256] = "";
  FILE *f = fopen(path, "r");
  if (f == NULL)
    return 0;
  size_t n = fread(got, 1, sizeof(got) - 1, f);
  got[n] = '\0';
  fclose(f);
  return strcmp(got, want) == 0;
}

void test_setup(void)
{
  vimKey("<esc>");
  vimKey("<esc>");
  vimExecute("e!");
  vimSetFileWritesEnabled(TRUE);
  restricted = FALSE;
  errorCount = 0;
  restrictedErrors = 0;
  lastError[0] = '\0';
}

void test_teardown(void)
{
  vimSetFileWritesEnabled(TRUE);
  restricted = FALSE;
}

MU_TEST(test_writes_are_on_by_default)
{
  char *path = in_dir("default");
  mu_check(vimGetFileWritesEnabled() == TRUE);
  ex("1,2w %s", path);
  mu_check(exists(path));
  mu_check(errorCount == 0);
}

/* Each command here writes a file in plain vim -- the control at the end
 * checks that -- and none does with file writes off. */
static const char *writers[] = {
    "w %s",
    "1,2w %s",
    "w! %s",
    "saveas %s",
    "execute 'w %s'",
    "execute \"normal! :w %s\\<CR>\"",
    "call writefile(['x'], '%s')",
    "call writefile(['x'], '%s', 'a')",
    "redir > %s | silent echo 1 | redir END",
    "redir >> %s | silent echo 1 | redir END",
    "mkexrc %s",
    "mkvimrc %s",
    "mksession %s",
    "mkview %s",
    "set verbosefile=%s | set verbose=1 | silent echo 1 | set verbose=0 verbosefile=",
};
#define WRITERS (sizeof(writers) / sizeof(writers[0]))

MU_TEST(test_nothing_is_written_when_off)
{
  char name[32];

  vimSetFileWritesEnabled(FALSE);
  mu_check(vimGetFileWritesEnabled() == FALSE);
  for (int i = 0; i < WRITERS; i++)
  {
    snprintf(name, sizeof(name), "off%d", i);
    char *path = in_dir(name);
    printf("off: %s\n", writers[i]);
    ex(writers[i], path);
    vimKey("<esc>");
    vimExecute("redir END");
    vimExecute("set verbose=0 verbosefile=");
    mu_check(!exists(path));
  }
  /* the ones that exist to write a file say so */
  mu_check(errorCount > 0);
  mu_check(strstr((char *)lastError, "not allowed") != NULL || errorCount > 1);

  /* the control: the same commands, with writes on, each make their file */
  vimSetFileWritesEnabled(TRUE);
  for (int i = 0; i < WRITERS; i++)
  {
    snprintf(name, sizeof(name), "on%d", i);
    char *path = in_dir(name);
    printf("on: %s\n", writers[i]);
    ex(writers[i], path);
    vimKey("<esc>");
    vimExecute("redir END");
    vimExecute("set verbose=0 verbosefile=");
    mu_check(exists(path));
  }
  /* :saveas renamed the buffer; put the test's file back */
  vimBufferOpen("collateral/lines_100.txt", 1, 0);
}

MU_TEST(test_refusal_is_reported)
{
  char *path = in_dir("reported");
  vimSetFileWritesEnabled(FALSE);

  ex("w %s", path);
  mu_check(errorCount == 1);
  mu_check(strstr((char *)lastError, "Writing files is not allowed") != NULL);

  errorCount = 0;
  ex("call writefile(['x'], '%s')", path);
  mu_check(errorCount == 1);
  mu_check(strstr((char *)lastError, "Writing files is not allowed") != NULL);

  mu_check(!exists(path));
}

/* A file that is already there is not changed, renamed or backed up. */
MU_TEST(test_existing_file_is_left_alone)
{
  char *path = in_dir("existing");
  char backup[600];
  snprintf(backup, sizeof(backup), "%s~", path);
  write_text(path, "original\n", 0644);

  vimSetFileWritesEnabled(FALSE);
  vimExecute("set backup writebackup backupcopy=no");
  ex("w! %s", path);
  ex("w >> %s", path);
  ex("call writefile(['changed'], '%s')", path);
  ex("call writefile(['more'], '%s', 'a')", path);
  vimExecute("set nobackup backupcopy&");

  mu_check(file_is(path, "original\n"));
  mu_check(!exists(backup));
}

/* 'swapfile' is one ":set" away, and a swap file is a file. */
MU_TEST(test_no_swap_file_when_off)
{
  char *path = in_dir("swapped");
  char swap[600];
  snprintf(swap, sizeof(swap), "%s/.swapped.swp", dir);

  vimSetFileWritesEnabled(FALSE);
  ex("set swapfile directory=%s", dir);
  ex("e %s", path);
  vimInput("i");
  vimInput("x");
  vimKey("<esc>");
  vimExecute("preserve");
  mu_check(!exists(swap));

  vimExecute("set noswapfile directory&");
  vimExecute("bwipeout!");
  vimBufferOpen("collateral/lines_100.txt", 1, 0);
}

MU_TEST(test_reading_still_works_when_off)
{
  vimSetFileWritesEnabled(FALSE);
  char_u *n = vimEval((char_u *)"len(readfile('collateral/lines_100.txt'))");
  mu_check(n != NULL && strcmp((char *)n, "100") == 0);
  vim_free(n);
  mu_check(errorCount == 0);
}

/* The spell file is vim's own: zg and :spellgood write it regardless, and
 * the .spl made from it. :mkspell, which writes where it is told, does not. */
MU_TEST(test_spell_file_is_still_written)
{
  char *add = in_dir("en.utf-8.add");
  char spl[600];
  snprintf(spl, sizeof(spl), "%s.spl", add);

  vimExecute("set runtimepath=../../runtime");
  vimExecute("set spelllang=en spell");
  ex("set spellfile=%s", add);
  vimSetFileWritesEnabled(FALSE);

  vimExecute("spellgood zzxqword");
  mu_check(file_is(add, "zzxqword\n"));
  mu_check(exists(spl));
  mu_check(errorCount == 0);

  char *made = in_dir("made");
  char madeSpl[600];
  snprintf(madeSpl, sizeof(madeSpl), "%s.utf-8.spl", made);
  char cmd[1400];
  snprintf(cmd, sizeof(cmd), "mkspell! %s %s", made, add);
  vimExecute((char_u *)cmd);
  mu_check(!exists(madeSpl));
  mu_check(strstr((char *)lastError, "Writing files is not allowed") != NULL);

  vimExecute("set nospell spellfile=");
}

/* vimInit starts over with writes on, as it does for mappings. */
MU_TEST(test_setting_is_plain_boolean)
{
  vimSetFileWritesEnabled(0);
  mu_check(vimGetFileWritesEnabled() == FALSE);
  vimSetFileWritesEnabled(5);
  mu_check(vimGetFileWritesEnabled() == TRUE);
}

/*
 * Restricted mode starts no program. 'shell' is a script that leaves a mark,
 * so a shell that ran shows. ":!" was always refused; ":diffpatch" called the
 * shell without asking.
 */
MU_TEST(test_restricted_mode_starts_no_shell)
{
  char *shell = in_dir("shell.sh");
  char *mark = in_dir("shell-ran");
  char *patch = in_dir("a.patch");
  char script[1024];
  snprintf(script, sizeof(script), "#!/bin/sh\ntouch '%s'\n", mark);
  write_text(shell, script, 0755);
  write_text(patch, "not a patch\n", 0644);
  ex("set shell=%s shellcmdflag=-c shellredir=> shellxquote=", shell);

  restricted = TRUE;
  ex("diffpatch %s", patch);
  mu_check(!exists(mark));
  mu_check(restrictedErrors == 1);
  vimExecute("silent! diffoff!");
  vimExecute("silent! only!");

  vimExecute("set diffopt-=internal");
  vimExecute("silent! diffthis");
  vimExecute("silent! diffupdate");
  mu_check(!exists(mark));
  vimExecute("silent! diffoff!");
  vimExecute("set diffopt+=internal");

  ex("silent! call system('true')%.0s", "");
  ex("silent! !true%.0s", "");
  mu_check(!exists(mark));

  /* the control: unrestricted, :diffpatch does run the shell */
  restricted = FALSE;
  ex("silent! diffpatch %s", patch);
  mu_check(exists(mark));
  vimExecute("silent! diffoff!");
  vimExecute("silent! only!");

  vimExecute("set shell& shellcmdflag& shellredir& shellxquote&");
  vimBufferOpen("collateral/lines_100.txt", 1, 0);
}

MU_TEST_SUITE(test_suite)
{
  MU_SUITE_CONFIGURE(&test_setup, &test_teardown);

  MU_RUN_TEST(test_writes_are_on_by_default);
  MU_RUN_TEST(test_nothing_is_written_when_off);
  MU_RUN_TEST(test_refusal_is_reported);
  MU_RUN_TEST(test_existing_file_is_left_alone);
  MU_RUN_TEST(test_no_swap_file_when_off);
  MU_RUN_TEST(test_reading_still_works_when_off);
  MU_RUN_TEST(test_spell_file_is_still_written);
  MU_RUN_TEST(test_setting_is_plain_boolean);
  MU_RUN_TEST(test_restricted_mode_starts_no_shell);
}

int main(int argc, char **argv)
{
  vimInit(argc, argv);

  vimSetMessageCallback(&onMessage);

  win_setwidth(5);
  win_setheight(100);

  snprintf(dir, sizeof(dir), "/tmp/libvim-file-writes-XXXXXX");
  if (mkdtemp(dir) == NULL)
  {
    perror("mkdtemp");
    return 1;
  }

  vimBufferOpen("collateral/lines_100.txt", 1, 0);

  MU_RUN_SUITE(test_suite);

  char cleanup[300];
  snprintf(cleanup, sizeof(cleanup), "rm -rf '%s'", dir);
  if (system(cleanup) != 0)
    printf("could not remove %s\n", dir);

  MU_REPORT();
  MU_RETURN();
}
