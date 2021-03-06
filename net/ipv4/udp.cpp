#include <errno.h>
#include <module.h>
#include <net/in.h>
#include <net/ipv4.h>
#include <net/sock.h>
#include <util.h>

static rwlock active_socket_lock;
static map<uint16_t /* host ordered */, net::udpsock *> active_sockets;

#define MIN_EPHEMERAL 49152
#define MAX_EPHEMERAL 65535
#define TOTAL_EPHEMERAL (MAX_EPHEMERAL - MIN_EPHEMERAL)
// this function expects active_socket_lock to be held as a reader or writer
static uint16_t next_ephemeral_port(void) {
  // finds a port 0..TOTAL instead of MIN..MAX
  // then adds MIN to it to check. Nice and easy
  static uint16_t last = 0;
  // loop around till you hit next again
  for (uint16_t port = last + 1; port != last;
       port = (port + 1) % TOTAL_EPHEMERAL) {
    if (!active_sockets.contains(port + MIN_EPHEMERAL)) {
      last = port;
      return port + MIN_EPHEMERAL;
    }
  }
  panic("couldn't find ephemeral udp port");
  return 0;
}

net::udpsock::udpsock(int domain, int type, int protocol)
    : ipv4sock(domain, type, protocol) {

  active_socket_lock.write_lock();
  uint16_t port = next_ephemeral_port();
  local_port = net::net_ord(port);
  active_sockets.set(port, this);
  active_socket_lock.write_unlock();
}

net::udpsock::~udpsock(void) {
  active_socket_lock.write_lock();
  active_sockets.remove(net::host_ord(local_port));
  active_socket_lock.write_unlock();
}

ssize_t net::udpsock::sendto(fs::file&, void *data, size_t len, int flags,
			     const struct sockaddr *addr, size_t alen) {
  (void)flags;

  if (addr && alen != sizeof(sockaddr_in)) return -EINVAL;

  // lock this ip socket (we change it's target ip and whatnot)
  scoped_lock l(iplock);

  if (addr) {
    // target ip address
    if (addr->sa_family != AF_INET) return -EINVAL;

    auto in = (const struct sockaddr_in *)addr;
    // printk("addr = %I\n", net::host_ord(in->sin_addr.s_addr));
    // printk("port = %04x\n", net::host_ord(in->sin_port));
    // printk("ipv4 handle sockaddr\n");

    peer_addr = in->sin_addr.s_addr;
    peer_port = in->sin_port;

    size_t final_len = sizeof(net::udp::hdr) + len;

    if (final_len >= 0xFFFF - sizeof(net::udp::hdr)) {
      return -E2BIG;
    }

    auto buf = kmalloc(final_len);
    auto *udp = (net::udp::hdr *)buf;

    // this should always "succeed"
    udp->length = net::net_ord((uint16_t)final_len);
    udp->source_port = local_port;
    udp->destination_port = peer_port;
    udp->checksum = 0;

    memcpy(udp + 1, data, len);
    auto res = send_packet((void *)udp, final_len);
    total_sent += final_len;

    kfree(buf);

    return res;
  }

  return -EDESTADDRREQ;
}

ssize_t net::udpsock::recvfrom(fs::file &, void *data, size_t len, int flags,
			       const sockaddr *, size_t) {
  return -ENOTIMPL;
}

int net::udpsock::bind(const struct sockaddr *addr, size_t len) {
  if (len != sizeof(sockaddr_in)) return -EINVAL;

  auto *sin = (struct sockaddr_in *)addr;
  // printk("udp bind to %I:%d\n", sin->sin_addr, net::host_ord(sin->sin_port));

  // TODO: check for permission
  active_socket_lock.write_lock();

  uint16_t hport = net::host_ord(sin->sin_port);

  if (active_sockets.contains(hport)) {
    active_socket_lock.write_unlock();
    return -EEXIST;
  }

	auto current_port = net::host_ord(local_port);

	// not bound?
	if (current_port != 0) {
		active_sockets.remove(current_port);
	}

  local_port = sin->sin_port;
  active_sockets.set(hport, this);
  active_socket_lock.write_unlock();

  return 0;
}
