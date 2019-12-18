CC = $(X86_64_ELF_TOOLCHAIN)gcc
CXX = $(X86_64_ELF_TOOLCHAIN)g++
AS = nasm
LD = $(X86_64_ELF_TOOLCHAIN)ld
GRUB = $(GRUB_PREFIX)grub-mkrescue

.PHONY: fs watch


STRUCTURE := $(shell tools/get_structure.sh)
CODEFILES := $(addsuffix /*,$(STRUCTURE))
CODEFILES := $(wildcard $(CODEFILES))


CSOURCES:=$(filter %.c,$(CODEFILES))
CPPSOURCES:=$(filter %.cpp,$(CODEFILES))

COBJECTS:=$(CSOURCES:%.c=build/%.c.o)
COBJECTS+=$(CPPSOURCES:%.cpp=build/%.cpp.o)

# ASOURCES:=$(wildcard kernel/src/*.asm)
ASOURCES:=$(filter %.asm,$(CODEFILES))
AOBJECTS:=$(ASOURCES:%.asm=build/%.asm.o)


include Makefile.common


LDFLAGS=-m elf_x86_64

KERNEL=build/vmchariot
ISO=build/kernel.iso
SYMS=build/kernel.syms
ROOTFS=build/root.img

AFLAGS=-f elf64 -w-zext-reloc


default: $(KERNEL)

build:
	@mkdir -p build


build/%.c.o: %.c
	@mkdir -p $(dir $@)
	@echo "[K] CC  " $<
	@$(CC) $(CFLAGS) -o $@ -c $<

build/%.cpp.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "[K] CXX " $<
	@$(CXX) $(CPPFLAGS) -o $@ -c $<


build/%.asm.o: %.asm
	@mkdir -p $(dir $@)
	@echo "[K] ASM " $<
	@$(AS) $(AFLAGS) -o $@ $<

$(KERNEL): $(CODEFILES) $(ASOURCES) $(COBJECTS) $(AOBJECTS)
	@echo "[K] LNK " $@
	@$(LD) $(LDFLAGS) $(AOBJECTS) $(COBJECTS) -T kernel.ld -o $@


$(SYMS): $(KERNEL)
	nm -s $< > $@

build/initrd.tar: build
	@#tar cvf $@ mnt


$(ROOTFS): $(KERNEL)
	./sync.sh

fs:
	rm -rf $(ROOTFS)
	make $(ROOTFS)

$(ISO): $(KERNEL) grub.cfg
	mkdir -p build/iso/boot/grub
	cp ./grub.cfg build/iso/boot/grub
	cp build/vmchariot build/iso/boot
	cp build/kernel.syms build/iso/boot
	$(GRUB) -o $(ISO) build/iso


klean:
	rm -f $(COBJECTS) build/initrd.tar $(AOBJECTS) $(KERNEL)

clean:
	rm -rf user/out user/build
	rm -rf build

images: $(ROOTFS)

QEMUOPTS=-hda $(ROOTFS) -smp 4 -m 2G -gdb tcp::8256

qemu: images
	qemu-system-x86_64 $(QEMUOPTS) \
		-serial stdio

qemu-nox: images
	qemu-system-x86_64 $(QEMUOPTS) -nographic



qemu-dbg: images
	qemu-system-x86_64 $(QEMUOPTS) -d cpu_reset

gdb:
	gdb $(KERNEL) -iex "target remote localhost:8256"



bochs: $(ISO)
	@bochs -f bochsrc



watch:
	while inotifywait **/*; do ./sync.sh; done
