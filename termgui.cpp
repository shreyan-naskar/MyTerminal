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

// include files for functionality
#include "excComm.cpp"

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

// returns total number of display lines (after wrapping)
static int drawScreen(Window win, GC gc, XFontStruct *font,
                      vector<string> &screenBuffer, bool showCursor, int scrollOffset)
{
    XClearWindow(dpy, win);
    int lineHeight = font->ascent + font->descent;

    // window metrics / margins
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    int winWidth = attrs.width;
    int winHeight = attrs.height;
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

    const string promptPrefix = "shre@Term:~"; // your prompt prefix

    // Build wrapped display lines as before
    struct DisplayLine { string text; int promptChars; };
    vector<DisplayLine> displayLines;

    for (const auto &origLine : screenBuffer)
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

            for (; pos < origLine.size(); ++pos)
            {
                char c = origLine[pos];
                int cw = XTextWidth(font, &c, 1);
                if (curWidth + cw > winWidth - marginLeft - 10)
                    break;
                curWidth += cw;
                ++len;
            }

            if (len == 0) { ++pos; ++len; }

            string piece = origLine.substr(start, len);
            int promptCharsInPiece = 0;
            if (start == 0 && origLine.rfind(promptPrefix, 0) == 0)
                promptCharsInPiece = (int)min<size_t>(len, promptPrefix.size());

            displayLines.push_back({piece, promptCharsInPiece});
        }
    }

    // determine how many display lines fit vertically
    int visibleRows = max(1, (winHeight - marginTop) / lineHeight);

    // clamp scrollOffset to valid range
    int totalLines = (int)displayLines.size();
    if (scrollOffset < 0) scrollOffset = 0;
    if (scrollOffset > max(0, totalLines - visibleRows)) scrollOffset = max(0, totalLines - visibleRows);

    // draw only visible window: from scrollOffset .. scrollOffset + visibleRows - 1
    int start = scrollOffset;
    int end = min(totalLines, scrollOffset + visibleRows);

    for (int row = start; row < end; ++row)
    {
        int y = marginTop + (row - start) * lineHeight;
        int x = marginLeft;
        const DisplayLine &dl = displayLines[row];

        if (dl.promptChars > 0)
        {
            string ppart = dl.text.substr(0, dl.promptChars);
            XSetForeground(dpy, gc, greenPixel);
            XDrawString(dpy, win, gc, x, y, ppart.c_str(), (int)ppart.length());
            x += XTextWidth(font, ppart.c_str(), (int)ppart.length());

            string rpart = dl.text.substr(dl.promptChars);
            if (!rpart.empty())
            {
                XSetForeground(dpy, gc, whitePixel);
                XDrawString(dpy, win, gc, x, y, rpart.c_str(), (int)rpart.length());
            }
        }
        else
        {
            XSetForeground(dpy, gc, whitePixel);
            XDrawString(dpy, win, gc, x, y, dl.text.c_str(), (int)dl.text.length());
        }
    }

    // Draw cursor — compute its absolute display-line index (last line)
    if (showCursor && !displayLines.empty())
    {
        int cursorLineIndex = totalLines - 1;            // last display line index
        if (cursorLineIndex >= start && cursorLineIndex < end)
        {
            const DisplayLine &last = displayLines[cursorLineIndex];
            int pxWidth = XTextWidth(font, last.text.c_str(), (int)last.text.length());
            int cursorX = marginLeft + pxWidth;
            int row = cursorLineIndex - start;
            int yTop = marginTop + row * lineHeight - font->ascent;
            int yBottom = marginTop + row * lineHeight + font->descent;

            XSetForeground(dpy, gc, whitePixel);
            XDrawLine(dpy, win, gc, cursorX, yTop, cursorX, yBottom);
        }
    }

    return totalLines;
}

static const int SCROLL_STEP = 3; // lines per wheel/page step

