#include <chariot.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int spawn_proc(char *bin) {
  int pid = spawn();

  if (pid == -1) {
    return -1;
  }
  char *cmd[] = {bin, 0};
  int res = cmdpidv(pid, cmd[0], cmd);
  if (res != 0) {
    return -1;
  }

  return pid;
}

int main(int argc, char **argv) {
  for (int i = 0; i < 3; i++) {
    spawn_proc("/bin/test");
  }

  int i = 0;

  while (1) {
    int fd = open("/etc/passwd", O_RDONLY);
    printf("%-4d: ", i++);

    char buf[500];
    int n = read(fd, buf, 255);

    // write to stdout
    write(1, buf, n);
    syscall(SYS_close, fd);
  }

  while (1) {
    printf("hello\n");
  }
}

