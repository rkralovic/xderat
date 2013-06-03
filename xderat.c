#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/XTest.h>
#include <X11/Xlib.h>

////////////////
// X state:
Display* dpy;
XineramaScreenInfo* screens;
int num_screens;

XFontStruct* font;
Font font_id;

const char* font_name = "terminus-14";
#define LABEL_LEN 3

// global state
char pfx[LABEL_LEN + 1];
int pfx_idx;
int drag, done, s;

// private
int screens_x_allocated;

// methods
void Init() {
  dpy = XOpenDisplay(NULL);
  if (dpy == NULL) {
    fprintf(stderr, "Can not open display!\n");
    exit(1);
  }
  int event, error;
  if (XineramaQueryExtension(dpy, &event, &error) &&
      XineramaIsActive(dpy)) {
    int i;
    Screen* screen = XDefaultScreenOfDisplay(dpy);

    screens = XineramaQueryScreens(dpy, &num_screens);
    screens_x_allocated = 1;
    // Xinerama returns weird screen numbers, fix.
    for (i = 0; i < num_screens; ++i) {
      screens[i].screen_number = XScreenNumberOfScreen(screen);
    }
  } else {
    Screen* screen = XDefaultScreenOfDisplay(dpy);

    num_screens = 1;
    screens = (XineramaScreenInfo*)malloc(sizeof(XineramaScreenInfo));
    screens[0].screen_number = XScreenNumberOfScreen(screen);
    screens[0].x_org = 0;
    screens[0].y_org = 0;
    screens[0].width = XWidthOfScreen(screen);
    screens[0].height = XHeightOfScreen(screen);
    screens_x_allocated = 0;

    XFree(screen);
  }

  font = XLoadQueryFont(dpy, font_name);
  if (font == NULL) {
    fprintf(stderr, "Can not loat font %s.\n", font_name);
    exit(1);
  }
  font_id = XLoadFont(dpy, font_name);
}

void Done() {
  XFree(font);
  XUnloadFont(dpy, font_id);
  if (screens_x_allocated) {
    XFree(screens);
  } else {
    free(screens);
  }
  XCloseDisplay(dpy);
}

// 2 hands, 3 rows, 4 columns
const char Keys[24] = "qwerasdfzxcvuiopjkl;m,./";
int InvKeys[256];

void InitKeys() {
  int i;
  for (i = 0; i < 256; ++i) InvKeys[i] = -1;
  for (i = 0; i < 24; ++i) InvKeys[(int)Keys[i]] = i;
}

void DecodeKey(char a, int *hand, int *row, int *col) {
  int i = InvKeys[(int)a];
  if (i < 0) return;
  *hand = i / 12;
  i = i % 12;
  *row = i / 4;
  *col = i % 4;
}

int ValidTransition(char a, char b) {
  if (InvKeys[(int)a] < 0 || InvKeys[(int)b] < 0) return 0;
  int h1, r1, c1, h2, r2, c2;
  DecodeKey(a, &h1, &r1, &c1);
  DecodeKey(b, &h2, &r2, &c2);
  if (h1 != h2) return 1;
  if (r1 == r2) return 1;
  return 0;
}

void GenLabel(int idx, char *str) {
  static const int BASE = 16;  // number of valid transitions
  char last = 'm';
  int tmp[LABEL_LEN], i, j;

  for (i = LABEL_LEN - 1; i >=0; --i) {
    tmp[i] = idx % BASE;
    idx /= BASE;
  }
  str[LABEL_LEN] = 0;
  for (i = 0; i < LABEL_LEN; ++i) {
    for (j = 0; j < 24; ++j) {
      if (ValidTransition(last, Keys[j])) --tmp[i];
      if (tmp[i] < 0) break;
    }
    if (j == 24) { fprintf(stderr, "Internal error\n"); exit(1); }
    last = str[i] = Keys[j];
  }
}

// Create new unmanaged window:
// s: offset in screens
// x, y: origin relative to screen s
// w, h: width and height
Window UnmanagedWindow(int s, int x, int y, int w, int h) {
  XSetWindowAttributes attr;

  attr.override_redirect = 1;
  unsigned long valuemask = CWOverrideRedirect;
  return XCreateWindow(dpy, XRootWindow(dpy, screens[s].screen_number),
                       x + screens[s].x_org, y + screens[s].y_org, w, h,
                       False, CopyFromParent, InputOutput, CopyFromParent,
                       valuemask, &attr);
}

struct label {
  int x1, y1, x2, y2;
  char str[LABEL_LEN + 1];
};

struct screen_labels {
  Window win;
  Pixmap ctx, shape;
  GC win_gc, shape_gc;
  struct label* labels;
  int num_labels;
} *labels;

