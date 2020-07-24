#include <gfx/bitmap.h>
#include <gfx/font.h>
#include <gfx/rect.h>
#include <gfx/scribe.h>
#include <lumen.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/emx.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ui/application.h>
#include <ui/view.h>
#include <ui/window.h>
#include <unistd.h>

#include <ck/array.h>
#include <ck/eventloop.h>
#include <ck/option.h>
#include <ck/rand.h>
#include <ck/strstream.h>
#include <ck/timer.h>
#include <ck/tuple.h>
#include <ck/unicode.h>
#include <ck/mem.h>


struct line {
	gfx::point start;
	gfx::point end;
};

class painter : public ui::view {
  int color = 0;

	ck::vec<struct line> lines;

 public:
  painter(void) {
		//
		color = rand();
	}


  virtual void paint_event(void) override {
    auto s = get_scribe();
    // s.clear(color);
    // s.draw_frame(gfx::rect(0, 0, width(), height()), color);

		for (auto &line : lines) {
    	s.draw_line(line.start, line.end, color);

		}
    invalidate();
  }

  virtual void on_mouse_move(ui::mouse_event& ev) override {
    auto s = get_scribe();

    int ox = ev.x - ev.dx;
    int oy = ev.y - ev.dy;

		lines.push({.start = gfx::point(ox, oy), .end = gfx::point(ev.x, ev.y)});

		repaint();
  }
};



int main(int argc, char** argv) {

	return 0;
  /*
  int emx = emx_create();

  int foo = 0;
  emx_set(emx, 0, (void*)&foo, EMX_READ);

  int events = 0;
  void *key = emx_wait(emx, &events);

  printf("key=%p, foo=%p\n", key, &foo);

  close(emx);
  return 0;
  */

  // ck::strstream stream;
  // stream.fmt("hello");

  while (1) {
    // connect to the window server
    ui::application app;
    ck::vec<ui::window*> windows;

    int width = 300;
    int height = 300;


    char buf[50];
    for (int i = 0; i < 5; i++) {
      sprintf(buf, "[Window %d]", i);
      auto win = app.new_window(buf, width, height);
      windows.push(win);
      auto& stk = win->set_view<ui::stackview>(ui::direction::horizontal);

      stk.spawn<painter>();
    }

    auto input = ck::file::unowned(0);
    input->on_read([&] {
      getchar();
      ck::eventloop::exit();
    });

    // auto t = ck::timer::make_timeout(1, [] { ck::eventloop::exit(); });

    // start the application!
    app.start();

    windows.clear();
  }


  return 0;
}
