#include <elf/loader.h>
#include <errno.h>
#include <fs/magicfd.h>
#include <printk.h>

#define round_up(x, y) (((x) + (y)-1) & ~((y)-1))
#define round_down(x, y) ((x) & ~((y)-1))

static const char elf_header[] = {0x7f, 0x45, 0x4c, 0x46};

bool elf::validate(fs::file &fd) {
  Elf64_Ehdr ehdr;
  return elf::validate(fd, ehdr);
}

bool elf::validate(fs::file &fd, Elf64_Ehdr &ehdr) {
  fd.seek(0, SEEK_SET);
  int header_read = fd.read(&ehdr, sizeof(ehdr));

  if (header_read != sizeof(ehdr)) {
    return false;
  }

  for (int i = 0; i < 4; i++) {
    if (ehdr.e_ident[i] != elf_header[i]) {
      return false;
    }
  }

  switch (ehdr.e_machine) {
    case EM_X86_64:
    case EM_ARM:
    case EM_AARCH64:
      break;
    default:
      return false;
      break;
  }

  return true;
}


int elf::each_symbol(fs::file &fd, func<bool(const char *sym, off_t)> cb) {
  Elf64_Ehdr ehdr;


  if (!elf::validate(fd, ehdr)) {
    printk("[ELF LOADER] elf not valid\n");
    return -1;
  }


  if (ehdr.e_shstrndx == SHN_UNDEF) {
    return -ENOENT;
  }


  Elf64_Shdr *sec_hdrs = new Elf64_Shdr[ehdr.e_shnum];

  // seek to and read the headers
  fd.seek(ehdr.e_shoff, SEEK_SET);
  auto sec_expected = ehdr.e_shnum * sizeof(*sec_hdrs);
  auto sec_read = fd.read(sec_hdrs, sec_expected);

  if (sec_read != sec_expected) {
    delete[] sec_hdrs;
    printk("sec_read != sec_expected\n");
    return -1;
  }




  Elf64_Sym *symtab = NULL;
  int symcount = 0;

  char *strtab = NULL;
  size_t strtab_size = 0;

  for (int i = 0; i < ehdr.e_shnum; i++) {
    auto &sh = sec_hdrs[i];
    if (sh.sh_type == SHT_SYMTAB) {
      symtab = (Elf64_Sym *)kmalloc(sh.sh_size);

      symcount = sh.sh_size / sizeof(*symtab);

      fd.seek(sh.sh_offset, SEEK_SET);
      fd.read(symtab, sh.sh_size);
      continue;
    }

    if (sh.sh_type == SHT_STRTAB && strtab == NULL) {
      strtab = (char *)kmalloc(sh.sh_size);
      fd.seek(sh.sh_offset, SEEK_SET);
      fd.read(strtab, sh.sh_size);
      strtab_size = sh.sh_size;
    }
  }



  delete[] sec_hdrs;


  int err = 0;
  if (symtab != NULL && symtab != NULL) {
    for (int i = 0; i < symcount; i++) {
      auto &sym = symtab[i];
      if (sym.st_name > strtab_size - 2) {
        err = -EINVAL;
        break;
      }
      if (!cb(strtab + sym.st_name, sym.st_value)) break;
    }

    kfree(symtab);
  }

  if (symtab != NULL) kfree(symtab);
  if (strtab != NULL) kfree(strtab);

  return err;
}

int elf::load(const char *path, struct process &p, mm::space &mm, ref<fs::file> fd, u64 &entry) {
  Elf64_Ehdr ehdr;

  off_t off = 0;


  p.file_lock.lock();
  p.open_files[MAGICFD_EXEC] = fd;
  p.executable = fd;
	p.tls_info.exists = false;
  p.file_lock.unlock();

  if (!elf::validate(*fd, ehdr)) {
    printk("[ELF LOADER] elf not valid\n");
    return -1;
  }

  // the binary is valid, so lets read the headers!
  entry = off + ehdr.e_entry;

  auto handle_bss = [&](Elf64_Phdr &ph) -> void {
    if (ph.p_memsz > ph.p_filesz) {
      auto addr = ph.p_vaddr;
      // possibly map a new bss region, anonymously in?
      auto file_end = addr + (ph.p_memsz - ph.p_filesz);
      auto file_page_end = round_up(file_end, PGSIZE);

      auto page_end = round_up(addr + ph.p_memsz, PGSIZE);

      if (page_end > file_page_end) {
        printk("need a new page!\n");
      }
    }
  };

  // read program headers
  Elf64_Phdr *phdr = new Elf64_Phdr[ehdr.e_phnum];
  fd->seek(ehdr.e_phoff, SEEK_SET);
  auto hdrs_size = ehdr.e_phnum * ehdr.e_phentsize;
  auto hdrs_read = fd->read(phdr, hdrs_size);

  if (hdrs_read != hdrs_size) {
    delete[] phdr;
    printk("hdrs_read != hdrs_size\n");
    return -1;
  }

  size_t i = 0;

  // skip to the first loadable header
  while (i < ehdr.e_phnum && phdr[i].p_type != PT_LOAD) i++;

  if (i == ehdr.e_phnum) {
    delete[] phdr;
    printk("i == ehdr.e_phnum\n");
    return -1;
  }

  // Elf places the PT_LOAD segments in ascending order of vaddresses.
  // So we find the last one to calculate the whole address span of the image.
  auto *first_load = &phdr[i];
  auto *last_load = &phdr[ehdr.e_phnum - 1];

  // walk the last_load back
  while (last_load > first_load && last_load->p_type != PT_LOAD) last_load--;

  // Total memory size of phdr between first and last PT_LOAD.
  // size_t span = last_load->p_vaddr + last_load->p_memsz -
  // first_load->p_vaddr;

  for (int i = 0; i < ehdr.e_phnum; i++) {
    auto &sec = phdr[i];


    if (sec.p_type == PT_TLS) {
      printk("Found a TLS template!\n");
			p.tls_info.exists = true;
			p.tls_info.fileoff = sec.p_offset;
			p.tls_info.fsize = sec.p_filesz;
			p.tls_info.memsz = sec.p_memsz;
    }

    if (sec.p_type == PT_LOAD) {
      auto start = round_down(sec.p_vaddr, 1);



      auto prot = 0L;
      if (sec.p_flags & PF_X) prot |= PROT_EXEC;
      if (sec.p_flags & PF_W) prot |= PROT_WRITE;
      if (sec.p_flags & PF_R) prot |= PROT_READ;

      if (sec.p_filesz == 0) {
        mm.mmap(path, off + start, sec.p_memsz, prot, MAP_ANON | MAP_PRIVATE, nullptr, 0);
        // printk("    is .bss\n");
      } else {
        mm.mmap(path, off + start, sec.p_memsz, prot, MAP_PRIVATE, fd, sec.p_offset);
        handle_bss(sec);
      }
    }
  }

  delete[] phdr;

  return 0;
}
