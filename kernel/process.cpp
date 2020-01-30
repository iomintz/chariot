#include <asm.h>
#include <cpu.h>
#include <errno.h>
#include <fs/vfs.h>
#include <map.h>
#include <mem.h>
#include <mmap_flags.h>
#include <paging.h>
#include <pctl.h>
#include <phys.h>
#include <process.h>
#include <syscalls.h>
#include <util.h>
#include <vga.h>

extern "C" void wrmsr(u32 msr, u64 val);

extern "C" void trapret(void);

/**
 * ===============================================
 * end thread functions
 * begin system calls
 */

// static long sys_invalid(u64, u64, u64, u64, u64, u64) { return 0; }

#define SYSSYM(name) sys_##name

void sys::restart() {
  // TODO: NEED TO RESTART
}

void sys::exit_task(int code) {
  printk("Exit task. code=%d\n", code);
  cpu::task()->exit(code);
  // return, letting trap() call sched::before_iret()
}

void sys::exit_proc(int code) {
  auto proc = cpu::proc().get();
  assert(proc != NULL);

  for (auto tid : proc->tasks) {
    auto *task = task::lookup(tid).get();
    if (task != NULL) {
      task->exit(code);
    }
    printk("tid=%d\n", tid);
  }

  printk("Exit proc. code=%d\n", code);
}

pid_t sys::getpid(void) { return cpu::task()->pid; }

pid_t sys::gettid(void) { return cpu::task()->tid; }

// WARNING: HACK
struct syscall {
  const char *name;
  int num;
  void *handler;
};

map<int, struct syscall> syscall_table;
// vec<struct syscall> syscall_table;

void set_syscall(const char *name, int num, void *handler) {
  // KINFO("%s -> %d\n", name, num);
  syscall_table[num] = {.name = name, .num = num, .handler = handler};
}

void syscall_init(void) {
#undef __SYSCALL
#define __SYSCALL(num, name) set_syscall(#name, num, (void *)sys::name);
#include <syscalls.inc>
}

static u64 do_syscall(long num, u64 a, u64 b, u64 c, u64 d, u64 e, u64 f) {
  if (!syscall_table.contains(num) || syscall_table[num].handler == nullptr) {
    KWARN("unknown syscall in pid %d. syscall(%d) @ rip=%p\n", cpu::proc()->pid,
          num, cpu::task()->tf->eip);
    return -1;
  }

  cpu::task()->syscall_count++;

  auto *func = (u64(*)(u64, u64, u64, u64, u64, u64))syscall_table[num].handler;

  return func(a, b, c, d, e, f);
}

extern "C" void syscall_handle(int i, struct task_regs *tf) {
  // int x = 0;
#ifdef __ARCH_x86_64__
  tf->rax =
      do_syscall(tf->rax, tf->rdi, tf->rsi, tf->rdx, tf->r10, tf->r8, tf->r9);
#endif

#ifdef __ARCH_i386__
  tf->eax = do_syscall(tf->eax, tf->ebx, tf->ecx, tf->edx, tf->esi, tf->edi,
                       0 /* TODO */);
#endif
  return;
}

extern "C" long __syscall(size_t, size_t, size_t, size_t, size_t, size_t,
                          size_t);

long ksyscall(long n, ...) {
  va_list ap;
  size_t a, b, c, d, e, f;
  va_start(ap, n);
  a = va_arg(ap, size_t);
  b = va_arg(ap, size_t);
  c = va_arg(ap, size_t);
  d = va_arg(ap, size_t);
  e = va_arg(ap, size_t);
  f = va_arg(ap, size_t);
  va_end(ap);

  return do_syscall(n, a, b, c, d, e, f);
}
