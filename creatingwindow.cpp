#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdio>
#include <err.h>
#include <string>
#include <iostream>
#include <chrono>
#include <vector>
#include <bits/stdc++.h>
#include <unistd.h>
#include <cctype>
#include <regex>
static Display *dpy;
static int scr;
static Window root;
static const int ROWS = 24; // visible lines

#define POSX 500
#define POSY 500
#define WIDTH 500
#define HEIGHT 500
#define BORDER 15
using namespace std;


string stripANSI(const string &s)
{
    static const regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
    return regex_replace(s, ansi, "");
}


vector<string> execCommand(const string &cmd)
{
    if (cmd.empty()) return {""};

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {"Error: failed to run command\n"};

    string result;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;

    pclose(pipe);

    // Remove ANSI sequences
    result = stripANSI(result);

    // Keep only printable + newline
    vector<string> cleaned;
    string clean = "";
    for (char c : result)
    {
        if (c == '\n' || c == '\r' || (c >= 32 && c < 127))
        {   
            if (c == '\r'){
                clean.push_back(' ');
                continue;
            }
            else if(c == '\n')
            {
                cleaned.push_back(clean);
                clean = "";
                continue;
            }
            clean.push_back(c);
        }
    }
    cleaned.push_back(clean);

    return cleaned;
}

string getPWD()
{
    char cwd[512];
    string currentDir;
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
    {
        currentDir = string(cwd);
    }
    return currentDir.substr(15);
}

static Window create_window(int x, int y, int h, int w, int b)
{
    Window win;
    XSetWindowAttributes xwa;

    xwa.background_pixel = BlackPixel(dpy, scr);
    xwa.border_pixel = WhitePixel(dpy, scr);
    xwa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask;

    win = XCreateWindow(dpy, root, x, y, w, h, b, DefaultDepth(dpy, scr), InputOutput, DefaultVisual(dpy, scr), CWBackPixel | CWBorderPixel | CWEventMask, &xwa);
    return win;
}


static void drawScreen(Window win, GC gc, XFontStruct *font,
                       vector<string> &screenBuffer, bool showCursor)
{
    XClearWindow(dpy, win);
    int lineHeight = font->ascent + font->descent;

    // window metrics / margins
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    int winWidth = attrs.width;
    int marginLeft = 10;
    int marginTop = 30;

    // allocate colors once
    static bool colorsInit = false;
    static unsigned long greenPixel = WhitePixel(dpy, scr);
    static unsigned long whitePixel = WhitePixel(dpy, scr);
    if (!colorsInit)
    {
        Colormap colormap = DefaultColormap(dpy, scr);
        XColor green, white, exact;
        if (XAllocNamedColor(dpy, colormap, "green", &green, &exact))
            greenPixel = green.pixel;
        else
            greenPixel = WhitePixel(dpy, scr);

        if (XAllocNamedColor(dpy, colormap, "white", &white, &exact))
            whitePixel = white.pixel;
        else
            whitePixel = WhitePixel(dpy, scr);

        colorsInit = true;
    }

    const string promptPrefix = "shre@Term:~"; // include trailing space!

    // We'll build a vector of display lines: each has the substring to draw and
    // how many initial chars of that substring (if any) belong to the prompt.
    struct DisplayLine { string text; int promptChars; };
    vector<DisplayLine> displayLines;

    // Create wrapped display lines per original screenBuffer line
    for (auto &origLine : screenBuffer)
    {
        if (origLine.empty())
        {
            displayLines.push_back({"", 0});
            continue;
        }

        size_t pos = 0;
        while (pos < origLine.size())
        {
            int curWidth = 0;
            size_t start = pos;
            size_t len = 0;

            // greedy accumulate chars until we hit the available width
            for (; pos < origLine.size(); ++pos)
            {
                char c = origLine[pos];
                int cw = XTextWidth(font, &c, 1);
                if (curWidth + cw > winWidth - marginLeft - 10)
                    break;
                curWidth += cw;
                ++len;
            }

            // if len == 0, force one char (avoid infinite loop on extremely narrow windows)
            if (len == 0)
            {
                ++pos;
                ++len;
            }

            string piece = origLine.substr(start, len);
            int promptCharsInPiece = 0;

            // If this is the start of the original line, and that original line begins
            // with the promptPrefix, mark how many chars of this piece belong to prompt
            if (start == 0 && origLine.rfind(promptPrefix, 0) == 0)
            {
                promptCharsInPiece = (int)min<size_t>(len, promptPrefix.size());
            }

            displayLines.push_back({piece, promptCharsInPiece});
        }
    }

    // Draw displayLines with appropriate colors (prompt portion green, rest white)
    for (size_t row = 0; row < displayLines.size(); ++row)
    {
        int y = marginTop + row * lineHeight;
        int x = marginLeft;
        const DisplayLine &dl = displayLines[row];

        if (dl.promptChars > 0)
        {
            // draw prompt part in green
            string ppart = dl.text.substr(0, dl.promptChars);
            XSetForeground(dpy, gc, greenPixel);
            XDrawString(dpy, win, gc, x, y, ppart.c_str(), (int)ppart.length());
            x += XTextWidth(font, ppart.c_str(), (int)ppart.length());

            // draw remainder in white
            string rpart = dl.text.substr(dl.promptChars);
            if (!rpart.empty())
            {
                XSetForeground(dpy, gc, whitePixel);
                XDrawString(dpy, win, gc, x, y, rpart.c_str(), (int)rpart.length());
            }
        }
        else
        {
            // whole line in white
            XSetForeground(dpy, gc, whitePixel);
            XDrawString(dpy, win, gc, x, y, dl.text.c_str(), (int)dl.text.length());
        }
    }

    // Draw cursor at the end of the last displayed line
    if (showCursor && !displayLines.empty())
    {
        const DisplayLine &last = displayLines.back();
        int pxWidth = XTextWidth(font, last.text.c_str(), (int)last.text.length());
        int cursorX = marginLeft + pxWidth;
        int row = (int)displayLines.size() - 1;
        int yTop = marginTop + row * lineHeight - font->ascent;
        int yBottom = marginTop + row * lineHeight + font->descent;

        // keep cursor white for visibility
        XSetForeground(dpy, gc, whitePixel);
        XDrawLine(dpy, win, gc, cursorX, yTop, cursorX, yBottom);
    }
}


