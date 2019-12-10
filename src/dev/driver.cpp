#include <dev/driver.h>
#include <errno.h>
#include <map.h>
#include <printk.h>

#define DRIVER_DEBUG

#ifdef DRIVER_DEBUG
#define INFO(fmt, args...) printk("[DRIVER] " fmt, ##args)
#else
#define INFO(fmt, args...)
#endif

dev::driver::driver() {
  // INFO("driver '%s' created\n", name());
}

dev::driver::~driver(void) {}

ref<dev::device> dev::driver::open(major_t maj, minor_t min) {
  int err;
  return open(maj, min, err);
}
ref<dev::device> dev::driver::open(major_t, minor_t, int &errcode) {
  errcode = -ENOTIMPL;
  return nullptr;
}

int dev::driver::release(dev::device *) { return -ENOTIMPL; }

/**
 * the internal structure of devices
 */
struct driver_instance {
  string name;
  major_t major;
  unique_ptr<dev::driver> driver;
};

// every device drivers, accessable through their major number
static map<major_t, struct driver_instance> device_drivers;


int dev::register_driver(major_t major, unique_ptr<dev::driver> d) {
  // TODO: take a lock

  if (d.get() == nullptr) return -ENOENT;

  if (major > MAX_DRIVERS) return -E2BIG;

  if (device_drivers.contains(major)) {
    return -EEXIST;
  }

  struct driver_instance inst;
  inst.name = d->name();
  inst.major = major;
  inst.driver = move(d);

  device_drivers[major] = move(inst);


  return 0;
}

int dev::deregister_driver(major_t major) {
  // TODO: take a lock

  if (major > MAX_DRIVERS) return -E2BIG;

  if (device_drivers.contains(major)) {
    // TODO: call driver::deregister() or something
    device_drivers.remove(major);
    return 0;
  }

  return -ENOENT;
}

map<string, dev_t> device_names;

int dev::register_name(string name, major_t major, minor_t minor) {
  // TODO: take a lock
  if (device_names.contains(name)) return -EEXIST;
  device_names[name] = {major, minor};
  return 0;
}

int dev::deregister_name(string name) {
  // TODO: take a lock
  if (device_names.contains(name)) device_names.remove(name);
  return -ENOENT;
}

// main API to opening devices

ref<dev::device> dev::open(string name) {
  int err;

  if (device_names.contains(name)) {
    auto d = device_names.get(name);
    return dev::open(d.major(), d.minor(), err);
  }

  return nullptr;
}

ref<dev::device> dev::open(major_t maj, minor_t min) {
  int err;
  auto dev = open(maj, min, err);
  if (err != 0)
    printk(
        "[DRIVER:ERR] open device %d:%d failed with unhandled error code %d\n",
        maj, min, -err);
  return dev;
}

ref<dev::device> dev::open(major_t maj, minor_t min, int &errcode) {
  // TODO: is this needed?
  if (maj > MAX_DRIVERS) {
    errcode = -E2BIG;
    return nullptr;
  }

  // TODO: take a lock!
  if (device_drivers.contains(maj)) {
    errcode = 0;
    return device_drivers[maj].driver->open(maj, min, errcode);
  }
  // if there wasnt a driver in the major list, return such
  errcode = -ENOENT;
  return nullptr;
}
