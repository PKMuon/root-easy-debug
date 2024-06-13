// See DebugExample.C for usage.
//
// Written by <lyazj@github.com>.
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <err.h>

#if __has_builtin(__builtin_debugtrap)  // This is expected to work for Clang/LLVM.
#define breakpoint  __builtin_debugtrap
#else  // Manually generate a break point otherwise.
// [NOTE] __builtin_trap() can stop code emitting afterwards thus is not used here.
#define breakpoint()  ({ __asm("int3"); })  // [XXX] This assumes x86_64. Change it on your own need.
#endif  /* __builtin_debugtrap */

static void begin_debug()  // SIGCONT used exclusively.
{
  struct SignalHandler {
    static void handler(int) { /* do nothing */ }
  };

  pid_t pid = fork();
  if(pid < 0) err(EXIT_FAILURE, "fork");

  if(pid == 0) {  // child
    // [XXX] Block other signals here if they can occur before SIGCONT.
    sighandler_t old_sigcont_handler = signal(SIGCONT, SignalHandler::handler);
    pause();  // Wait for a debugger to be ready.
    signal(SIGCONT, old_sigcont_handler);
    return;
  }

  // Run GDB to attach process pid.
  char pid_s[64];
  sprintf(pid_s, "%lld", (long long)pid);
  execlp("gdb", "gdb",
      //"--quiet",
      "--eval-command", "set pagination off",  // Avoid being blocked by screen height.
      "--eval-command", "signal SIGCONT",
      "--eval-command", "set pagination on",
      "--pid", pid_s, NULL);
  err(EXIT_FAILURE, "execlp: gdb");
}