static void run(Window win)
{
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);

    // --- XIM/XIC setup ---
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim) std::cerr << "XOpenIM failed — continuing without input method\n";

    XIC xic = nullptr;
    if (xim) {
        xic = XCreateIC(xim,
                        XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win,
                        XNFocusWindow, win,
                        nullptr);
        if (!xic) std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XFontStruct *font = XLoadQueryFont(dpy, "12x24");
    if (!font) font = XLoadQueryFont(dpy, "fixed");

    vector<string> screenBuffer;
    screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");

    GC gc = XCreateGC(dpy, win, 0, nullptr);
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, WhitePixel(dpy, scr)); // default

    XMapWindow(dpy, win);

    bool running = true;
    bool showCursor = true;
    auto lastBlink = std::chrono::steady_clock::now();
    XEvent event;
    string input;

    int scrollOffset = 0;
    bool userScrolled = false;

    int totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);

    while (running)
    {
        if (XPending(dpy) > 0)
        {
            XNextEvent(dpy, &event);

            switch (event.type)
            {
                case Expose:
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;

                case ButtonPress:
                {
                    XWindowAttributes attrs;
                    XGetWindowAttributes(dpy, win, &attrs);
                    int lineHeight = font->ascent + font->descent;
                    int visibleRows = max(1, (attrs.height - 30) / lineHeight);

                    if (event.xbutton.button == Button4) { // wheel up
                        scrollOffset = max(0, scrollOffset - SCROLL_STEP);
                        userScrolled = true;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    else if (event.xbutton.button == Button5) { // wheel down
                        scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                           scrollOffset + SCROLL_STEP);
                        if (scrollOffset >= max(0, totalDisplayLines - visibleRows)) userScrolled = false;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    break;
                }

                case KeyPress:
                {
                    wchar_t wbuf[32];
                    KeySym keysym = 0;
                    Status status = 0;
                    int len = 0;

                    if (xic) {
                        len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);
                    } else {
                        // Fallback: ignore input if XIC not available
                        len = 0;
                        keysym = event.xkey.keycode; // basic handling
                    }

                    XWindowAttributes attrs;
                    XGetWindowAttributes(dpy, win, &attrs);
                    int lineHeight = font->ascent + font->descent;
                    int visibleRows = max(1, (attrs.height - 30) / lineHeight);

                    // --- Scroll keys ---
                    if (keysym == XK_Page_Up) {
                        scrollOffset = max(0, scrollOffset - SCROLL_STEP * 5);
                        userScrolled = true;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        break;
                    }
                    else if (keysym == XK_Page_Down) {
                        scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                           scrollOffset + SCROLL_STEP * 5);
                        if (scrollOffset >= max(0, totalDisplayLines - visibleRows)) userScrolled = false;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        break;
                    }
                    else if (keysym == XK_Home) {
                        scrollOffset = 0;
                        userScrolled = true;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        break;
                    }
                    else if (keysym == XK_End) {
                        scrollOffset = max(0, totalDisplayLines - visibleRows);
                        userScrolled = false;
                        drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        break;
                    }

                    // --- Normal character handling ---
                    if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                    {
                        if (wbuf[0] == L'\r' || wbuf[0] == L'\n') {
                            vector<string> outputs = execCommand(input); // returns vector<string>
                            input.clear();
                            for (const auto &line : outputs) screenBuffer.push_back(line);
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");

                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);

                            if (!userScrolled) {
                                scrollOffset = max(0, totalDisplayLines - visibleRows);
                                drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                            }
                        }
                        else if (wbuf[0] == 8 || wbuf[0] == 127) { // backspace
                            string prompt = "shre@Term:~" + getPWD() + "$ ";
                            if (!screenBuffer.empty() && screenBuffer.back().size() > prompt.size()) {
                                screenBuffer.back().pop_back();
                                if (!input.empty()) input.pop_back();
                            }
                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        }
                        else { // normal char
                            if (screenBuffer.empty()) screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
                            if (wbuf[0] < 128) {
                                input.push_back((char)wbuf[0]);
                                screenBuffer.back().push_back((char)wbuf[0]);
                            }
                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        }
                    }

                    if (keysym == XK_Escape) running = false;

                    break;
                } // KeyPress
            } // switch
        } // XPending

        // --- Blink cursor ---
        auto now = chrono::steady_clock::now();
        if (chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() > 500)
        {
            showCursor = !showCursor;
            lastBlink = now;
            drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
        }
    }

    if (xic) XDestroyIC(xic);
    if (xim) XCloseIM(xim);
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