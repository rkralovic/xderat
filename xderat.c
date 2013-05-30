#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/extensions/Xinerama.h>
#include <X11/Xlib.h>

#define LOG(x...) fprintf(stderr, x...)

////////////////
// X state:
Display* dpy;
XineramaScreenInfo* screens;
int num_screens;

XFontStruct* font;
Font font_id;

GC TextGC[1];
GC BarGC;
int NumTextGC() {
  return sizeof(TextGC) / sizeof(GC);
}

const char* font_name = "terminus-14";
#define LABEL_LEN 4

// private
int screens_x_allocated;

// methods
void Init() {
  XGCValues gc_val;
  unsigned long gc_mask;

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

  gc_mask = GCForeground | GCBackground | GCFont;
  gc_val.foreground = XWhitePixel(dpy, XDefaultScreen(dpy));
  gc_val.background = XBlackPixel(dpy, XDefaultScreen(dpy));
  gc_val.font = font_id;
  TextGC[0] = XCreateGC(dpy, XDefaultRootWindow(dpy), gc_mask, &gc_val);
  BarGC = XCreateGC(dpy, XDefaultRootWindow(dpy), gc_mask, &gc_val);
}

void Done() {
  int i;
  XFreeGC(dpy, BarGC);
  for (i = 0; i < NumTextGC(); ++i) {
    XFreeGC(dpy, TextGC[i]);
  }
  XFree(font);
  XUnloadFont(dpy, font_id);
  if (screens_x_allocated) {
    XFree(screens);
  } else {
    free(screens);
  }
  XCloseDisplay(dpy);
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

Window TextWindow(int s, int x, int y, int gc, char* text) {
  static const int border = 2;
  Window win;
  int dir, asc, desc, w, h;
  XCharStruct overall;
  XGCValues gc_val;

  XTextExtents(font, text, strlen(text), &dir, &asc, &desc, &overall);
  w = overall.rbearing - overall.lbearing + 2*border;
  h = overall.ascent + overall.descent + 2*border;

  win = UnmanagedWindow(s, x - w / 2, y - h / 2, w, h);

  XGetGCValues(dpy, TextGC[gc], GCBackground, &gc_val);
  XSetWindowBackground(dpy, win, gc_val.background);

  XMapRaised(dpy, win);

  XDrawImageString(dpy, win, TextGC[gc],
                   border - overall.lbearing, border + overall.ascent,
                   text, strlen(text));

  return win;
}

void Sort2(int* a, int* b) {
  if (*a > *b) {
    int c = *a;
    *a = *b;
    *b = c;
  }
}

Window BarWindow(int s, int x1, int y1, int x2, int y2) {
  static const int border = 1;
  Window win;
  XGCValues gc_val;

  Sort2(&x1, &x2);
  Sort2(&y1, &y2);
  win = UnmanagedWindow(s, x1 - border, y1 - border,
                        x2 - x1 + 1 + 2*border, y2 - y1 + 1 + 2*border);

  XGetGCValues(dpy, BarGC, GCBackground, &gc_val);
  XSetWindowBackground(dpy, win, gc_val.background);

  XMapRaised(dpy, win);

  XFillRectangle(dpy, win, BarGC, border, border, x2 - x1 + 1, y2 - y1 + 1);

  return win;
}

struct label_win {
  Window win;
  char label[LABEL_LEN + 1];
  int mapped;
};

struct screen_labels {
  struct label_win* labels;
  int num_labels;
} *labels;

const char Keys[] = "";

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

  {
    int x;
    scanf("%d", &x);
  }

  {
    int i, j, k;
    int dir, asc, desc;
    int x_delta, y_delta;
    XCharStruct overall;
    char label[LABEL_LEN + 1];

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
      labels[i].num_labels = x * y;
      labels[i].labels = (struct label_win *)malloc(
          sizeof(struct label_win) * x * y);
      for (j = 0; j < x; ++j) {
        for (k = 0; k < y; ++k) {
          int idx = k * x + j;
          strcpy(labels[i].labels[idx].label, "XXXX");
          labels[i].labels[idx].win =
            TextWindow(i, x_delta / 2 + x_delta * j, y_delta / 2 + y_delta * k,
                       0, labels[i].labels[idx].label);
        }
      }

    }
  }
  
  {
    /*
    int i, j;
    Window w[16][16];
    Window w2[2][16];
    for (i = 0; i < 16; ++i) {
        w2[0][1] = BarWindow(0, 35 + 50 * i, 30, 35 + 50 * i, 350);
      for (j = 0; j < 16; ++j) {
        w[i][j] = TextWindow(0, 10 + 50*i, 40 + 20 * j, 0, "ab");
      }
    }
    */
    XSync(dpy, False);
    int x;
    scanf("%d", &x);
  }

  {
    int i;
    for (i = 0; i < num_screens; ++i) {
      free(labels[i].labels);
    }
    free(labels);
  }
  Done();
  return 0;
}
