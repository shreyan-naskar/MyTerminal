#include "headers.cpp"

// include files for functionality
#include "run.cpp"

using namespace std;

int main()
{
    Window win;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        errx(1, "Cant open display");
    }
    inputs = loadInputs();
    // reverse(inputs.begin(), inputs.begin());

    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    
    win = create_window(POSX, POSY, WIDTH, HEIGHT, BORDER);
    XStoreName(dpy, win, "shreTerm");
    // chdir("/");
    run(win);

    XUnmapWindow(dpy, win);
    XDestroyWindow(dpy, win);
    return 0;
}