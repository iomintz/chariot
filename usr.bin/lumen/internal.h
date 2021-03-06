#pragma once

#include <chariot/fb.h>
#include <chariot/keycode.h>
#include <chariot/mouse_packet.h>
#include <ck/io.h>
#include <ck/socket.h>
#include <ck/timer.h>
#include <gfx/bitmap.h>
#include <gfx/point.h>
#include <gfx/scribe.h>
#include <lumen/msg.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <ck/lock.h>
#include "gfx/rect.h"
#include <pthread.h>

namespace lumen {

  // fwd decl
  struct guest;

  enum mouse_cursor : char {
    pointer = 0,
    grab = 1,
    grabbing = 2,

    mouse_count
  };


  // represents a framebuffer for a screen. This also renders the mouse cursor
  // with double buffering
  class screen {
   public:
    int fd = -1;
    size_t bufsz = 0;
    uint32_t *buf = NULL;
    struct ck_fb_info info;


    int buffer_index = 0;
    uint32_t *front_buffer;
    uint32_t *back_buffer;

    bool mouse_moved = false;

    gfx::rect m_bounds;
    gfx::point mouse_pos;

    lumen::mouse_cursor cursor = lumen::mouse_cursor::pointer;
    ck::ref<gfx::bitmap> cursors[mouse_cursor::mouse_count];


    void flush_info(void) {
      info.active = 1;
      if (ioctl(fd, FB_SET_INFO, &info) < 0) {
        ioctl(fd, FB_GET_INFO, &info);
      }
    }

    void load_info(void) { ioctl(fd, FB_GET_INFO, &info); }

    inline gfx::rect mouse_rect(void) {
      return gfx::rect(mouse_pos.x() - 6, mouse_pos.y() - 6,
                       cursors[cursor]->width(), cursors[cursor]->height());
    }

    screen(int w, int h);
    ~screen(void);

    void flip_buffers(void);

    // returns the new mouse position
    const gfx::point &handle_mouse_input(struct mouse_packet &pkt);
    // draw the mouse later
    void draw_mouse(void);

    void set_resolution(int w, int h);
    inline size_t screensize(void) { return bufsz; }
    inline uint32_t *pixels(void) { return buf; }


    inline uint32_t *buffer(void) { return back_buffer; }
    inline void set_pixel(int x, int y, uint32_t color) {
      buffer()[x + y * m_bounds.w] = color;
    }


    // THIS IS VERY SLOW!!!
    inline uint32_t get_pixel(int x, int y) {
      return buffer()[x + y * m_bounds.w];
    }

    inline void clear(uint32_t color) {
      for (int i = 0; i < width() * height(); i++) {
        buffer()[i] = color;
      }
    }

    inline int width(void) { return m_bounds.w; }
    inline int height(void) { return m_bounds.h; }

    inline const gfx::rect &bounds(void) { return m_bounds; }
  };


  enum class window_mode : char {
    normal,  // no borders, only a title bar.
  };

  enum class hover_result : char {
    normal,     // normal pointer
    draggable,  // this region of the window is draggable
  };


  struct window {
    int id = 0;
    ck::string name;
    lumen::guest &guest;
    gfx::rect rect;

    bool hovered = false;
    bool focused = false;


		ck::mutex window_lock;
		long pending_invalidation_id = -1;

    window_mode mode;

    ck::ref<gfx::shared_bitmap> bitmap;

    window(int id, lumen::guest &c, int w, int h);
    ~window(void);

    // get the bounds of the window frame
    gfx::rect bounds();

    // users invalidate the bounds of their bitmap, not the window. So we gotta
    // translate that invalidation to the actual bounds of the bitmap on the
    // screen.
    void translate_invalidation(gfx::rect &r);


#define WINDOW_REGION_NORM 1  // use the window's cursor style
#define WINDOW_REGION_DRAG 2
#define WINDOW_REGION_CLOSE 3
    // return one of the above ^
    int handle_mouse_input(gfx::point &r, struct mouse_packet &p);

    int handle_keyboard_input(struct keyboard_packet_t &p);

    // used to tell the window compositor where in the window we are hovering.
    hover_result hover();

    void set_mode(lumen::window_mode);

    // draw within the scribe. The scribe is offset to within the window
    void draw(gfx::scribe &);
  };


  // a guest who is connected to the server
  // guests can have many windows
  struct guest {
    unsigned long id = 0;
    struct context &ctx;  // the context we live in
    ck::ipcsocket *connection;
    ck::map<long, struct window *> windows;

		ck::mutex guest_lock;

    guest(long id, struct context &ctx, ck::ipcsocket *conn);
    ~guest(void);

    void process_message(lumen::msg &);
    void on_read(void);

    long send_raw(int type, int id, void *payload, size_t payloadsize);

    template <typename T>
    inline int send_msg(int type, const T &payload) {
      return send_raw(type, -1, (void *)&payload, sizeof(payload));
    }


    template <typename T>
    inline int respond(lumen::msg &m, int type, const T &payload) {
      return send_raw(type, m.id, (void *)&payload, sizeof(payload));
    }

    struct window *new_window(ck::string name, int w, int h);


    // a big number.
    long next_window_id = 0;
    long next_msg_id = 0x7F00'0000'0000'0000;
  };


  /**
   * contains all the state needed to run the window server
   */
  struct context {
    lumen::screen screen;

    ck::file keyboard, mouse;
    ck::ipcsocket server;

    ck::ref<ck::timer> compose_timer;
    context(void);



		pthread_t compositor_thread;

    void accept_connection(void);
    void handle_keyboard_input(keyboard_packet_t &pkt);
    void handle_mouse_input(struct mouse_packet &pkt);
    void process_message(lumen::guest &c, lumen::msg &);
    void guest_closed(long id);


    void window_opened(window *);
    void window_closed(window *);

    void calculate_hover(void);


		static void *compositor_thread_worker(void *arg);
    void compose(void);

    struct window_ref {
      // the client and the window id
      long client, id;
    };


    // ordered list of all the windows (front to back)
    // The currently focused window is at the front
    ck::vec<lumen::window *> windows;
		ck::mutex windows_lock;

    lumen::window *hovered_window = nullptr;
    lumen::window *focused_window = nullptr;
    bool dragging = false;

		ck::mutex dirty_regions_lock;
    ck::vec<gfx::rect> dirty_regions;

    void invalidate(const gfx::rect &r);

    // select a window, moving it around the view stack and all that fun stuff
    void select_window(lumen::window *win);

    // return if a rect in a window is occluded
    bool occluded(lumen::window &win, const gfx::rect &r);


   private:
    ck::map<long, lumen::guest *> guests;
    long next_guest_id = 0;
  };


}  // namespace lumen
