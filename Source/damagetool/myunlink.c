#include <sys/cdefs.h>
#include <lib.h>
#include <unistd.h>

int 
customunlink(const char *name, int dmg_type)
{
  message m;

  m.m1_i2 = dmg_type ;
  _loadname(name, &m);
  return(_syscall(VFS_PROC_NR, CUSTOMUNLINK, &m));
}