void MakeTextLabel(int s, int idx, int x, int y) {
  static const int border = 2;
  int dir, asc, desc, w, h;
  XCharStruct overall;
  char* text = labels[s].labels[idx].str;

  XTextExtents(font, text, strlen(text), &dir, &asc, &desc, &overall);
  w = overall.rbearing - overall.lbearing + 2*border;
  h = overall.ascent + overall.descent + 2*border;

  x -= w / 2;
  y -= h / 2;

  XSetForeground(dpy, labels[s].win_gc,
                 XBlackPixel(dpy, screens[s].screen_number));
  XSetBackground(dpy, labels[s].win_gc,
                 XBlackPixel(dpy, screens[s].screen_number));
  XFillRectangle(dpy, labels[s].ctx, labels[s].win_gc, x, y, w, h);

  XSetForeground(dpy, labels[s].win_gc,
                 XWhitePixel(dpy, screens[s].screen_number));
  XSetFont(dpy, labels[s].win_gc, font_id);
  XDrawImageString(dpy, labels[s].ctx, labels[s].win_gc,
                   x + border - overall.lbearing, y + border + overall.ascent,
                   text, strlen(text));

  labels[s].labels[idx].x1 = x;
  labels[s].labels[idx].y1 = y;
  labels[s].labels[idx].x2 = x + w;
  labels[s].labels[idx].y2 = y + h;

  XSetForeground(dpy, labels[s].shape_gc, 1);
  XFillRectangle(dpy, labels[s].shape, labels[s].shape_gc, x, y, w, h);
}

void InitWindows() {
  int i, j, k;
  int dir, asc, desc;
  int x_delta, y_delta;
  XCharStruct overall;
  char label[LABEL_LEN + 1];

  InitKeys();
  for (i = 0; i < LABEL_LEN; ++i) label[i] = 'X';
  label[LABEL_LEN] = 0;
  XTextExtents(font, label, LABEL_LEN, &dir, &asc, &desc, &overall);
  x_delta = overall.rbearing - overall.lbearing + 10;
  y_delta = overall.ascent - overall.descent + 10;

  labels = (struct screen_labels*)malloc(
      sizeof(struct screen_labels) * num_screens);
  for (i = 0; i < num_screens; ++i) {
    int x = screens[i].width / x_delta;
    int y = screens[i].height / y_delta;
    labels[i].win = UnmanagedWindow(i, 0, 0,
                                    screens[i].width, screens[i].height);
    labels[i].ctx = XCreatePixmap(dpy, labels[i].win,
                                  screens[i].width, screens[i].height,
                                  XDefaultDepth(dpy, screens[i].screen_number));
    labels[i].shape = XCreatePixmap(dpy, labels[i].win,
                                    screens[i].width, screens[i].height, 1);
    labels[i].win_gc = XCreateGC(dpy, labels[i].ctx, 0, NULL);
    labels[i].shape_gc = XCreateGC(dpy, labels[i].shape, 0, NULL);
    XSetForeground(dpy, labels[i].shape_gc, 0);
    XFillRectangle(dpy, labels[i].shape, labels[i].shape_gc, 0, 0,
                   screens[i].width, screens[i].height);
    labels[i].num_labels = x * y;
    labels[i].labels = (struct label*)malloc(sizeof(struct label) * x * y);
    for (j = 0; j < x; ++j) {
      for (k = 0; k < y; ++k) {
        int idx = k * x + j;
        GenLabel(idx, labels[i].labels[idx].str);
        MakeTextLabel(i, idx,
                      x_delta / 2 + x_delta * j, y_delta / 2 + y_delta * k);
      }
    }
  }
}

void DoneWindows() {
  int i;
  for (i = 0; i < num_screens; ++i) {
    free(labels[i].labels);
    XFreeGC(dpy, labels[i].win_gc);
    XFreeGC(dpy, labels[i].shape_gc);
    XFreePixmap(dpy, labels[i].ctx);
    XFreePixmap(dpy, labels[i].shape);
    XDestroyWindow(dpy, labels[i].win);
  }
}

int FindScreen() {
  Window root, child;
  int i, x, y, wx, wy;
  unsigned int mask;
  for (i = 0; i < num_screens; ++i) {
    if (XQueryPointer(dpy, XRootWindow(dpy, screens[i].screen_number),
                      &root, &child, &x, &y, &wx, &wy, &mask)) {
      if (x >= screens[i].x_org && x < screens[i].x_org + screens[i].width &&
          y >= screens[i].y_org && y < screens[i].y_org + screens[i].height) {
        return i;
      }
    }
  }
  return 0;
}

//
struct StatusWin {
  Window win;
  GC gc;
  int w, h, border;
} inp;

