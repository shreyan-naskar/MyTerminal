#include "headers.cpp"
#include "run.cpp"

using namespace std;

int main(int argc, char** argv)
{
    string par_dir = argv[1];
    len = int(par_dir.size());

    Window win;

    disp = XOpenDisplay(NULL);
    if (disp == NULL)
    {
        errx(1, "Cant open display");
    }
    inputs = getHistory();

    scr = DefaultScreen(disp);
    root = RootWindow(disp, scr);
    
    win = makeWindow(POSX, POSY, WIDTH, HEIGHT, BORDER);
    XStoreName(disp, win, "shreTerm");
    run(win);

    XUnmapWindow(disp, win);
    XDestroyWindow(disp, win);
    cout<<"RUNNING"<<endl;
    return argc;
}