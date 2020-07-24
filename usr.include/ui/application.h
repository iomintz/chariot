#pragma once


#include <ck/eventloop.h>
#include <ck/object.h>
#include <ck/socket.h>
#include <lumen/msg.h>
#include <ui/window.h>
#include "stdlib.h"

namespace ui {

  class application : public ck::object {
    // this is the connection to the windowserver
    ck::localsocket sock;

   public:
    application(void);
    ~application(void);

    inline bool connected(void) { return sock.connected(); }

    long send_raw(int type, void *payload, size_t payloadsize);
    lumen::msg *send_raw_sync(int type, void *payload, size_t payloadsize);

    // send a bare message
    inline int send_msg(int type) { return send_raw(type, NULL, 0); }
    inline int send_msg_sync(int type) { return send_raw(type, NULL, 0); }

    template <typename T>
    inline int send_msg(int type, const T &payload) {
      return send_raw(type, (void *)&payload, sizeof(payload));
    }


    // Waits for a response to the specific message id
    template <typename T>
    inline lumen::msg *send_msg_sync(int type, const T &payload) {
      return send_raw_sync(type, (void *)&payload, sizeof(payload));
    }

    template <typename T, typename R>
    inline bool send_msg_sync(int type, const T &payload, R *ret) {
      auto *response = send_msg_sync(type, payload);
      if (response == NULL) return false;
      int success = true;

      if (success && response->len != sizeof(R)) success = false;
      if (success && ret != NULL) *ret = *(R *)response->data;

      delete response;
      return success;
    }


    ui::window *new_window(ck::string initial_title, int w, int h);


    void drain_messages(void);
    void dispatch_messages(void);

    CK_OBJECT(ui::application);

    void start(void);

    auto &eventloop(void) { return m_eventloop; }

#define THE_APP ui::application::get()
    static ui::application &get(void);

   private:
    // all the windows in the application
    ck::map<int, ck::unique_ptr<ui::window>> m_windows;
    ck::vec<lumen::msg *> m_pending_messages;
    ck::eventloop m_eventloop;
  };
};  // namespace ui
