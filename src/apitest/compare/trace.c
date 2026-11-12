/*
 * trace: give libvim one step at a time and show the state after each. For
 * looking closely at a sequence that probe and vim disagree on.
 *
 *   ./trace.exe i '<c-o>' : 's/a/b/' '<cr>' X '<esc>'
 *
 * Each argument is a step:
 *
 *   {EX}cmd        vimExecute(cmd)
 *   {EVAL}expr     vimEval(expr), and print the result
 *   {TO}           vimInputTimeout()
 *   {RESTRICTED}   turn restricted mode on
 *   <name>         vimKey()
 *   anything else  vimInput(), in one piece
 *
 * The buffer is probe's: five lines, "alpha one" to "echo five". Set
 * LIBVIM_RUNTIME to a directory for 'runtimepath' if spell files are wanted.
 */
#include "libvim.h"
#include "vim.h"

static void onMessage(char_u *title, char_u *msg, msgPriority_T priority)
{
  printf("      msg[%d]: %s\n", priority, msg);
}

static void show(const char *step)
{
  int mode = vimGetMode();
  linenr_T i;

  printf("%-14s mode=0x%-4x pending=%d cursor=%ld,%d", step, mode, vimInputPending(),
         (long)vimCursorGetLine(), (int)vimCursorGetColumn());
  if ((mode & CMDLINE) == CMDLINE && vimCommandLineGetText() != NULL)
    printf(" cmdline=%c%s", vimCommandLineGetType(), vimCommandLineGetText());
  printf(" |");
  for (i = 1; i <= (linenr_T)vimBufferGetLineCount(curbuf) && i <= 6; i++)
    printf("%s|", vimBufferGetLine(curbuf, i));
  printf("\n");
}

int main(int argc, char **argv)
{
  char *lines[] = {"alpha one", "bravo two", "charlie three", "delta four", "echo five"};
  char *runtime = getenv("LIBVIM_RUNTIME");
  buf_T *buf;
  int i;

  vimInit(0, NULL);
  vimSetMessageCallback(onMessage);
  buf = vimBufferNew(0);
  vimBufferSetCurrent(buf);
  vimBufferSetLines(buf, 0, -1, (char_u **)lines, 5);
  if (runtime != NULL && *runtime != 0)
  {
    char cmd[2048];

    snprintf(cmd, sizeof(cmd), "set runtimepath+=%s", runtime);
    vimExecute((char_u *)cmd);
  }
  vimSetMappingsEnabled(TRUE);

  alarm(30); /* a step that never returns ends the program */

  for (i = 1; i < argc; i++)
  {
    char *step = argv[i];
    size_t n = strlen(step);

    if (strncmp(step, "{EX}", 4) == 0)
      vimExecute((char_u *)step + 4);
    else if (strncmp(step, "{EVAL}", 6) == 0)
    {
      char_u *result = vimEval((char_u *)step + 6);

      printf("      = %s\n", result == NULL ? "(null)" : (char *)result);
      vim_free(result);
    }
    else if (strcmp(step, "{TO}") == 0)
      vimInputTimeout();
    else if (strcmp(step, "{RESTRICTED}") == 0)
      restricted = 1;
    else if (n > 2 && step[0] == '<' && step[n - 1] == '>')
      vimKey((char_u *)step);
    else
      vimInput((char_u *)step);

    show(step);
  }

  return 0;
}
