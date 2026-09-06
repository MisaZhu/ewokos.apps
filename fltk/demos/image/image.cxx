/*
 * fltkimage - FLTK image-format demo for EwokOS.
 *
 * Proves the libfltk_images port end to end:
 *   1. fl_register_images() hooks the GIF/PNG/JPEG/PNM/BMP loaders into
 *      Fl_Shared_Image (without it, Fl_Shared_Image only knows the core
 *      XBM/XPM formats).
 *   2. A PNG and a JPEG are decoded through libpng + libjpeg, linked from the
 *      SDK, into Fl_RGB_Images.
 *   3. Each is drawn by fl_draw_image -> XPutImage on NX11, and its decoded
 *      width x height x depth is printed under the picture, so a successful
 *      decode is verifiable even before comparing pixels.
 *
 * The sample files live in the app's resource dir.  EwokOS installs apps under
 * /apps/<task>, and RESDIR is baked in by the Makefile - the same convention
 * the macemu (-DDATADIR) and previous (-DCONFDIR) ports use for their data.
 */
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Shared_Image.H>
#include <stdio.h>
#include <stdlib.h>

#ifndef RESDIR
#define RESDIR "/apps/fltkimage/res"
#endif

static const int PW  = 320;   // sample.png / sample.jpg are 320x240
static const int PH  = 240;
static const int PAD = 12;
static const int CAPH = 20;

static void quit_cb(Fl_Widget *, void *) { exit(0); }

/*
 * Put a decoded image into `view` and write a one-line status into `cap`.
 * `capbuf` must outlive the label (Fl_Widget::label() stores the pointer, it
 * does not copy), so callers pass a static buffer.  Returns 1 on a real decode
 * (non-zero width), else 0.
 */
static int show_image(Fl_Shared_Image *img, const char *fmt, const char *path,
                      Fl_Box *view, Fl_Box *cap, char *capbuf, int caplen) {
  if (img && img->w() > 0 && img->h() > 0) {
    view->image(img);
    snprintf(capbuf, caplen, "%s  %d x %d x %d   OK",
             fmt, img->w(), img->h(), img->d());
    printf("fltkimage: decoded %s %s -> %dx%dx%d\n",
           fmt, path, img->w(), img->h(), img->d());
  } else {
    snprintf(capbuf, caplen, "%s  load FAILED: %s", fmt, path);
    printf("fltkimage: FAILED to decode %s %s\n", fmt, path);
  }
  cap->label(capbuf);
  return (img && img->w() > 0) ? 1 : 0;
}

int main(int argc, char **argv) {
  /* Register GIF/PNG/JPEG/PNM/BMP BEFORE any Fl_Shared_Image::get(). */
  fl_register_images();

  char pngpath[128], jpgpath[128];
  snprintf(pngpath, sizeof(pngpath), "%s/sample.png", RESDIR);
  snprintf(jpgpath, sizeof(jpgpath), "%s/sample.jpg", RESDIR);

  /* Decoding needs no open display, so load before the window is built. */
  Fl_Shared_Image *png = Fl_Shared_Image::get(pngpath);
  Fl_Shared_Image *jpg = Fl_Shared_Image::get(jpgpath);

  int winw = PAD * 3 + PW * 2;
  int winh = PAD + PH + 4 + CAPH + PAD + 28 + PAD;

  Fl_Window *win = new Fl_Window(winw, winh, "FLTK Images on EwokOS");

  int x1 = PAD, x2 = PAD * 2 + PW;
  int yimg = PAD, ycap = PAD + PH + 4, ybtn = ycap + CAPH + PAD;

  Fl_Box *pv = new Fl_Box(x1, yimg, PW, PH);
  Fl_Box *jv = new Fl_Box(x2, yimg, PW, PH);
  pv->box(FL_DOWN_BOX);
  jv->box(FL_DOWN_BOX);

  Fl_Box *pc = new Fl_Box(x1, ycap, PW, CAPH, "");
  Fl_Box *jc = new Fl_Box(x2, ycap, PW, CAPH, "");
  pc->labelfont(FL_COURIER); pc->labelsize(12);
  jc->labelfont(FL_COURIER); jc->labelsize(12);

  Fl_Button *quit = new Fl_Button(winw / 2 - 50, ybtn, 100, 28, "Quit");
  quit->callback(quit_cb);

  /* Static so the label pointers stay valid for the life of the program. */
  static char pbuf[128], jbuf[128];
  int png_ok = show_image(png, "PNG ", pngpath, pv, pc, pbuf, sizeof(pbuf));
  int jpg_ok = show_image(jpg, "JPEG", jpgpath, jv, jc, jbuf, sizeof(jbuf));
  printf("fltkimage: PNG %s, JPEG %s\n",
         png_ok ? "ok" : "FAILED", jpg_ok ? "ok" : "FAILED");

  win->end();
  win->show(argc, argv);

  return Fl::run();
}
