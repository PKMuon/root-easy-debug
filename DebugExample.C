// Usage: root DebugExample.C+g
//
// Written by <lyazj@github.com>.
#include "debug.h"

void DebugExample()
{
  begin_debug();

  volatile int a = 0;
  printf("\nDirection: Trap break points and change variables in GDB.\n");
  printf("Instruction: (in GDB)\n");
  printf("  > Type `set variable a = 1` and ENTER.\n");
  printf("  > Type `c` and ENTER to continue the target program.\n");
  breakpoint();

  // Validate the value of `a`.
  if(a == 0) {
    printf("\nFAILED: a = 0 not changed.\n");
  } else if(a == 1) {
    printf("\nSUCCESS: a = 1 set successfully.\n");
    exit(EXIT_SUCCESS);
  } else {
    printf("\nFAILED: a = %d set unexpectedly.\n", a);
  }
  exit(EXIT_FAILURE);
}
