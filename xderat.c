#include <stdio.h>
#include <stdlib.h>
#include <X11/extensions/Xinerama.h>
#include <X11/Xlib.h>

#define LOG(x...) fprintf(stderr, x...)

////////////////
// X state:
Display* dpy;
XineramaScreenInfo* screens;
int num_screens;

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
}

void Done() {
  if (screens_x_allocated) {
    XFree(screens);
  } else {
    free(screens);
  }
  XCloseDisplay(dpy);
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

  Done();
  return 0;
}
