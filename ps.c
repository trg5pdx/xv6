#ifdef CS333_P2
#include "types.h"
#include "user.h"
#include "x86.h"
#include "mmu.h"
#include "param.h"
#include "proc.h"
#include "uproc.h"


int
main(int argc, char *argv[])
{
  struct uproc * up = malloc(PROCSIZE * sizeof(struct uproc));

  if(getprocs(PROCSIZE, up) < 0) {
    printf(2, "Error\n");
    exit();
  }

  exit();
}

#endif // CS333_P2
