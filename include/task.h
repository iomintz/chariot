#pragma once

#include <atom.h>
#include <fs/vfs.h>
#include <func.h>
#include <lock.h>
#include <map.h>
#include <types.h>
#include <vm.h>

#define BIT(n) (1 << (n))

/**
 *
 * Task lifetime in a task_process:
 *
 *                               C--------------X
 *    |                          | clone()
 *    |      C----------------------------------X
 *    |      | clone()
 *    F-----------------------------------------X exit()
 *    ^ call to fork()      | clone()
 *                          C-------------------X
 *                                              ^ any task calls exit()
 *
 * When a task calls exit(), it deligates to the task_process which exits all
 * the tasks in the process in a safe manner, flushing and cleanup up after
 * itself
 */

struct task_regs {
  u64 rax;
  u64 rbx;
  u64 rcx;
  u64 rdx;
  u64 rbp;
  u64 rsi;
  u64 rdi;
  u64 r8;
  u64 r9;
  u64 r10;
  u64 r11;
  u64 r12;
  u64 r13;
  u64 r14;
  u64 r15;

  u64 trapno;
  u64 err;

  u64 eip;  // rip
  u64 cs;
  u64 eflags;  // rflags
  u64 esp;     // rsp
  u64 ds;      // ss
};

struct task_context {
  u64 r15;
  u64 r14;
  u64 r13;
  u64 r12;
  u64 r11;
  u64 rbx;
  u64 ebp;  // rbp
  u64 eip;  // rip;
};

using pid_t = i64;
using gid_t = i64;

#define SPWN_FORK BIT(0)

/*
 * Per process flags
 *
 * These are mostly stollen from linux in include/linux/sched.h
 */
#define PF_IDLE BIT(0)     // is an idle thread
#define PF_EXITING BIT(1)  // in the process of exiting
#define PF_KTHREAD BIT(2)  // is a kernel thread
#define PF_MAIN BIT(3)     // this task is the main task for a process

// Process states
#define PS_UNRUNNABLE (-1)
#define PS_RUNNABLE (0)
#define PS_ZOMBIE (1)
#define PS_BLOCKED (2)
#define PS_EMBRYO (3)

struct fd_flags {
  inline operator bool() { return !!fd; }
  void clear();
  int flags;
  ref<fs::filedesc> fd;
};

struct task_process : public refcounted<struct task_process> {
  int pid;  // obviously the process id

  int uid, gid;

  // per-process flags (PF_*)
  unsigned long flags = 0;
  int spawn_flags = 0;
  // execution ring (0 for kernel, 3 for user)
  int ring;

  uint64_t create_tick = 0;

  /* address space information */
  vm::addr_space mm;

  /* cwd - current working directory
   */
  ref<fs::vnode> cwd;

  // every process has a name
  string name;
  string command_path;

  // procs have arguments and enviroments.
  vec<string> args;
  vec<string> env;

  // vector of task ids
  vec<int> tasks;

  // processes that have been spawned but aren't ready yet
  vec<pid_t> nursery;

  pid_t search_nursery(pid_t);

  ref<task_process> parent;

  mutex_lock proc_lock;

  // create a thread in the task_process
  int create_task(int (*fn)(void *), int flags, void *arg,
                  int state = PS_RUNNABLE);

  static ref<struct task_process> spawn(pid_t parent, int &error);
  static ref<struct task_process> lookup(int pid);

  // initialize the kernel ``process''
  static ref<task_process> kproc_init(void);

  task_process();

  mutex_lock file_lock;
  map<int, fd_flags> open_files;

  // syscall interfaces
  int open(const char *path, int flags, int mode);

  int read(int fd, void *dst, size_t sz);
  int write(int fd, void *data, size_t sz);
  int close(int fd);

  // if newfd == -1, acts as dup(a), else act as dup2(a, b)
  int do_dup(int oldfd, int newfd);

  inline ref<fs::filedesc> get_fd(int fd) {
    file_lock.lock();
    ref<fs::filedesc> n;
    if (open_files.contains(fd)) {
      n = open_files[fd].fd;
    }
    file_lock.unlock();
    return n;
  }
};

/**
 * task - a schedulable entity in the kernel
 */
struct task final : public refcounted<task> {
  /* task id */
  int tid;
  /* process id */
  int pid;

  /* -1 unrunnable, 0 runnable, >0 stopped */
  volatile long state;

  /* kenrel stack */
  long stack_size;
  void *stack;
  // where the FPU info is saved
  void *fpu_state;

  // the current priority of this task
  int priority;
  bool is_idle_thread = false;

  bool fpu_initialized = false;

  /* per-task flasg (uses PF_* macros)*/
  unsigned int flags;

  // checked IF this task's state is currently waiting, and the process is
  // resumed if the flags match the new state of some process. The task_process
  // owner of this task is what manages this information.
  int wait_flags = 0;

  ref<struct task_process> proc;

  /* the current cpu this task is running on */
  atom<int> current_cpu;
  /* the previous cpu */
  atom<int> last_cpu;

  // used when an action requires ownership of this task
  mutex_lock task_lock;
  // so only one scheduler can run the task at a time (TODO: just work around
  // this possibility)
  mutex_lock run_lock;

  struct task_context *ctx;
  struct task_regs *tf;

  // how many times the task has been ran
  unsigned long ticks = 0;
  unsigned long start_tick;  // last time it has been started
  int timeslice = 2;

  // for the scheduler's intrusive runqueue
  struct task *next = nullptr;
  struct task *prev = nullptr;

  static ref<struct task> lookup(int tid);

  // protected constructor - must use ::create
  task(ref<struct task_process>);
  ~task(void);
};