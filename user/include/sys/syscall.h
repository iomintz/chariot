#pragma once


// invoke a system call
long syscall(long number, ...);



#define SYS_restart 0
#define SYS_exit 1
#define SYS_open 2
#define SYS_close 3
#define SYS_lseek 4
#define SYS_read 5
#define SYS_write 6
#define SYS_yield 7

// process creation
#define SYS_spawn 8
#define SYS_impersonate 9
#define SYS_cmdpidve 10

#define SYS_mmap 60
#define SYS_munmap 61

