#include <cpu.h>
#include <mm.h>
#include <mmap_flags.h>
#include <syscall.h>

void *sys::mmap(void *addr, long length, int prot, int flags, int fd,
                long offset) {
  auto proc = cpu::proc();
  off_t va;
  ref<fs::file> f = nullptr;

  // printk("mmap offset 0x%08x (fd:%d)\n", offset, fd);
  if ((offset & 0xFFF) != 0) {
    // printk("invalid!\n");
    return MAP_FAILED;
  }

  if ((flags & MAP_ANON) == 0) {
    f = proc->get_fd(fd);
    if (!f) return MAP_FAILED;

    // you can only map FILE, BLK, and CHAR devices
    switch (f->ino->type) {
      case T_FILE:
      case T_BLK:
      case T_CHAR:
        break;

      default:
        return MAP_FAILED;
    }
  }

  va = proc->mm->mmap((off_t)addr, length, prot, flags, f, offset);

  return (void *)va;
}

int sys::munmap(void *addr, size_t length) {
  auto proc = cpu::proc();
  if (!proc) return -1;

  return proc->mm->unmap((off_t)addr, length);
}

int sys::mrename(void *addr, char *name) {
  auto proc = cpu::proc();

  if (!proc->mm->validate_string(name)) return -1;

  string sname = name;
  for (auto &c : sname) {
    if (c < ' ') c = ' ';
    if (c > '~') c = ' ';
  }

  auto region = proc->mm->lookup((u64)addr & ~0xFFF);

  if (region == NULL) return -ENOENT;

  region->name = move(sname);
  return 0;
}



int sys::mgetname(void *addr, char *name, size_t bufsz) {
  auto proc = cpu::proc();

  if (!proc->mm->validate_pointer(name, bufsz, VALIDATE_WRITE)) return -1;

  auto region = proc->mm->lookup((u64)addr & ~0xFFF);
  if (region == NULL) return -ENOENT;
  auto len = region->name.len();

  if (len > bufsz - 1) len = bufsz - 1;

  memcpy(name, region->name.get(), len);

  return 0;
}



// returns the number of regions populated (or failure)
int sys::mregions(struct mmap_region *dst, int nregions) {
  auto &mm = *curproc->mm;

  int regions = mm.regions.size();

  // if passed null, return the number of regions
  if (dst == 0) {
    scoped_lock l(mm.lock);
    return regions;
  }

  // validate the pointer
  if (!mm.validate_pointer(dst, nregions * sizeof(*dst), VALIDATE_WRITE)) {
    return -EINVAL;
  }

  // how many regions do we need to populate
  int want = min(nregions, regions);

  // in order to hold the mm lock, we can't page fault, so we need to malloc
  // our own structs then copy, release the lock, then copy them into userspace
  auto *tmp = new mmap_region[want];

  mm.lock.lock();
  for (int i = 0; i < want; i++) {
		auto &region = mm.regions[i];
		tmp[i].id = 0;
		tmp[i].flags = region->flags;
		tmp[i].prot = region->prot;
		tmp[i].off = region->va;
		tmp[i].len = region->len;

		int nl = min(63, region->name.len());
		memcpy(tmp[i].name, region->name.get(), nl + 1);
		// tmp[i].name
  }
  mm.lock.unlock();


  for (int i = 0; i < want; i++) {
    dst[i] = tmp[i];
  }

  delete[] tmp;

  return want;
}
