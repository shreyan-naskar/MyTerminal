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
    if (getPWD() == "/")
        screenBuffer.push_back("shre@Term:" + getPWD() + "$ ");
    else
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
                if (keysym == XK_Up)
                {
                    if (!inputs.empty())
                    {
                        if (idx > 0)
                            idx--;
                        else
                            idx = 0; // stay at the oldest command

                        input = inputs[idx];
                        screenBuffer.pop_back();
                        if (getPWD() == "/")
                            screenBuffer.push_back("shre@Term:" + getPWD() + "$ " + input);
                        else
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ " + input);
                    }
                    break;
                }

                if (keysym == XK_Down)
                {
                    if (!inputs.empty())
                    {
                        if (idx < (int)inputs.size() - 1)
                        {
                            idx++;
                            input = inputs[idx];
                        }
                        else
                        {
                            idx = inputs.size();
                            input.clear();
                        }

                        screenBuffer.pop_back();
                        if (getPWD() == "/")
                            screenBuffer.push_back("shre@Term:" + getPWD() + "$ " + input);
                        else
                            screenBuffer.push_back("shre@Term:~" + getPWD() + "$ " + input);
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
                            // save to history (same as before)
                            if ((inputs.empty() || inputs.back() != input) && (!input.empty()))
                                inputs.push_back(input);

                            idx = inputs.size() - 1;

                            // SPECIAL: handle "clear" (only clears visible screenBuffer, keeps history)
                            // trim whitespace from input for robust match
                            auto trim = [](const string &s) -> string
                            {
                                size_t a = 0, b = s.size();
                                while (a < b && isspace((unsigned char)s[a]))
                                    ++a;
                                while (b > a && isspace((unsigned char)s[b - 1]))
                                    --b;
                                return s.substr(a, b - a);
                            };
                            string trimmed = trim(input);
                            if (trimmed == "clear")
                            {
                                // clear only the visible buffer and show single prompt line
                                screenBuffer.clear();
                                if (getPWD() == "/")
                                    screenBuffer.push_back("shre@Term:" + getPWD() + "$ ");
                                else
                                    screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");

                                // reset scroll to bottom (you can set to 0 if you prefer top)
                                totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                                // put view at bottom (so prompt is visible)
                                {
                                    XWindowAttributes attrs;
                                    XGetWindowAttributes(dpy, win, &attrs);
                                    int lineHeight = font->ascent + font->descent;
                                    int visibleRows = max(1, (attrs.height - (/* if defined */ + 5)) / lineHeight);
                                    // In your code you used 30 as top margin earlier; use same logic if needed.
                                    scrollOffset = max(0, totalDisplayLines - visibleRows);
                                    userScrolled = false;
                                }
                                // redraw and clear input
                                drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                                input.clear();
                                continue;
                            }

                            // Normal execution path: run the command and append outputs
                            vector<string> outputs = execCommand(input); // returns vector<string>
                            input.clear();
                            for (const auto &line : outputs)
                            {
                                screenBuffer.push_back(line);
                            }
                            if (getPWD() == "/")
                                screenBuffer.push_back("shre@Term:" + getPWD() + "$ ");
                            else
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
                        if (getPWD() == "/")
                                prompt = "shre@Term:" + getPWD() + "$ ";
                        if (!screenBuffer.empty() && screenBuffer.back().size() != prompt.size() && screenBuffer.back().size() > 1)
                        {
                            screenBuffer.back().pop_back();
                            if (!input.empty())
                            {
                                if (input.back() == '"')
                                    ifMultLine = not ifMultLine;
                                input.pop_back();
                            }
                        }
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    else
                    { // normal char
                        if (screenBuffer.empty())
                        {
                            if (getPWD() == "/")
                                screenBuffer.push_back("shre@Term:" + getPWD() + "$ ");
                            else
                                screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
                        }
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