void InitStatusWin() {
  int dir, asc, desc;
  XCharStruct overall;

  XTextExtents(font, "X", 1, &dir, &asc, &desc, &overall);
  inp.border = 4;
  inp.w = overall.rbearing - overall.lbearing + 2*inp.border;
  inp.h = overall.ascent + overall.descent + 2*inp.border;

  inp.win = UnmanagedWindow(s, 0, 0, inp.w, inp.h);
  inp.gc = XCreateGC(dpy, inp.win, 0, NULL);
  XMapRaised(dpy, inp.win);
}

void DrawStatusWin() {
  int dir, asc, desc, w, x, y;
  XCharStruct overall;
  char str[8];
  
  if (drag) strcpy(str, "D");
  else strcpy(str, "M");

  XTextExtents(font, str, strlen(str), &dir, &asc, &desc, &overall);
  w = overall.rbearing - overall.lbearing;

  x = (inp.w - w) / 2;
  y = inp.border + overall.ascent;

  XSetForeground(dpy, inp.gc, XBlackPixel(dpy, screens[s].screen_number));
  XSetBackground(dpy, inp.gc, XBlackPixel(dpy, screens[s].screen_number));
  XFillRectangle(dpy, inp.win, inp.gc, 0, 0, inp.w, inp.h);

  XSetForeground(dpy, inp.gc, XWhitePixel(dpy, screens[s].screen_number));
  XSetFont(dpy, inp.gc, font_id);
  XDrawImageString(dpy, inp.win, inp.gc, x, y, str, strlen(str));
}

void DoneStatusWin() {
  XFreeGC(dpy, inp.gc);
  XDestroyWindow(dpy, inp.win);
}

//
void InitState() {
  XMapRaised(dpy, labels[s].win);
  pfx_idx = 0;
  done = 0;
  drag = 0;
}

void Grab() {
  while (XGrabKeyboard(dpy, inp.win, False, GrabModeAsync, GrabModeAsync,
                       CurrentTime)) {
    usleep(10000);
  }
}

void Ungrab() {
  XUngrabKeyboard(dpy, CurrentTime);
  XSync(dpy, False);
}

void Mouse(int btn, Bool down) {
  XTestFakeButtonEvent(dpy, btn, down, CurrentTime);
}

int HandleKeyPress(XKeyEvent* ev) {
  int l = 1;
  if (ev->state & ShiftMask) l = 6;

  if (pfx_idx == -1) {
    switch (XLookupKeysym(ev, 0)) {
      case XK_h:
        XWarpPointer(dpy, None, None, 0, 0, 0, 0, -l, 0);
        break;
      case XK_j:
        XWarpPointer(dpy, None, None, 0, 0, 0, 0, 0, l+1);
        break;
      case XK_k:
        XWarpPointer(dpy, None, None, 0, 0, 0, 0, 0, -l);
        break;
      case XK_l:
        XWarpPointer(dpy, None, None, 0, 0, 0, 0, l+1, 0);
        break;
      case XK_q:
      case XK_Return:
      case XK_Escape:
        done = 1;
        break;
      case XK_Tab:
        s = (s + 1) % num_screens;
        break;
      case XK_m:
        XMapRaised(dpy, labels[s].win);
        pfx_idx = 0;
        break;
      case XK_c:
        if (!drag) {
          if (ev->state & ShiftMask) {
            Ungrab();
            drag = 1;
            done = 1;
          }
          Mouse(1, True);
        }
        break;
      case XK_e:
        if (ev->state & ShiftMask) {
          Ungrab();
          drag = 2;
          done = 1;
        }
        Mouse(2, True);
        break;
      case XK_r:
        if (ev->state & ShiftMask) {
          Ungrab();
          drag = 3;
          done = 1;
        }
        Mouse(3, True);
        break;
      case XK_w:  // wheel up
        Mouse(4, True);
        break;
      case XK_s:  // wheel down
        Mouse(5, True);
        break;
      case XK_d:
        if (!drag) {
          XTestFakeButtonEvent(dpy, 1, True, CurrentTime);
          drag = 1;
        } else {
          XTestFakeButtonEvent(dpy, 1, False, CurrentTime);
          drag = 0;
        }
        DrawStatusWin();
        break;
      default:
        return 0;
    }
  } else {
    char c;
    switch (XLookupKeysym(ev, 0)) {
      case XK_q:         c = 'q'; break;
      case XK_w:         c = 'w'; break;
      case XK_e:         c = 'e'; break;
      case XK_r:         c = 'r'; break;
      case XK_a:         c = 'a'; break;
      case XK_s:         c = 's'; break;
      case XK_d:         c = 'd'; break;
      case XK_f:         c = 'f'; break;
      case XK_z:         c = 'z'; break;
      case XK_x:         c = 'x'; break;
      case XK_c:         c = 'c'; break;
      case XK_v:         c = 'v'; break;
      case XK_u:         c = 'u'; break;
      case XK_i:         c = 'i'; break;
      case XK_o:         c = 'o'; break;
      case XK_p:         c = 'p'; break;
      case XK_j:         c = 'j'; break;
      case XK_k:         c = 'k'; break;
      case XK_l:         c = 'l'; break;
      case XK_semicolon: c = ';'; break;
      case XK_m:         c = 'm'; break;
      case XK_comma:     c = ','; break;
      case XK_period:    c = '.'; break;
      case XK_slash:     c = '/'; break;
      case XK_Tab:
        XUnmapWindow(dpy, labels[s].win);
        s = (s + 1) % num_screens;
        XMapRaised(dpy, labels[s].win);
        return 1;
        break;
      case XK_Escape:
        XUnmapWindow(dpy, labels[s].win);
        pfx_idx = -1;
        return 1;
        break;
      default: return 0;
    }
    pfx[pfx_idx++] = c;
    if (pfx_idx == LABEL_LEN) {
      int x, y, i;
      x = y = 0;
      for (i = 0; i < labels[s].num_labels; ++i) {
        if (strcmp(labels[s].labels[i].str, pfx) == 0) {
          x = (labels[s].labels[i].x1 + labels[s].labels[i].x2) / 2;
          y = (labels[s].labels[i].y1 + labels[s].labels[i].y2) / 2;
          break;
        }
      }

      pfx_idx = -1;
      XWarpPointer(dpy, None, labels[s].win, 0, 0, 0, 0, x, y);
      XUnmapWindow(dpy, labels[s].win);
    }
  }
  return 1;
}

