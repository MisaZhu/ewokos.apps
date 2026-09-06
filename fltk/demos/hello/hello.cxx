/*
 * fltkhello - minimal FLTK demo for EwokOS.
 *
 * Exercises the pieces a real FLTK app depends on: an Fl_Window (which the
 * NX11 bridge turns into a real EwokOS xwin window), labels, buttons, widget
 * callbacks and the Fl::run() event loop that the patched fl_wait() drives
 * through nano-X's nx11x_select().
 */
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <stdio.h>
#include <stdlib.h>

static int   clicks = 0;
static char  msg[64] = "Hello, EwokOS!";
static Fl_Box *box = 0;

static void click_cb(Fl_Widget *, void *) {
  clicks++;
  snprintf(msg, sizeof(msg), "Clicked %d time%s", clicks, clicks == 1 ? "" : "s");
  if (box) { box->label(msg); box->redraw(); }
}

static void quit_cb(Fl_Widget *, void *) {
  exit(0);
}

int main(int argc, char **argv) {
  Fl_Window *win = new Fl_Window(320, 150, "FLTK on EwokOS");

  box = new Fl_Box(FL_FLAT_BOX, 20, 20, 280, 40, msg);
  box->labelfont(FL_BOLD);
  box->labelsize(18);

  Fl_Button *click = new Fl_Button(20, 80, 180, 40, "Click me");
  click->callback(click_cb);

  Fl_Button *quit = new Fl_Button(210, 80, 90, 40, "Quit");
  quit->callback(quit_cb);

  win->end();
  win->show(argc, argv);
  return Fl::run();
}
