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
using namespace std;


static Display *dpy;
static int scr;
static Window root;
static const int ROWS = 24; // visible lines

#define POSX 500
#define POSY 500
#define WIDTH 500
#define HEIGHT 500
#define BORDER 15

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

    const string promptPrefix = "shre@Term:"; // your prompt prefix

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
