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
  int uptimeBefore, uptimeAfter, uptimeTotal, totalS, totalMS, pid;

  uptimeBefore = uptime();

  pid = fork();
  if(pid == 0) {
    exec(argv[1], argv + 1);
    exit();
  }
  else
    wait();

  uptimeAfter = uptime();

  uptimeTotal = uptimeAfter - uptimeBefore;

  totalMS = uptimeTotal % 1000;

  totalS = (uptimeTotal - totalMS) / 1000;

  printf(1, "%s ran for %d.%d seconds\n", argv[1], totalS, totalMS);


  exit();
}

#endif // CS333_P2
