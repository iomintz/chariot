#include <asm.h>
#include <cpu.h>
#include <lock.h>
#include <map.h>
#include <sched.h>
#include <single_list.h>
#include <syscall.h>
#include <time.h>
#include <wait.h>
#include "../arch/x86/fpu.h"


#define SIG_ERR ((void (*)(int)) - 1)
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)

// #define SCHED_DEBUG
//

#define TIMESLICE_MIN 4

#ifdef SCHED_DEBUG
#define INFO(fmt, args...) printk("[SCHED] " fmt, ##args)
#else
#define INFO(fmt, args...)
#endif

extern "C" void swtch(struct thread_context **, struct thread_context *);

static bool s_enabled = true;

// every mlfq entry has a this structure
// The scheduler is defined simply in OSTEP.
//   1. if Priority(A) > Priority(B), A runs
//   2. if Priority(A) == Priority(B), A and B run in RR
//   3. when a job enters the system, it has a high priority to maximize
//      responsiveness.
//   4a. If a job uses up an entire time slice while running, its priority is
//       reduced, (only moves down one queue)
//   4b. If a job gives up the CPU before
//       the timeslice is over, it stays at the same priority level.
//
struct mlfq_entry {
  // a simple round robin queue of tasks
  struct thread *task_queue;
  // so we can add to the end of the queue
  struct thread *last_task;
  int priority;

  long ntasks = 0;
  long timeslice = 0;

  spinlock queue_lock;
};

static struct mlfq_entry mlfq[SCHED_MLFQ_DEPTH];
static spinlock mlfq_lock;

bool sched::init(void) {
  // initialize the mlfq
  for (int i = 0; i < SCHED_MLFQ_DEPTH; i++) {
    auto &Q = mlfq[i];
    Q.task_queue = NULL;
    Q.last_task = NULL;
    Q.priority = i;
    Q.ntasks = 0;
    Q.timeslice = 1;
  }

  return true;
}

static struct thread *get_next_thread(void) {
  // the queue is a singly linked list, so we need to
  // keep track of the last task in the queue so we can
  // remove the one we want to run from it
  struct thread *nt = nullptr;


  for (int i = SCHED_MLFQ_DEPTH - 1; i >= 0; i--) {
    auto &Q = mlfq[i];

    Q.queue_lock.lock();

    for (auto *t = Q.task_queue; t != NULL; t = t->sched.next) {
      if (t->state == PS_BLOCKED) {
        if (t->sig.pending) {
          // printk("[tid %d] %08x %08x\n", t->tid, t->sig.pending,
          // t->sig.mask);
          if (t->sig.pending & t->sig.mask) {
            // printk("tid %d has pending signals\n", t->tid);
          }
        }

        // poll the thread's blocker if it exists
        if (t->blocker != NULL) {
          if (t->blocker->should_unblock(*t, time::now_us())) {
            t->state = PS_RUNNABLE;
          }
        }
      }

      if (t->state == PS_RUNNABLE) {
        nt = t;

        // remove from the queue
        if (t->sched.prev != NULL) t->sched.prev->sched.next = t->sched.next;
        if (t->sched.next != NULL) t->sched.next->sched.prev = t->sched.prev;
        if (Q.task_queue == t) Q.task_queue = t->sched.next;
        if (Q.last_task == t) Q.last_task = t->sched.prev;

        Q.ntasks--;
        t->sched.prev = NULL;
        t->sched.next = NULL;

        break;
      }
    }

    Q.queue_lock.unlock();

    if (nt != nullptr) {
      break;
    }
  }

  return nt;
}


static auto pick_next_thread(void) {
  if (cpu::current().next_thread == NULL) {
    cpu::current().next_thread = get_next_thread();
  }
  return cpu::current().next_thread;
}


