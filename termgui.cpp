#include "headers.cpp"

// include files for functionality
#include "run.cpp"

using namespace std;

int main(int argc, char** argv)
{
    string par_dir = argv[1];
    len = int(par_dir.size());

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