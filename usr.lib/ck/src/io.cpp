#define _CHARIOT_SRC
#include <ck/io.h>
#include <ck/object.h>
#include <ck/ptr.h>
#include <ck/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <unistd.h>


ck::buffer::buffer(size_t size) {
  m_buf = calloc(size, 1);
  m_size = size;
}

ck::buffer::~buffer(void) {
  if (m_buf) free(m_buf);
}



extern "C" int vfctprintf(void (*out)(char character, void *arg), void *arg,
                          const char *format, va_list va);


static void ck_file_writef_callback(char c, void *arg) {
  auto *f = (ck::stream *)arg;
  f->write(&c, 1);
}


int ck::stream::fmt(const char *format, ...) {
  va_list va;
  va_start(va, format);
  const int ret = vfctprintf(ck_file_writef_callback, (void *)static_cast<ck::stream*>(this), format, va);
  va_end(va);
  return ret;
}


ssize_t ck::file::read(void *buf, size_t sz) {
  if (eof()) return 0;

  if (m_fd == -1) return 0;

  size_t k = ::read(m_fd, buf, sz);

  if (k < 0) {
    set_eof(true);
    return 0;
  }
  return k;
}

ssize_t ck::file::write(const void *buf, size_t sz) {
  if (m_fd == -1) return 0;

  auto *src = (const unsigned char *)buf;

  if (buffered() && m_buffer != NULL) {
    /**
     * copy from the src into the buffer, flushing on \n or when it is full
     */
    for (size_t i = 0; i < sz; i++) {
      // Assume that the buffer has space avail.
      unsigned char c = src[i];

      m_buffer[buf_len++] = c;

      if (c == '\n' || /* is full */ buf_len == buf_cap) {
        flush();
      }
    }
    return sz;
  }

  long k = errno_syscall(SYS_write, m_fd, src, sz);
  if (k < 0) return 0;

  return k;
}

ssize_t ck::file::size(void) {
  if (m_fd == -1) return -1;

  auto original = tell();
  if (lseek(m_fd, 0, SEEK_END) == -1) return -1;
  auto sz = tell();
  // not sure how we handle an error here, we may have broken it
  // with the first call to fseek
  if (lseek(m_fd, original, SEEK_SET) == -1) return -1;
  return sz;
}

ssize_t ck::file::tell(void) {
  if (m_fd == -1) return -1;
  return lseek(m_fd, 0, SEEK_CUR);
}

int ck::file::seek(long offset, int whence) {
  return lseek(m_fd, offset, whence);
}

int ck::file::stat(struct stat &s) {
  if (m_fd == -1) return -1;
  return fstat(m_fd, &s);
}


void ck::file::flush(void) {
  if (buffered()) {
    errno_syscall(SYS_write, m_fd, m_buffer, buf_len);
    // ck::hexdump(m_buffer, buf_len);
    buf_len = 0;
    // memset(m_buffer, 0, buf_cap);
  }
}


void ck::file::set_buffer(size_t size) {
  if (m_buffer != NULL) {
    flush();
    free(m_buffer);
  }
  if (size > 0) {
    buf_cap = size;
    buf_len = 0;
    m_buffer = (uint8_t *)calloc(1, size);
  }
}


static int string_to_mode(const char *mode) {
  if (mode == NULL) return -1;
#define DEF_MODE(s, m) \
  if (!strcmp(mode, s)) return m

  DEF_MODE("r", O_RDONLY);
  DEF_MODE("rb", O_RDONLY);

  DEF_MODE("w", O_WRONLY | O_CREAT | O_TRUNC);
  DEF_MODE("wb", O_WRONLY | O_CREAT | O_TRUNC);

  DEF_MODE("a", O_WRONLY | O_CREAT | O_APPEND);
  DEF_MODE("ab", O_WRONLY | O_CREAT | O_APPEND);

  DEF_MODE("r+", O_RDWR);
  DEF_MODE("rb+", O_RDWR);

  DEF_MODE("w+", O_RDWR | O_CREAT | O_TRUNC);
  DEF_MODE("wb+", O_RDWR | O_CREAT | O_TRUNC);

  DEF_MODE("a+", O_RDWR | O_CREAT | O_APPEND);
  DEF_MODE("ab+", O_RDWR | O_CREAT | O_APPEND);

  return -1;
#undef DEF_MODE
}