static void run(Window win)
{
    XSelectInput(dpy, win, ExposureMask | KeyPressMask);
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim)
    {
        std::cerr << "XOpenIM failed\n";
        return;
    }

    XIC xic = XCreateIC(xim,
                        XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win,
                        XNFocusWindow, win,
                        nullptr);

    if (!xic)
    {
        std::cerr << "XCreateIC failed\n";
        return;
    }

    XFontStruct *font = XLoadQueryFont(dpy, "12x24");
    if (!font)
        font = XLoadQueryFont(dpy, "12x24"); // fallback
    if (!font)
        font = XLoadQueryFont(dpy, "fixed");
    vector<string> screenBuffer;
    screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");

    GC gc = XCreateGC(dpy, win, 0, nullptr);
    XSetFont(dpy, gc, font->fid);

    XColor greenColor, exactColor;
    Colormap colormap = DefaultColormap(dpy, scr);

    if (XAllocNamedColor(dpy, colormap, "green", &greenColor, &exactColor))
        XSetForeground(dpy, gc, greenColor.pixel);
    else
        XSetForeground(dpy, gc, WhitePixel(dpy, scr)); // fallback

    XMapWindow(dpy, win);

    bool running = true;
    string typedText = "";
    bool showCursor = true;
    auto lastBlink = std::chrono::steady_clock::now();

    XEvent event;
    string input;
    while (running)
    {
        // If events are pending, handle them

        if (XPending(dpy) > 0)
        {
            XNextEvent(dpy, &event);

            switch (event.type)
            {
            case Expose:
                drawScreen(win, gc, font, screenBuffer, showCursor);
                break;

            case KeyPress:
            {
                wchar_t wbuf[32];
                KeySym keysym;
                Status status;
                int len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);

                if (status == XLookupChars || status == XLookupBoth)
                {
                    if (len > 0)
                    {
                        if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                        {
                            // ENTER → new line with prompt
                            vector<string> outputs = execCommand(input);
                            input.clear();
                            for( auto &output: outputs)
                            {
                                screenBuffer.push_back(output);
                            }
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
                            if ((int)screenBuffer.size() > ROWS)
                                screenBuffer.erase(screenBuffer.begin()); // scroll
                        }
                        else if (wbuf[0] == 8 || wbuf[0] == 127)
                        {
                            string curr = "shre@Term:~" + getPWD() + "$ ";
                            // BACKSPACE → delete but not prompt
                            if (!screenBuffer.empty() &&
                                screenBuffer.back().size() > curr.size())
                            {
                                screenBuffer.back().pop_back();
                                input.pop_back();
                            }
                        }
                        else
                        {
                            // Normal character
                            if (screenBuffer.empty())
                                screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
                            cout << wbuf[0];
                            if (wbuf[0] < 128) // only ASCII
                            {
                                input.push_back((char)wbuf[0]);
                                screenBuffer.back().push_back((char)wbuf[0]);
                            }
                        }
                    }
                }

                if (keysym == XK_Escape)
                {
                    running = false;
                }

                drawScreen(win, gc, font, screenBuffer, showCursor);
                break;
            }
            }
        }

        // Blink cursor every 500 ms
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() > 500)
        {
            showCursor = !showCursor;
            lastBlink = now;
            drawScreen(win, gc, font, screenBuffer, showCursor);
        }
    }
}
int main()
{
    Window win;

    dpy = XOpenDisplay(NULL);
    if (dpy == NULL)
    {
        errx(1, "Cant open display");
    }

    scr = DefaultScreen(dpy);
    root = RootWindow(dpy, scr);
    win = create_window(POSX, POSY, WIDTH, HEIGHT, BORDER);
    XStoreName(dpy, win, "shreTerm");
    chdir("/home/shreyan10");
    run(win);

    XUnmapWindow(dpy, win);
    XDestroyWindow(dpy, win);
    return 0;
}