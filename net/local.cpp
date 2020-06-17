#include <awaitfs.h>
#include <errno.h>
#include <fs/vfs.h>
#include <lock.h>
#include <net/sock.h>
#include <net/un.h>
#include <syscall.h>
#include <util.h>

static rwlock all_localsocks_lock;
static struct net::localsock *all_localsocks = NULL;

net::localsock::localsock(int type) : net::sock(AF_LOCAL, type, 0) {
  all_localsocks_lock.write_lock();
  next = all_localsocks;
  prev = nullptr;
  all_localsocks = this;
  if (next != NULL) {
    next->prev = this;
  }
  all_localsocks_lock.write_unlock();
}

net::localsock::~localsock(void) {
  all_localsocks_lock.write_lock();
  if (all_localsocks == this) {
    all_localsocks = next;
  }

  if (next) next->prev = prev;
  if (prev) prev->next = next;

  // if this socket is bound, release it
  if (bindpoint != NULL) {
    // we don't need to
    bindpoint->bound_socket = NULL;
    fs::inode::release(bindpoint);
  }
  all_localsocks_lock.write_unlock();
}


net::sock *net::localsock::accept(struct sockaddr *uaddr, int addr_len,
                                  int &err) {
  // wait on a client
  auto *client = pending_connections.recv();
  return client;
}

int net::localsock::connect(struct sockaddr *addr, int len) {
  if (len != sizeof(struct sockaddr_un)) {
    return -EINVAL;
  }

  auto un = (struct sockaddr_un *)addr;
  string path;
  for (int i = 0; i < UNIX_PATH_MAX; i++) {
    char c = un->sun_path[i];
    if (c == '\0') break;
    path.push(c);
  }

  auto in = vfs::open(path, O_RDWR);
  if (in == NULL) return -ENOENT;

  if (in->bound_socket == NULL) {
    return -ENOENT;
  }

  peer = (struct localsock *)net::sock::acquire(*in->bound_socket);

  // send, and wait. This will always succeed if we are here.
  this->peer->pending_connections.send(this, true);

  // assume the above succeeded
  return 0;
}


int net::localsock::disconnect(int flags) {
  if (flags & PFLAGS_CLIENT) {
    // printk("client disconnect\n");
    for_server.close();
  }
  if (flags & PFLAGS_SERVER) {
    // printk("server disconnect\n");
    for_client.close();
  }
  return 0;
}


ssize_t net::localsock::sendto(fs::file &fd, void *data, size_t len, int flags,
                               const sockaddr *, size_t) {
  bool block = (flags & MSG_DONTWAIT) == 0;
  // printk("sendto(%p, %zu)\n", data, len);
  if (fd.pflags & PFLAGS_SERVER) {
    return for_client.write(data, len, block);
  } else if (fd.pflags & PFLAGS_CLIENT) {
    return for_server.write(data, len, block);
  }
  panic("invalid file descriptor for localsock::sendto()\n");
  return -EINVAL;
}
ssize_t net::localsock::recvfrom(fs::file &fd, void *data, size_t len,
                                 int flags, const sockaddr *, size_t) {
  bool block = (flags & MSG_DONTWAIT) == 0;
  // printk("recvfrom(%p, %zu)\n", data,) == 0 len);
  if (fd.pflags & PFLAGS_SERVER) {
    return for_server.read(data, len, block);
  } else if (fd.pflags & PFLAGS_CLIENT) {
    return for_client.read(data, len, block);
  }
  panic("invalid file descriptor for localsock::recvfrom()\n");
  return -EINVAL;
}

int net::localsock::bind(const struct sockaddr *addr, size_t len) {
  if (len != sizeof(struct sockaddr_un)) return -EINVAL;
  if (bindpoint != NULL) return -EINVAL;

  auto un = (struct sockaddr_un *)addr;

  string path;
  for (int i = 0; i < UNIX_PATH_MAX; i++) {
    char c = un->sun_path[i];
    if (c == '\0') break;
    path.push(c);
  }

  auto in = vfs::open(path, O_RDWR);
  if (in == NULL) return -ENOENT;
  // has someone already bound to this file?
  if (in->bound_socket != NULL) return -EADDRINUSE;

  // acquire the file and set the bound_socket to this
  bindpoint = fs::inode::acquire(in);
  bindpoint->bound_socket = net::sock::acquire(*this);
  return 0;
}


int net::localsock::poll(fs::file &f, int events) {
  int res = 0;

  if (f.pflags & PFLAGS_CLIENT) {
    res |= for_client.poll() & (AWAITFS_READ);
  } else if (f.pflags & PFLAGS_SERVER) {
    // A pending connection is considered an AWAITFS_READ event
    res |= for_server.poll() & (AWAITFS_READ);
  }

  res |= pending_connections.poll() & AWAITFS_READ;

  return res & events & AWAITFS_READ;
}
