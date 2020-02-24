#include <errno.h>
#include <ftw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

long nfiles = 0;
long ndirs = 0;
int use_colors = 1;
int level = -1;
unsigned long total_size = 0;
int quiet = 0;

#define C_RED "\x1b[31m"
#define C_GREEN "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_BLUE "\x1b[34m"
#define C_MAGENTA "\x1b[35m"
#define C_CYAN "\x1b[36m"

#define C_RESET "\x1b[0m"
#define C_GRAY "\x1b[90m"

static void set_color(char *c) {
  if (use_colors) {
    printf("%s", c);
  }
}

int print_filename(const char *name, int mode) {
  char end = '\0';
  char *name_color = C_RESET;

  if (S_ISDIR(mode)) {
    end = '/';
    name_color = C_MAGENTA;
  } else {
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
      end = '*';
      name_color = C_GREEN;
    }
  }
  set_color(name_color);
  puts(name);
  set_color(C_RESET);
  printf("%c", end);

  return 1 + strlen(name);
}

static int display_info(const char *fpath, const struct stat *sb, int tflag,
                        struct FTW *ftwbuf) {
  const char *name = fpath + ftwbuf->base;

  if (tflag & FTW_D) {
    ndirs++;
  } else {
    nfiles++;
    total_size += sb->st_size;
  }

  if (!quiet) {
    for (int i = 0; i < ftwbuf->level; i++) puts("│   ");
    puts("├── ");

    print_filename(name, sb->st_mode);
    puts("\n");
  }

  return 0;  // To tell nftw() to continue
}

int print_filesize(long s) {
  if (s >= 1 << 20) {
    size_t t = s / (1 << 20);
    return printf("%d.%1d mb", (int)t,
                  (int)(s - t * (1 << 20)) / ((1 << 20) / 10));
  } else if (s >= 1 << 10) {
    size_t t = s / (1 << 10);
    return printf("%d.%1d kb", (int)t,
                  (int)(s - t * (1 << 10)) / ((1 << 10) / 10));
  } else {
    return printf("%d bytes", (int)s);
  }
}

int main(int argc, char *argv[]) {
  int ftw_flags = 0;

  char ch;

  const char *flags = "L:q";
  while ((ch = getopt(argc, argv, flags)) != -1) {
    switch (ch) {
      case 'L':
        level = atoi(optarg);
        break;

      case 'q':
        quiet = 1;
        break;

      case '?':
        puts("optarg: invalid option\n");
        return -1;
    }
  }
  argc -= optind;
  argv += optind;

  const char *path = ".";

  if (argc >= 1) {
    path = argv[0];
  }
  if (nftw(path, display_info, 32, ftw_flags) < 0) {
    exit(EXIT_FAILURE);
  }

  puts("\n");
  printf("%ld directories, %ld files. \n", ndirs, nfiles);
  print_filesize(total_size);
  puts("\n");
  exit(EXIT_SUCCESS);
  return 0;
}