// add a task to a mlfq entry based on tsk->priority
int sched::add_task(struct thread *tsk) {
  // clamp the priority to the two bounds
  if (tsk->sched.priority > PRIORITY_HIGH) tsk->sched.priority = PRIORITY_HIGH;
  if (tsk->sched.priority < PRIORITY_IDLE) tsk->sched.priority = PRIORITY_IDLE;

  auto &Q = mlfq[tsk->sched.priority];

  // the task inherits the timeslice from the queue
  tsk->sched.timeslice = Q.timeslice;

  /* This critical section must be both locked and behind a cli()
   * because processes share this function with the scheduler. This means that
   * if a process were to be prempted within this section, the system would
   * deadlock as the scheduler would also try to grab the Q.queue_lock as well.
   * Because the scheduler cannot contend locks with threads, this is obviously
   * bad. To avoid this, we just make it so the thread cannot be interrupted in
   * this spot. (The only thing a thread could contend with is another CPU
   * core's scheduler, which is safe as the critical section is enormously
   * simple.
   */
  if (cpu::in_thread()) arch::cli();

  // only lock this queue.
  Q.queue_lock.lock();

  if (Q.task_queue == nullptr) {
    // this is the only thing in the queue
    Q.task_queue = tsk;
    Q.last_task = tsk;

    tsk->sched.next = NULL;
    tsk->sched.prev = NULL;
  } else {
    // insert at the end of the list
    Q.last_task->sched.next = tsk;
    tsk->sched.next = NULL;
    tsk->sched.prev = Q.last_task;
    // the new task is the end
    Q.last_task = tsk;
  }

  Q.ntasks++;

  Q.queue_lock.unlock();
  if (cpu::in_thread()) arch::sti();

  return 0;
}

int sched::remove_task(struct thread *t) {
  auto &Q = mlfq[t->sched.priority];

  if (cpu::in_thread()) arch::cli();
  // only lock this queue.
  Q.queue_lock.lock();

  if (t->sched.next) t->sched.next->sched.prev = t->sched.prev;
  if (t->sched.prev) t->sched.prev->sched.next = t->sched.next;
  if (Q.last_task == t) Q.last_task = t->sched.prev;
  if (Q.task_queue == t) Q.task_queue = t->sched.next;

  Q.ntasks--;
  Q.queue_lock.unlock();
  if (cpu::in_thread()) arch::sti();
  return 0;
}

static void switch_into(struct thread &thd) {
  thd.locks.run.lock();
  cpu::current().current_thread = &thd;
  thd.state = PS_UNRUNNABLE;
  arch::restore_fpu(thd);

  // update the statistics of the thread
  thd.stats.run_count++;
  thd.sched.start_tick = cpu::get_ticks();
  thd.stats.current_cpu = cpu::current().cpunum;

  // load up the thread's address space
  cpu::switch_vm(&thd);

  thd.stats.last_start_cycle = arch::read_timestamp();

  // Switch into the thread!
  swtch(&cpu::current().sched_ctx, thd.kern_context);

  arch::save_fpu(thd);

  // Update the stats afterwards
  cpu::current().current_thread = nullptr;
  thd.stats.last_cpu = thd.stats.current_cpu;
  thd.stats.current_cpu = -1;

  thd.locks.run.unlock();
}

void sched::do_yield(int st) {
  arch::cli();

  auto &thd = *curthd;

  thd.stats.cycles += arch::read_timestamp() - thd.stats.last_start_cycle;

  // thd.sched.priority = PRIORITY_HIGH;
  if (cpu::get_ticks() - thd.sched.start_tick >= thd.sched.timeslice) {
    // uh oh, we used up the timeslice, drop the priority!
    thd.sched.priority--;
  }

  // thd.sched.priority = PRIORITY_HIGH;

  thd.state = st;
  thd.stats.last_cpu = thd.stats.current_cpu;
  thd.stats.current_cpu = -1;
  swtch(&thd.kern_context, cpu::current().sched_ctx);

  arch::sti();
}

// helpful functions wrapping different resulting task states
void sched::block() { sched::do_yield(PS_BLOCKED); }

void sched::yield() {
  // when you yield, you give up the CPU by ''using the rest of your
  // timeslice''
  // TODO: do this another way
  // auto tsk = cpu::task().get();
  // tsk->start_tick -= tsk->timeslice;
  do_yield(PS_RUNNABLE);
}
void sched::exit() { do_yield(PS_ZOMBIE); }

void sched::dumb_sleepticks(unsigned long t) {
  auto now = cpu::get_ticks();

  while (cpu::get_ticks() * 10 < now + t) {
    sched::yield();
  }
}


