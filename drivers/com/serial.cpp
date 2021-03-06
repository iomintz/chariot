#include <arch.h>
#include <asm.h>
#include <console.h>
#include <dev/driver.h>
#include <dev/serial.h>
#include <errno.h>
#include <module.h>
#include <printk.h>
#include "../majors.h"

#define IRQ_COM1 4

static int uart = 0;

#define BAUD 9600

void serial_install() {
  // Turn off the FIFO
  // outb(COM1 + 2, 0);

  // BAUD baud, 8 data bits, 1 stop bit, parity off.
  outb(COM1 + 3, 0x80);  // Unlock divisor
  outb(COM1 + 0, 115200 / BAUD);
  outb(COM1 + 1, 0);
  outb(COM1 + 3, 0x03);  // Lock divisor, 8 data bits.
  outb(COM1 + 4, 0);
  outb(COM1 + 1, 0x01);  // Enable receive interrupts.

  // If status is 0xFF, no serial port.
  if (inb(COM1 + 5) == 0xFF) return;
  uart = 1;
}

int serial_rcvd(int device) { return inb(device + 5) & 1; }

char serial_recv(int device) {
  while (!serial_rcvd(device)) {
  }
  return inb(device);
}

char serial_recv_async(int device) { return inb(device); }

int serial_transmit_empty(int device) { return inb(device + 5) & 0x20; }

void serial_send(int device, char out) {
  if (uart) {
    while (!serial_transmit_empty(device)) {
    }
    outb(device, out);
  }
}

void serial_string(int device, char *out) {
  for (uint32_t i = 0; out[i] != '\0'; i++) {
    outb(device, out[i]);
  }
}

static int uartgetc(void) {
  if (!uart) return -1;
  if (!(inb(COM1 + 5) & 0x01)) return -1;
  return inb(COM1 + 0);
}

void serial_irq_handle(int i, reg_t *) {
  size_t nread = 0;
  char buf[32];

  while (1) {
    int c = uartgetc();
    if (c < 0) break;

    // serial only sends \r for some reason
    if (c == '\r') c = '\n';

    if (nread > 32) {
      console::feed(nread, buf);
      nread = 0;
    }

    buf[nread] = c;
    nread++;
  }

  if (nread != 0) console::feed(nread, buf);
}




static ssize_t com_read(fs::file &f, char *dst, size_t sz) { return -ENOSYS; }

static ssize_t com_write(fs::file &f, const char *dst, size_t sz) {
  return -ENOSYS;
}


static struct fs::file_operations com_ops = {
    .read = com_read,
    .write = com_write,
};


static struct dev::driver_info com_driver {
  .name = "com", .type = DRIVER_CHAR, .major = MAJOR_COM, .char_ops = &com_ops,
};



static void serial_mod_init() {
  // setup interrupts on serial
  irq::install(32 + IRQ_COM1, serial_irq_handle, "COM1 Serial Port");

  // Acknowledge pre-existing interrupt conditions;
  // enable interrupts.
  inb(COM1 + 2);
  inb(COM1 + 0);

  dev::register_driver(com_driver);


  dev::register_name(com_driver, "com1", 0);
  // smp::ioapicenable(IRQ_COM1, 0);
}

module_init("serial", serial_mod_init);
