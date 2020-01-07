#pragma once
#ifndef __ARCH_H__
#define __ARCH_H__


#ifndef GIT_REVISION
#define GIT_REVISION "NO-GIT"
#endif

struct task_regs;

// Architecture specific functionality. Implemented in arch/$ARCH/*
namespace arch {
void cli(void);
void sti(void);

void halt(void);

// invalidate a page mapping
void invalidate_page(unsigned long addr);

void flush_tlb(void);

unsigned long read_timestamp(void);

/**
 * the architecture must only implement init() and eoi(). The arch must
 * implement calling irq::dispatch() when an interrupt is received.
 */
namespace irq {
// init architecture specific interrupt logic. Implemented by arch
int init(void);
// End of Interrupt.
void eoi(int i);


// enable or disable interrupts
void enable(int num);
void disable(int num);
};  // namespace irq
};  // namespace arch

// core kernel irq (implemented in src/irq.cpp)
namespace irq {

using handler = void (*)(int i, struct task_regs *);

// install and remove handlers
int install(int irq, irq::handler handler, const char *name);
irq::handler uninstall(int irq);

int init(void);
static inline void eoi(int i) { arch::irq::eoi(i); }

// cause an interrupt to be handled by the kernel's interrupt dispatcher
void dispatch(int irq, struct task_regs *);

};  // namespace irq

#endif
