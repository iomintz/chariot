#include <cpu.h>
#include <elf/loader.h>
#include <syscall.h>



// kernel/proc.cpp
extern mm::space *alloc_user_vm(void);


int sys::execve(const char *path, const char **uargv, const char **uenvp) {
  // TODO: this isn't super smart imo, we are relying on not getting an irq
  auto *tf = curthd->trap_frame;

  if (path == NULL) return -EINVAL;
  if (uargv == NULL) return -EINVAL;

  if (!curproc->mm->validate_string(path)) return -EINVAL;

  // TODO validate the pointers
  vec<string> argv;
  vec<string> envp;
	// printk("path=%s\n", path);
  for (int i = 0; uargv[i] != NULL; i++) {
		// printk("argv[%d] = %p\n", i, uargv[i]);
    argv.push(uargv[i]);
  }

  if (uenvp != NULL) {
    for (int i = 0; uenvp[i] != NULL; i++) {
			// printk("uenvp[%d] = %p\n", i, uenvp[i]);
      envp.push(uenvp[i]);
    }
  }


  // try to load the binary
  fs::inode *exe = NULL;

  // TODO: open permissions on the binary
  if (vfs::namei(path, 0, 0, curproc->cwd, exe) != 0) {
    return -ENOENT;
  }

  off_t entry = 0;
  auto fd = make_ref<fs::file>(exe, FDIR_READ);

  // allocate a new address space
  auto *new_addr_space = new mm::space(0x1000, 0x7ff000000000, mm::pagetable::create());

  int loaded = elf::load(path, *curproc, *new_addr_space, fd, entry);
  if (loaded < 0) {
    delete new_addr_space;
    return -EINVAL;
  }


  // allocate a 1mb stack
  // TODO: this size is arbitrary.
  auto stack_size = 1024 * 1024;
  off_t stack =
      new_addr_space->mmap("[stack]", 0, stack_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, nullptr, 0);

  // kill the other threads
  for (auto tid : curproc->threads) {
    auto *thd = thread::lookup(tid);
    if (thd != NULL && thd != curthd) {
      // take the runlock on the thread so nobody else runs it
      thd->locks.run.lock();
      thread::teardown(thd);
    }
  }

  curproc->args = argv;
  curproc->env = envp;
  delete curproc->mm;
  curproc->mm = new_addr_space;

  arch::reg(REG_SP, tf) = stack + stack_size - 64;
  arch::reg(REG_PC, tf) = (unsigned long)entry;

  cpu::switch_vm(curthd);

  curthd->setup_tls();
  curthd->setup_stack(tf);

  return 0;
}
