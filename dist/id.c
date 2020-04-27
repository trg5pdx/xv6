#include "user.h"
#include "proc.h"

#ifdef CS333_P2
int getuid(void) {
  return myproc()->uid;
}


int getgid(void) {
  return myproc()->gid;
}

int getppid(void) {
  if(myproc->parent)
    return myproc()->parent->pid;
  else
    return myproc()->pid;
}

int setuid(uint n) {
  if(n < 0 || n > 32767)
    return 0;
  myproc()->uid = n;

  return 1;
}

int setgid(uint n) {
  if(n < 0 || n > 32767)
    return 0;
  myproc()->gid = n;

  return 1;
}

#endif // CS333_P2