int HandleKeyRelease(XKeyEvent* ev) {
  if (pfx_idx == -1) {
    switch (XLookupKeysym(ev, 0)) {
      case XK_h:
      case XK_j:
      case XK_k:
      case XK_l:
      case XK_q:
      case XK_Return:
      case XK_Escape:
      case XK_m:
      case XK_d:
        break;
      case XK_c:
        if (!drag) Mouse(1, False);
        break;
      case XK_e:
        Mouse(2, False);
        break;
      case XK_r:
        Mouse(3, False);
        break;
      case XK_w:  // wheel up
        Mouse(4, False);
        break;
      case XK_s:  // wheel down
        Mouse(5, False);
        break;
      default:
        return 0;
    }
  } else {
    switch (XLookupKeysym(ev, 0)) {
      case XK_q:
      case XK_w:
      case XK_e:
      case XK_r:
      case XK_a:
      case XK_s:
      case XK_d:
      case XK_f:
      case XK_z:
      case XK_x:
      case XK_c:
      case XK_v:
      case XK_u:
      case XK_i:
      case XK_o:
      case XK_p:
      case XK_j:
      case XK_k:
      case XK_l:
      case XK_semicolon:
      case XK_m:
      case XK_comma:
      case XK_period:
      case XK_slash:
      case XK_Escape:
        break;
      default: return 0;
    }
  }
  return 1;
}

int main() {
  int i;

  Init();
  InitWindows();
  s = FindScreen();
  InitStatusWin();
  XSelectInput(dpy, inp.win, KeyPressMask | ExposureMask);

  for (i = 0; i < num_screens; ++i) {
    XSelectInput(dpy, labels[i].win, ExposureMask);
  }

  InitState(s);
  Grab();

  while (!done) {
    XEvent ev;
    XNextEvent(dpy, &ev);
    int handled = 0;
    switch (ev.type) {
      case Expose:
        if (ev.xexpose.window == inp.win) {
          DrawStatusWin();
        } else {
          XCopyArea(dpy, labels[s].ctx, labels[s].win, labels[s].win_gc,
                    0, 0, screens[s].width, screens[s].height, 0, 0);
          XShapeCombineMask(dpy, labels[s].win, ShapeBounding, 0, 0,
                            labels[s].shape, ShapeSet);
        }
        handled = 1;
        break;
      case KeyPress:
        handled = HandleKeyPress(&ev.xkey);
        break;
      case KeyRelease:
        handled = HandleKeyRelease(&ev.xkey);
        break;
    }
    /*
    if (!handled) {
      printf("allow event\n");
      XAllowEvents(dpy, ReplayKeyboard, ev.xkey.time);
      XFlush(dpy);
    }
    */
  }
  if (drag) {
    Mouse(drag, False);
  }
  Ungrab();

  DoneWindows();
  Done();
  return 0;
}
