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
#include "exec.cpp"
#include "draw.cpp"

static const int SCROLL_STEP = 3; // lines per wheel/page step

static void run(Window win)
{
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);

    // --- XIM/XIC setup ---
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim)
        std::cerr << "XOpenIM failed — continuing without input method\n";

    XIC xic = nullptr;
    if (xim)
    {
        xic = XCreateIC(xim,
                        XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win,
                        XNFocusWindow, win,
                        nullptr);
        if (!xic)
            std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XFontStruct *font = XLoadQueryFont(dpy, "12x24");
    if (!font)
        font = XLoadQueryFont(dpy, "fixed");

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
    bool ifMultLine = false; 
    int isUp = 0; // 1 : now going up; -1 : now going down; 0 : init
    int idx = -1;
    vector<string> inputs;

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

                if (event.xbutton.button == Button4)
                { // wheel up
                    scrollOffset = max(0, scrollOffset - SCROLL_STEP);
                    userScrolled = true;
                    drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                }
                else if (event.xbutton.button == Button5)
                { // wheel down
                    scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                       scrollOffset + SCROLL_STEP);
                    if (scrollOffset >= max(0, totalDisplayLines - visibleRows))
                        userScrolled = false;
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

                if (xic)
                {
                    len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);
                }
                else
                {
                    // Fallback: ignore input if XIC not available
                    len = 0;
                    keysym = event.xkey.keycode; // basic handling
                }

                XWindowAttributes attrs;
                XGetWindowAttributes(dpy, win, &attrs);
                int lineHeight = font->ascent + font->descent;
                int visibleRows = max(1, (attrs.height - 30) / lineHeight);

                // --- Scroll keys ---
                if (keysym == XK_Page_Up)
                {
                    scrollOffset = max(0, scrollOffset - SCROLL_STEP * 5);
                    userScrolled = true;
                    drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_Page_Down)
                {
                    scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                       scrollOffset + SCROLL_STEP * 5);
                    if (scrollOffset >= max(0, totalDisplayLines - visibleRows))
                        userScrolled = false;
                    drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_Home)
                {
                    scrollOffset = 0;
                    userScrolled = true;
                    drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_End)
                {
                    scrollOffset = max(0, totalDisplayLines - visibleRows);
                    userScrolled = false;
                    drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_Up)

                {   
                    if(isUp == -1) idx --;

                    if (idx >= 0)
                    {   
                        input = inputs[idx--];
                        screenBuffer.pop_back();
                        screenBuffer.push_back("shre@Term:~" + getPWD() + "$ " + input);
                        isUp = 1;
                    }
                    break;

                }
                else if (keysym == XK_Down)
                {   
                    if(isUp == 1) idx++;

                    if (idx < (int)inputs.size())
                    {    
                        input = inputs[++idx];
                        screenBuffer.pop_back();
                        screenBuffer.push_back("shre@Term:~" + getPWD() + "$ " + input);
                        isUp = -1;
                    }
                    break;

                }

                // --- Normal character handling ---
                if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                {
                    if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                    {
                        if (ifMultLine)
                        {
                            screenBuffer.push_back(">");
                            input.push_back('\n');
                            continue;
                        }
                        else
                        {
                            if((inputs.empty() || inputs.back() != input) && (!input.size() == 0))
                                inputs.push_back(input);
                                

                            idx = inputs.size() - 1;

                            vector<string> outputs = execCommand(input); // returns vector<string>
                            input.clear();
                            for (const auto &line : outputs)
                            {
                                screenBuffer.push_back(line);
                                // cout<<line;
                            }
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");

                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);

                            if (!userScrolled)
                            {
                                scrollOffset = max(0, totalDisplayLines - visibleRows);
                                drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                            }
                        }
                    }
                    else if (wbuf[0] == 8 || wbuf[0] == 127)
                    { // backspace
                        string prompt = "shre@Term:~" + getPWD() + "$ ";
                        if (!screenBuffer.empty() && screenBuffer.back().size() != prompt.size() && screenBuffer.back().size() > 1)
                        {
                            screenBuffer.back().pop_back();
                            if (!input.empty())
                            {
                                if (input.back() == '"') ifMultLine = not ifMultLine;
                                input.pop_back();
                            }
                        }
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    else
                    { // normal char
                        if (screenBuffer.empty())
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
                        if (wbuf[0] < 128)
                        {
                            // if (((char)wbuf[0]) != '"')
                            input.push_back((char)wbuf[0]);
                            screenBuffer.back().push_back((char)wbuf[0]);
                            if (((char)wbuf[0]) == '"')
                                ifMultLine = not ifMultLine;
                        }
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                }

                if (keysym == XK_Escape)
                    running = false;

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

    if (xic)
        XDestroyIC(xic);
    if (xim)
        XCloseIM(xim);
}