void boost(void) {
  // re-calculated later using ''math''
  int boost_interval = 10;
  u64 last_boost = 0;
  auto ticks = cpu::get_ticks();

  // every S ticks or so, boost the processes at the bottom of the queue
  // into the top
  if (ticks - last_boost > boost_interval) {
    last_boost = ticks;
    int nmoved = 0;

    auto &HI = mlfq[PRIORITY_HIGH];

    HI.queue_lock.lock();
    for (int i = 0; i < PRIORITY_HIGH; i++) {
      auto &Q = mlfq[i];

      Q.queue_lock.lock();

      auto loq = Q.task_queue;

      if (loq != NULL) {
        for (auto *c = loq; c != NULL; c = c->sched.next) {
          if (!c->kern_idle) c->sched.priority = PRIORITY_HIGH;
        }

        // take the entire queue and add it to the end of the HIGH queue
        if (HI.task_queue != NULL) {
          assert(HI.last_task != NULL);
          HI.last_task->sched.next = loq;
          loq->sched.prev = HI.last_task;

          // inherit the last task from the old Q
          HI.last_task = Q.last_task;
        } else {
          assert(HI.ntasks == 0);
          HI.task_queue = Q.task_queue;
          HI.last_task = Q.last_task;
        }

        HI.ntasks += Q.ntasks;
        nmoved += Q.ntasks;

        // zero out this queue
        Q.task_queue = Q.last_task = NULL;
        Q.ntasks = 0;
      }

      Q.queue_lock.unlock();
    }

    HI.queue_lock.unlock();
  }
}

void sched::run() {
  cpu::current().in_sched = true;
  for (;;) {
    struct thread *thd = pick_next_thread();
    cpu::current().next_thread = NULL;

    if (thd == nullptr) {
      // idle loop when there isn't a task
      cpu::current().kstat.iticks++;
      arch::sti();
      asm("hlt");
      arch::cli();
      continue;
    }

    s_enabled = true;

    switch_into(*thd);

    sched::add_task(thd);
    boost();
  }
  panic("scheduler should not have gotten back here\n");
}

bool sched::enabled() { return s_enabled; }

void sched::handle_tick(u64 ticks) {
  if (!enabled() || !cpu::in_thread()) return;

  // grab the current thread
  auto thd = cpu::thread();

  if (thd->proc.ring == RING_KERN) {
    cpu::current().kstat.kticks++;
  } else {
    cpu::current().kstat.uticks++;
  }
  thd->sched.ticks++;

  auto has_run = ticks - thd->sched.start_tick;

  // yield?
  if (has_run >= thd->sched.timeslice) {
#if 1
    // pick a thread to go into next. If there is nothing to run,
    // don't yield to the scheduler. This improves some stuff's latencies
    if (pick_next_thread() != NULL) {
      sched::yield();
    }
#else
    sched::yield();
#endif
  }
}



/* the default "waiter" type */
class threadwaiter : public waiter {
 public:
  inline threadwaiter(waitqueue &wq, struct thread *td) : waiter(wq), thd(td) { thd->waiter = this; }

  virtual ~threadwaiter(void) { thd->waiter = nullptr; }

  virtual bool notify(int flags) override {
    // if (flags & NOTIFY_RUDE) printk("rude!\n");
    thd->awaken(flags);
    // I absorb this!
    return true;
  }

  virtual void start(void) override { sched::do_yield(PS_BLOCKED); }

  struct thread *thd = NULL;
};


void waiter::interrupt(void) { wq.interrupt(this); }

void waitqueue::interrupt(struct waiter *w) {
  scoped_lock l(lock);
  if (w->flags & WAIT_NOINT) return;
}


bool waitqueue::wait(u32 on, ref<waiter> wt) { return do_wait(on, 0, wt); }

void waitqueue::wait_noint(u32 on, ref<waiter> wt) { do_wait(on, WAIT_NOINT, wt); }

bool waitqueue::do_wait(u32 on, int flags, ref<waiter> waiter) {
  if (!waiter) {
    waiter = make_ref<threadwaiter>(*this, curthd);
  }
  lock.lock();

  if (navail > 0) {
    navail--;
    lock.unlock();
    return true;
  }

  assert(navail == 0);

  waiter->flags = flags;

  waiter->next = NULL;
  waiter->prev = NULL;

  curthd->waiter = waiter.get();

  if (!back) {
    assert(!front);
    back = front = waiter;
  } else {
    back->next = waiter;
    waiter->prev = back;
    back = waiter;
  }

  lock.unlock();

  waiter->start();

  // TODO: read from the thread if it was rudely notified or not
  return curthd->wq.rudely_awoken == false;
}

void waitqueue::notify(int flags) {
  scoped_lock lck(lock);
top:
  if (!front) {
    navail++;
  } else {
    auto waiter = front;
    if (front == back) back = nullptr;
    front = waiter->next;
    // *nicely* awaken the thread
    if (!waiter->notify(flags)) {
      goto top;
    }
  }
}

