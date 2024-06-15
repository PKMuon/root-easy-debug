// See DebugExample.C for usage.
//
// Written by <lyazj@github.com>.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <errno.h>
#include <err.h>

// [NOTE] __builtin_trap() can stop code emitting afterwards thus is not used here.
#define breakpoint()  ({ __asm("int3"); })  // [XXX] This assumes x86_64. Change it on your own need.

static int begin_debug(bool external = false)
{
  struct Inner {
    static int ptrace_scope() {
      FILE *file = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
      if(file == NULL) return -1;

      char buf[64] = {0};
      fgets(buf, sizeof buf, file);
      fclose(file);

      char *end;
      int value = strtol(buf, &end, 10);
      if(end == buf) return -1;
      return value;
    }

    static int tracer_pid() {
      pid_t pid = -1;

      FILE *file = fopen("/proc/self/status", "r");
      if(file == NULL) return pid;

      char buf[1024];
      while(fgets(buf, sizeof buf, file)) {
        if(strncmp(buf, "TracerPid:", 10) == 0) {
          char *end;
          long value = strtol(buf + 10, &end, 10);
          if(end != buf + 10) pid = value;
          break;
        }
      }

      fclose(file);
      return pid;
    }

    static void sigcont_handler(int) { /* do nothing */ }

    static void wait_for_tracer() {
      while(tracer_pid() <= 0) {  // False signal eliminated.
        sighandler_t old_sigcont_handler = signal(SIGCONT, sigcont_handler);
        pause();
        signal(SIGCONT, old_sigcont_handler);
      }
    }

    static void default_sigtrap_handler(int) {
      char msg[] = "FATAL: SIGTRAP received without a proper debugger attached\n";
      write(STDERR_FILENO, msg, sizeof msg - 1);
      _exit(1);
    }
  };

  if(external) {
    if(Inner::ptrace_scope() == 1) {
      // Enable non-direct-ancestor ptrace for external debuggers.
      prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    }

    printf("\nNow run the following command in another terminal:\n");
    printf("\n");
    //printf("    cgdb");
    printf("    gdb");
    //printf(" --quiet");
    printf(" --eval-command '%s'", "set pagination off");  // Avoid being blocked by screen height.
    printf(" --eval-command '%s'", "signal SIGCONT");
    printf(" --eval-command '%s'", "set pagination on");
    printf(" --pid %d", getpid());
    printf("\n");
    Inner::wait_for_tracer();
    return 0;
  }

  pid_t pid = fork();
  if(pid < 0) {
    int e = errno;
    warn("fork");
    signal(SIGTRAP, Inner::default_sigtrap_handler);
    errno = e;
    return -1;
  }

  if(pid == 0) {  // child
    Inner::wait_for_tracer();
    return 0;
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

  // Handle execlp failure.
  warn("execlp");
  int e = errno;
  kill(pid, SIGKILL);
  signal(SIGTRAP, Inner::default_sigtrap_handler);
  errno = e;
  return -1;
}
