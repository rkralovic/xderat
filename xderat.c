#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/shape.h>
#include <X11/Xlib.h>

#define LOG(x...) fprintf(stderr, x...)

////////////////
// X state:
Display* dpy;
XineramaScreenInfo* screens;
int num_screens;

XFontStruct* font;
Font font_id;

const char* font_name = "terminus-14";
#define LABEL_LEN 3

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
    screens = XineramaQueryScreens(dpy, &num_screens);
    screens_x_allocated = 1;
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
  for (i = 0; i < 24; ++i) InvKeys[Keys[i]] = i;
}

void DecodeKey(char a, int *hand, int *row, int *col) {
  int i = InvKeys[a];
  if (i < 0) return;
  *hand = i / 12;
  i = i % 12;
  *row = i / 4;
  *col = i % 4;
}

int ValidTransition(char a, char b) {
  if (InvKeys[a] < 0 || InvKeys[b] < 0) return 0;
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

  XSetForeground(dpy, labels[s].win_gc, XWhitePixel(dpy, screens[s].screen_number));
  XSetBackground(dpy, labels[s].win_gc, XBlackPixel(dpy, screens[s].screen_number));
  XSetFont(dpy, labels[s].win_gc, font_id);
  XDrawImageString(dpy, labels[s].ctx, labels[s].win_gc,
                   x + border - overall.lbearing, y + border + overall.ascent,
                   text, strlen(text));

  labels[s].labels[idx].x1 = x;
  labels[s].labels[idx].y1 = y;
  labels[s].labels[idx].x2 = x + w;
  labels[s].labels[idx].y2 = y + w;

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
  x_delta = overall.rbearing - overall.lbearing + 6;
  y_delta = overall.ascent - overall.descent + 6;

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

int main() {
  Init();

  {
    int i;
    for (i = 0; i < num_screens; ++i) {
      printf("screen %d: [%d, %d], [%d, %d]\n",
             screens[i].screen_number, screens[i].x_org, screens[i].y_org,
             screens[i].width, screens[i].height);
    }
  }

  InitWindows();

  {
    int x;
    scanf("%d", &x);
    for (x = 0; x < num_screens; ++x) {
      XMapWindow(dpy, labels[x].win);
      XCopyArea(dpy, labels[x].ctx, labels[x].win, labels[x].win_gc,
                0, 0, screens[x].width, screens[x].height, 0, 0);
      XShapeCombineMask(dpy, labels[x].win, ShapeBounding, 0, 0,
                        labels[x].shape, ShapeSet);
    }

    XSync(dpy, False);
    scanf("%d", &x);
  }

  DoneWindows();
  Done();
  return 0;
}