void waitqueue::notify_all(int flags) {
  scoped_lock lck(lock);

  if (!front) {
    navail++;
  } else {
    while (front) {
      auto waiter = front;
      if (front == back) back = nullptr;
      front = waiter->next;
      // *nicely* awaken the thread
      waiter->notify(flags);
    }
  }
}

bool waitqueue::should_notify(u32 val) {
  lock.lock();
  if (front) {
    if (front->waiting_on <= val) {
      lock.unlock();
      return true;
    }
  }
  lock.unlock();
  return false;
}


#define SIGACT_IGNO 0
#define SIGACT_TERM 1
#define SIGACT_STOP 2
#define SIGACT_CONT 3


static int default_signal_action(int signal) {
  ASSERT(signal && signal < 64);

  switch (signal) {
    case SIGHUP:
    case SIGINT:
    case SIGKILL:
    case SIGPIPE:
    case SIGALRM:
    case SIGUSR1:
    case SIGUSR2:
    case SIGVTALRM:
    // case SIGSTKFLT:
    case SIGIO:
    case SIGPROF:
    case SIGTERM:
      return SIGACT_TERM;
    case SIGCHLD:
    case SIGURG:
    case SIGWINCH:
      // case SIGINFO:
      return SIGACT_IGNO;
    case SIGQUIT:
    case SIGILL:
    case SIGTRAP:
    case SIGABRT:
    case SIGBUS:
    case SIGFPE:
    case SIGSEGV:
    case SIGXCPU:
    case SIGXFSZ:
    case SIGSYS:
      return SIGACT_TERM;
    case SIGCONT:
      return SIGACT_CONT;
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
      return SIGACT_STOP;
  }

  return SIGACT_TERM;
}

void sched::dispatch_signal(int sig) {
  // sanity check
  if (sig < 0 || sig > 63) return;

  auto &action = curproc->sig.handlers[sig];

  if (sig == SIGSTOP) {
    printk("TODO: SIGSTOP\n");
    return;
  }

  if (sig == SIGCONT) {
    printk("TODO: SIGCONT\n");
    // resume_from_stopped();
  }

  if (action.sa_handler == SIG_DFL) {
    // handle the default action
    switch (default_signal_action(sig)) {
      case SIGACT_STOP:
        printk("TODO: SIGACT_STOP!\n");
        return;
      case SIGACT_TERM:
        sys::exit_proc(128 + sig);
        return;
      case SIGACT_IGNO:
				return;
      case SIGACT_CONT:
        printk("TODO: SIGACT_CONT!\n");
        return;
    }
  }

  // whatver the arch needs to do
  arch::dispatch_function((void *)action.sa_handler, sig);
}


void sched::before_iret(bool userspace) {
  if (!cpu::in_thread()) return;
  // exit via the scheduler if the task should die.
  if (userspace && curthd->should_die) sched::exit();

  long sig_to_handle = -1;


  while (1) {
    sig_to_handle = -1;
    if (curthd->sig.pending != 0) {
      for (int i = 0; i < 63; i++) {
        if ((curthd->sig.pending & SIGBIT(i)) != 0) {
          if (true || (curthd->sig.mask & SIGBIT(i))) {
            // Mark this signal as handled.
            curthd->sig.pending &= ~SIGBIT(i);
            sig_to_handle = i;
            break;
          }
        }
      }
    }

    if (sig_to_handle != -1) {
      sched::dispatch_signal(sig_to_handle);
    } else {
      break;
    }
  }
}

sleep_blocker::sleep_blocker(unsigned long us_to_sleep) {
  auto now = time::now_us();
  end_us = now + us_to_sleep;
  // printk("waiting %zuus from %zu till %zu\n", us_to_sleep, now, end_us);
}

bool sleep_blocker::should_unblock(struct thread &t, unsigned long now_us) {
  bool ready = now_us >= end_us;

  if (ready) {
    // printk("accuracy: %zdus\n", (long)now_us - (long)end_us);
  } else {
    // printk("time to go %zuus\n", end_us - now_us);
  }

  return ready;
}

int sys::usleep(unsigned long n) {
  // optimize short sleeps into a spinloop. Not sure if this is a good idea or
  // not, but I dont really care about efficiency right now :)
  if (false && n <= 1000 * 100 /* 100ms */) {
    unsigned long end = time::now_us() + n;
    while (1) {
      if (time::now_us() >= end) break;
      arch::relax();
    }
    return 0;
  }

  if (curthd->block<sleep_blocker>(n) != BLOCKRES_NORMAL) {
    return -1;
  }
  // printk("ret\n");
  return 0;
}
