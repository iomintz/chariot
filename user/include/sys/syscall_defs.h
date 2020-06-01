#define SYS_restart                  (0x00)
#define SYS_exit_thread              (0x01)
#define SYS_exit_proc                (0x02)
#define SYS_spawn                    (0x03)
#define SYS_despawn                  (0x04)
#define SYS_startpidve               (0x05)
#define SYS_pctl                     (0x06)
#define SYS_waitpid                  (0x07)
#define SYS_fork                     (0x08)
#define SYS_open                     (0x10)
#define SYS_close                    (0x11)
#define SYS_lseek                    (0x12)
#define SYS_read                     (0x13)
#define SYS_write                    (0x14)
#define SYS_stat                     (0x16)
#define SYS_fstat                    (0x17)
#define SYS_lstat                    (0x18)
#define SYS_dup                      (0x19)
#define SYS_dup2                     (0x1a)
#define SYS_chdir                    (0x1b)
#define SYS_getcwd                   (0x1c)
#define SYS_chroot                   (0x1d)
#define SYS_ioctl                    (0x1f)
#define SYS_yield                    (0x20)
#define SYS_getpid                   (0x21)
#define SYS_gettid                   (0x22)
#define SYS_getuid                   (0x26)
#define SYS_geteuid                  (0x27)
#define SYS_getgid                   (0x28)
#define SYS_getegid                  (0x29)
#define SYS_mmap                     (0x30)
#define SYS_munmap                   (0x31)
#define SYS_mrename                  (0x32)
#define SYS_mgetname                 (0x33)
#define SYS_mregions                 (0x34)
#define SYS_dirent                   (0x40)
#define SYS_localtime                (0x50)
#define SYS_gettime_microsecond      (0x51)
#define SYS_usleep                   (0x52)
#define SYS_socket                   (0x60)
#define SYS_sendto                   (0x61)
#define SYS_bind                     (0x62)
#define SYS_accept                   (0x63)
#define SYS_connect                  (0x64)
#define SYS_signal_init              (0x70)
#define SYS_signal                   (0x71)
#define SYS_sigreturn                (0x72)
#define SYS_sigprocmask              (0x73)
#define SYS_kshell                   (0xF0)