ck::file::file(void) { m_fd = -1; }

ck::file::file(ck::string path, const char *mode) {
  m_fd = -1;
  this->open(path, mode);
}



bool ck::file::open(ck::string path, const char *mode) {
  int fmode = string_to_mode(mode);
  if (fmode == -1) {
    fprintf(::stderr, "[ck::file::open] '%s' is an invalid mode\n", mode);
    notifier.set_active(false);
    return false;
  }


  int new_fd = ::open(path.get(), fmode);

  if (new_fd < 0) {
    notifier.set_active(false);
    return false;
  }

  if (m_fd != -1) {
    flush();
    close(m_fd);
  }

  m_fd = new_fd;
  init_notifier();

  return true;
}



ck::file::file(int fd) {
  m_fd = fd;
  init_notifier();
}

ck::file::~file(void) {
  if (m_fd != -1 && m_owns) {
    flush();
    close(m_fd);
  }
}

void ck::file::init_notifier(void) {
  notifier.init(m_fd, 0);
  notifier.on_event = [this](int event) {
    if (event == CK_EVENT_READ && this->m_on_read) this->m_on_read();
    if (event == CK_EVENT_WRITE && this->m_on_write) this->m_on_write();
  };

  update_notifier();
}


void ck::file::update_notifier(void) {
  int mask = 0;

  if (this->m_on_read) mask |= AWAITFS_READ;
  if (this->m_on_write) mask |= AWAITFS_WRITE;
  notifier.set_active(mask != 0);
  notifier.set_event_mask(mask);
}



void ck::hexdump(void *buf, size_t sz) { debug_hexdump(buf, sz); }


void ck::hexdump(const ck::buffer &buf) {
  debug_hexdump(buf.get(), buf.size());
}



// Socket implementation


ck::socket::socket(int fd, int domain, int type, int protocol) : ck::file(fd) {
  m_domain = domain;
  m_type = type;

  m_proto = protocol;

  // sockets should not be buffered
  set_buffer(0);
}


ck::socket::socket(int domain, int type, int protocol)
    : ck::socket(::socket(AF_UNIX, SOCK_STREAM, 0), AF_UNIX, SOCK_STREAM, 0) {}


ck::socket::~socket(void) {
  // nothing for now.
}

bool ck::socket::connect(struct sockaddr *addr, size_t size) {
  int connect_res = ::connect(ck::file::m_fd, addr, size);

  if (connect_res < 0) {
    // EOF is how we specify that a file is or is not readable.
    set_eof(true);
    return false;
  }

  m_connected = true;
  set_eof(false);
  return true;
}


ssize_t ck::socket::send(void *buf, size_t sz, int flags) {
  if (eof()) return 0;
  if (m_fd == -1) return 0;
  return ::send(m_fd, buf, sz, flags);
}


ssize_t ck::socket::recv(void *buf, size_t sz, int flags) {
  if (eof()) return 0;
  if (m_fd == -1) return 0;
  int nread = ::recv(m_fd, buf, sz, flags);
  if (nread == 0) set_eof(true);
  return nread;
}



bool ck::localsocket::connect(ck::string str) {
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;

  // bind the local socket to the windowserver
  strncpy(addr.sun_path, str.get(), sizeof(addr.sun_path) - 1);
  return ck::socket::connect((struct sockaddr *)&addr, sizeof(addr));
}



int ck::localsocket::listen(ck::string path, ck::func<void()> cb) {
  // are we already listening?
  if (m_listening) return false;

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.get(), sizeof(addr.sun_path) - 1);


  int bind_res = ::bind(m_fd, (struct sockaddr *)&addr, sizeof(addr));

  if (bind_res == 0) {
    m_listening = true;
    on_read(move(cb));
  }
  return bind_res;
}


ck::localsocket *ck::localsocket::accept(void) {
  int client = ::accept(m_fd, (struct sockaddr *)&addr, sizeof(addr));

  if (client < 0) return nullptr;

  return new ck::localsocket(client);
}
