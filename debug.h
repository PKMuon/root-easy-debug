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
#if defined(__i386__) || defined(__amd64__)
#define breakpoint()  do { __asm__("int3"); } while(0)
#elif defined(__arm__) || defined(__aarch64__)
#define breakpoint()  do { __asm__("bkpt #0"); } while(0)
#elif !defined(breakpoint)
#warning "macro `breakpoint()' unavailable"
#endif  /* breakpoint */

#define DEBUG_IGNORE_VALUE(expr)  do if((expr)) { } while(0)

static int begin_debug(bool external = false)
{
  struct Inner {
    static int ptrace_scope() {  // >=0: see ptrace(2); <0: error
      FILE *file = fopen("/proc/sys/kernel/yama/ptrace_scope", "r");
      if(file == NULL) return -1;

      char buf[64] = {0};
      DEBUG_IGNORE_VALUE(fgets(buf, sizeof buf, file));
      fclose(file);

      char *end;
      int value = strtol(buf, &end, 10);
      if(end == buf) return -1;
      return value;
    }

    static int tracer_pid() {  // >=0: tracer pid; <0: error
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

    static void wait_for_tracer() {
      sigset_t sigset;
      assign_sigset_to_wait(&sigset);
      while(tracer_pid() <= 0) {  // False signal eliminated.
        // All signals in sigset must have been blocked before tracer can initiate one.
        // So we have no chance to miss any signal from tracer.
        int sig;
        sigwait(&sigset, &sig);
      }
    }

    static int disable_ptrace_restriction() {
      int scope = Inner::ptrace_scope();
      if(scope <= 0) {  /* 0: classic ptrace permissions */
        // We assume no Yama ptrace protection here.
      } else if(scope == 1) {  /* restricted ptrace */
        // Enable non-direct-ancestor ptrace for external debuggers.
        prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
      } else {  /* admin-only attach or no attach */
        // We give up external ptrace and return an error.
        return -1;
      }
      return 0;
    }

    static void default_sigtrap_handler(int) {
      char msg[] = "FATAL: SIGTRAP received without a proper debugger attached\n";
      DEBUG_IGNORE_VALUE(write(STDERR_FILENO, msg, sizeof msg - 1));
      _exit(1);
    }

    static void assign_sigset_to_wait(sigset_t *sigset) {
      sigemptyset(sigset);
      sigaddset(sigset, SIGCONT);
    }
  };

  if(external && Inner::disable_ptrace_restriction() < 0) {
    int e = errno;
    warnx("external ptrace disabled by Yama ptrace_scope");
    signal(SIGTRAP, Inner::default_sigtrap_handler);
    errno = e;
    return -1;
  }

  // Block signals to wait.
  sigset_t sigset, sigset_old;
  Inner::assign_sigset_to_wait(&sigset);
  sigprocmask(SIG_BLOCK, /* restrict */&sigset, /* restrict */&sigset_old);

  if(external) {
    printf("\nNow run the following command in another terminal:\n\n    ");
    //printf("cgdb");
    printf("gdb");
    //printf(" --quiet");
    printf(" --eval-command '%s'", "set pagination off");  // Avoid being blocked by screen height.
    printf(" --eval-command '%s'", "signal SIGCONT");
    printf(" --eval-command '%s'", "set pagination on");
    printf(" --pid %d", getpid());
    printf("\n");

    // Restore signal mask after synchronization.
    Inner::wait_for_tracer();
    sigprocmask(SIG_SETMASK, &sigset_old, NULL);
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

  // Restore signal mask after synchronization.
  if(pid == 0) {  // child
    Inner::wait_for_tracer();
    sigprocmask(SIG_SETMASK, &sigset_old, NULL);
    return 0;
  }
  sigprocmask(SIG_SETMASK, &sigset_old, NULL);

  // Run GDB to attach to process pid.
  // DONOT use CGDB here for the following reasons:
  //   * Output of the program would interleave with CGDB
  //   * We didn't disable ptrace scope protection for the child process
  char pid_s[64];
  sprintf(pid_s, "%lld", (long long)pid);
  // [NOTE] Adapt it to "lldb", etc. on your own demand.
  execlp("gdb", "gdb",
      //"--quiet",
      "--eval-command", "set pagination off",  // Avoid being blocked by screen height.
      "--eval-command", "signal SIGCONT",
      "--eval-command", "set pagination on",
      "--pid", pid_s, NULL);

  // Handle execlp failure.
  // Killing child process cancels everything because it is always kept suspended.
  warn("execlp");
  int e = errno;
  kill(pid, SIGKILL);
  signal(SIGTRAP, Inner::default_sigtrap_handler);
  errno = e;
  return -1;
}
