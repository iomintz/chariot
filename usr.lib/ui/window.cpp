#include <gfx/scribe.h>
#include <ui/application.h>
#include <ui/event.h>
#include <ui/window.h>


ui::window::window(int id, ck::string name, gfx::rect r, ck::ref<gfx::shared_bitmap> bmp) {
  m_id = id;
  m_name = name;
  m_rect = r;
  m_bitmap = bmp;
}


ui::window::~window(void) {}


void ui::window::invalidate(const gfx::rect &r) {
  auto &app = ui::application::get();
  struct lumen::invalidate_msg iv;

  iv.nrects = 1;
  iv.id = m_id;
  iv.rects[0].x = r.x;
  iv.rects[0].y = r.y;
  iv.rects[0].w = r.w;
  iv.rects[0].h = r.h;

  struct lumen::invalidated_msg response = {0};
  app.send_msg_sync(LUMEN_MSG_WINDOW_INVALIDATE, iv, &response);
}

void ui::window::flush(void) { invalidate(m_rect); }

gfx::point last_pos;

void ui::window::handle_input(struct lumen::input_msg &msg) {
  if (msg.type == LUMEN_INPUT_KEYBOARD) {
    auto &k = msg.keyboard;

    if (k.flags & LUMEN_KBD_PRESS) {
      ui::keydown_event ev;
      ev.c = k.c;
      ev.flags = k.flags;
      ev.code = k.keycode;
      if (focused) focused->event(ev);
    } else {
      ui::keyup_event ev;
      ev.c = k.c;
      ev.flags = k.flags;
      ev.code = k.keycode;
      if (focused) focused->event(ev);
    }
  } else if (msg.type == LUMEN_INPUT_MOUSE) {
    ui::mouse_event ev;
    ev.x = msg.mouse.xpos;
    ev.y = msg.mouse.ypos;
    ev.dx = msg.mouse.dx;
    ev.dy = msg.mouse.dy;
    ev.left = (msg.mouse.buttons & LUMEN_MOUSE_LEFT_CLICK) != 0;
    ev.right = (msg.mouse.buttons & LUMEN_MOUSE_RIGHT_CLICK) != 0;

    if (msg.mouse.buttons & LUMEN_MOUSE_SCROLL_UP) {
      ev.ds = 1;
    } else if (msg.mouse.buttons & LUMEN_MOUSE_SCROLL_DOWN) {
      ev.ds = -1;
    }
    if (focused) {
      focused->event(ev);
    }
    // TODO: forward to the main widget or something
  }
}


void ui::window::schedule_reflow(void) {
  if (!m_pending_reflow) {
    ck::eventloop::defer(fn() {
      m_main_view->set_size(m_rect.w, m_rect.h);
      m_main_view->set_pos(0, 0);  // the main widget exists at the top left
      // tell the main vie to reflow
      m_main_view->do_reflow();
      this->m_pending_reflow = false;
    });
  }
}


ck::tuple<int, int> ui::window::resize(int w, int h) {
  // don't change stuff if you dont need to
  if (width() == w && height() == h) {
    return ck::tuple(w, h);
  }

  auto &app = ui::application::get();

  // allocate the messages
  struct lumen::resize_msg m = {0};
  struct lumen::resized_msg response = {0};
  m.id = this->m_id;
  m.width = w;
  m.height = h;

  app.send_msg_sync(LUMEN_MSG_RESIZE, m, &response);

  // failure case
  if (response.id != m.id) {
    // nothing changed, so we just return the old size
    return ck::tuple(width(), height());
  }

  auto new_bitmap = gfx::shared_bitmap::get(response.bitmap_name, response.width, response.height);

  if (!new_bitmap) printf("NOT FOUND!\n");
  // update the bitmap
  this->m_bitmap = new_bitmap;
  m_rect.w = width();
  m_rect.h = height();

  m_main_view->set_size(m_rect.w, m_rect.h);
  m_main_view->set_pos(0, 0);  // the main widget exists at the top left
  // tell the main vie to reflow
  m_main_view->do_reflow();
  return ck::tuple(w, h);
}
