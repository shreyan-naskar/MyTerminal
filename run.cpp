#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
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
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include <locale.h>

using namespace std;
#include "exec.cpp"
#include "draw.cpp"
vector<string> inputs;
vector<string> sepInp(string input)
{
    vector<string> lines;

    string line = "";
    for (auto c : input)
    {
        if (c == '\n')
        {
            lines.push_back(line);
            line = "";
            continue;
        }
        line += c;
    }
    lines.push_back(line);

    return lines;
}

static const int SCROLL_STEP = 3; // lines per wheel/page step

static void run(Window win)
{
    int inpIdx = -1;
    XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask);

    // --- XIM/XIC setup ---
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim)
        std::cerr << "XOpenIM failed — continuing without input method\n";

    XIC xic = nullptr;
    if (xim)
    {
        xic = XCreateIC(xim,
                        XNInputStyle,
                        XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win,
                        XNFocusWindow, win,
                        nullptr);
        if (!xic)
            std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XFontStruct *font = XLoadQueryFont(dpy, "8x16");
    if (!font)
        font = XLoadQueryFont(dpy, "fixed");

    vector<string> screenBuffer;
    string s = getPWD();
    if (s == "/")
    {
        screenBuffer.push_back("shre@Term:" + getPWD() + "$ ");
    }
    else
    {
        screenBuffer.push_back("shre@Term:~" + getPWD() + "$ ");
    }
    
    GC gc = XCreateGC(dpy, win, 0, nullptr);
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, WhitePixel(dpy, scr)); // default

    XMapWindow(dpy, win);

    bool running = true;
    bool showCursor = true;
    auto lastBlink = std::chrono::steady_clock::now();
    XEvent event;

    int scrollOffset = 0;
    bool userScrolled = false;

    int totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
    bool isMultLine = false;
    int count = 0;
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
                // helper: look up chars
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
                    len = 0;
                    keysym = event.xkey.keycode;
                }

                // local window metrics
                XWindowAttributes attrs;
                XGetWindowAttributes(dpy, win, &attrs);
                int lineHeight = font->ascent + font->descent;
                int visibleRows = max(1, (attrs.height - 30) / lineHeight);

                // --- helper to rebuild screenBuffer from input ---
                auto rebuildScreenBuffer = [&]()
                {
                    // remove the last prompt/input lines and rebuild
                    // We want the visible buffer to contain previous outputs unchanged,
                    // and the last N lines to show the prompt + input split by '\n'.
                    // We'll remove trailing lines until we find a line that starts with the prompt prefix.
                    while (!screenBuffer.empty() && screenBuffer.back().rfind("shre@Term:", 0) != 0)
                    {
                        screenBuffer.pop_back();
                    }
                    if (!screenBuffer.empty())
                    {
                        screenBuffer.pop_back(); // remove the prompt line (we'll rebuild it)
                    }

                    // build prompt
                    string cwd = getPWD();
                    string prompt = (cwd == "/") ? ("shre@Term:" + cwd + "$ ") : ("shre@Term:~" + cwd + "$ ");

                    // split input by '\n'
                    vector<string> parts;
                    size_t last = 0;
                    size_t p;
                    while ((p = input.find('\n', last)) != string::npos)
                    {
                        parts.push_back(input.substr(last, p - last));
                        last = p + 1;
                    }
                    parts.push_back(input.substr(last));

                    if (parts.empty())
                    {
                        // always show a prompt line, even for empty input
                        screenBuffer.push_back(prompt);
                        return;
                    }

                    for (size_t i = 0; i < parts.size(); ++i)
                    {
                        if (i == 0)
                        {
                            screenBuffer.push_back(prompt + parts[i]);
                        }
                        else
                        {
                            screenBuffer.push_back(parts[i]);
                        }
                    }
                };

                // --- navigation keys that don't rely on characters ---
                if (keysym == XK_Page_Up)
                {
                    scrollOffset = max(0, scrollOffset - SCROLL_STEP * 5);
                    userScrolled = true;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_Page_Down)
                {
                    scrollOffset = min(max(0, totalDisplayLines - visibleRows), scrollOffset + SCROLL_STEP * 5);
                    if (scrollOffset >= max(0, totalDisplayLines - visibleRows))
                        userScrolled = false;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_Home && (event.xkey.state & ControlMask))
                {
                    // Ctrl+Home -> go to top of buffer
                    scrollOffset = 0;
                    userScrolled = true;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                else if (keysym == XK_End && (event.xkey.state & ControlMask))
                {
                    // Ctrl+End -> go to bottom
                    scrollOffset = max(0, totalDisplayLines - visibleRows);
                    userScrolled = false;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }

                // --- inputs navigation (Up/Down) ---
                if (keysym == XK_Up)
                {
                    if (!inputs.empty())
                    {
                        if (inpIdx > 0)
                            inpIdx--;
                        else
                            inpIdx = 0;
                        input = inputs[inpIdx];
                        currCursorPos = input.size();
                        rebuildScreenBuffer();
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    break;
                }
                if (keysym == XK_Down)
                {
                    if (!inputs.empty())
                    {
                        if (inpIdx < (int)inputs.size() - 1)
                        {
                            inpIdx++;
                            input = inputs[inpIdx];
                        }
                        else
                        {
                            inpIdx = inputs.size();
                            input.clear();
                        }
                        currCursorPos = input.size();
                        rebuildScreenBuffer();
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    }
                    break;
                }

                // --- Left/Right arrows ---
                if (keysym == XK_Left)
                {
                    if (currCursorPos > 0)
                        currCursorPos--;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }
                if (keysym == XK_Right)
                {
                    if (currCursorPos < (int)input.size())
                        currCursorPos++;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }

                // --- Ctrl+V (paste via selection) handled earlier via SelectionNotify, keep unchanged ---
                if ((event.xkey.state & ControlMask) && (keysym == XK_v || keysym == XK_V))
                {
                    XConvertSelection(dpy, XInternAtom(dpy, "CLIPBOARD", False),
                                      XInternAtom(dpy, "UTF8_STRING", False),
                                      XInternAtom(dpy, "PASTE_BUFFER", False),
                                      win, CurrentTime);
                    break;
                }

                // --- Ctrl+A (start of user input) ---
                if ((event.xkey.state & ControlMask) && (keysym == XK_a))
                {
                    // Move to start of the current input (not counting prompt)
                    currCursorPos = count;
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }

                // --- Ctrl+E (end of input) ---
                if ((event.xkey.state & ControlMask) && (keysym == XK_e))
                {
                    currCursorPos = input.size();
                    totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                    break;
                }

                // --- printable/character handling ---
                if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                {
                    // ENTER / RETURN
                    if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                    {
                        if (isMultLine)
                        { // multi-line active -> insert newline into input
                            input.insert(input.begin() + currCursorPos, '\n');
                            currCursorPos++;
                            count = input.size();
                            rebuildScreenBuffer();
                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                            continue; // don't execute command yet
                        }

                        else
                        {

                            // execute command flow
                            if (!input.empty())
                            {
                                if (inputs.empty() || inputs.back() != input)
                                    inputs.push_back(input);
                            }
                            count = 0;
                            inpIdx = inputs.size();
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
                            // --- handle "clear" command ---
                            if (trimmed == "clear")
                            {
                                // 1) Clear visible buffer (keep inputs)
                                screenBuffer.clear();

                                // 2) Push fresh prompt line
                                string cwd = getPWD();
                                string prompt = (cwd == "/") ? ("shre@Term:" + cwd + "$ ") : ("shre@Term:~" + cwd + "$ ");
                                screenBuffer.push_back(prompt);

                                // 3) Reset input & cursor & isMultLines (so editing starts clean)
                                input.clear();
                                currCursorPos = 0;
                                isMultLine = false;      // if you use this for quoting/multiline, reset it

                                // 4) Redraw and reposition view to bottom (so prompt is visible)
                                totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);

                                {
                                    XWindowAttributes attrs;
                                    XGetWindowAttributes(dpy, win, &attrs);
                                    int lineHeight = font->ascent + font->descent;
                                    int visibleRows = max(1, (attrs.height - 30) / lineHeight);
                                    scrollOffset = max(0, totalDisplayLines - visibleRows);
                                    userScrolled = false;
                                }

                                drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);

                                // 5) skip normal execution for this Enter
                                continue;
                            }

                            // execute and append outputs
                            vector<string> outputs = execCommand(input);
                            input.clear();
                            currCursorPos = 0;

                            for (const auto &line : outputs)
                                screenBuffer.push_back(line);

                            string cwd = getPWD();
                            string prompt = (cwd == "/") ? ("shre@Term:" + cwd + "$ ") : ("shre@Term:~" + cwd + "$ ");
                            screenBuffer.push_back(prompt);

                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                            if (!userScrolled)
                            {
                                scrollOffset = max(0, totalDisplayLines - visibleRows);
                                totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                            }
                            continue;
                        }
                    }

                    // BACKSPACE
                    if (wbuf[0] == 8 || wbuf[0] == 127)
                    {
                        if (currCursorPos > 0)
                        {
                            if (input[currCursorPos - 1] == '"')
                                isMultLine = !isMultLine;
                            input.erase(input.begin() + currCursorPos - 1);
                            currCursorPos--;
                            rebuildScreenBuffer();
                            totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        }
                        else
                        {
                            // at start of input: nothing to do (or optionally beep)
                        }
                        continue;
                    }

                    // Regular printable char
                    char ch = (char)wbuf[0];
                    if (isprint((unsigned char)ch) || ch == '\t')
                    {
                        if (ch == '"')
                            isMultLine = !isMultLine;
                        input.insert(input.begin() + currCursorPos, ch);
                        currCursorPos++;
                        rebuildScreenBuffer();
                        totalDisplayLines = drawScreen(win, gc, font, screenBuffer, showCursor, scrollOffset);
                        continue;
                    }
                }

                // Escape quits
                if (keysym == XK_Escape)
                    running = false;
                break;
            } // end KeyPress

            // KeyPress
            case SelectionNotify:
                if (event.xselection.selection == XInternAtom(dpy, "CLIPBOARD", False))
                {
                    Atom type;
                    int format;
                    unsigned long nitems, bytes_after;
                    unsigned char *data = nullptr;

                    XGetWindowProperty(dpy, win,
                                       XInternAtom(dpy, "PASTE_BUFFER", False),
                                       0, (~0L), True, AnyPropertyType,
                                       &type, &format, &nitems, &bytes_after, &data);

                    if (data)
                    {
                        std::string clipText((char *)data, nitems);
                        XFree(data);

                        // ✅ Append to input buffer or insert into screen
                        std::istringstream ss(clipText);
                        std::string line;
                        bool first = true;

                        while (std::getline(ss, line))
                        {
                            if (first)
                            {
                                // Continue on current line
                                screenBuffer.back() += line;
                                input += line;
                                first = false;
                            }
                            else
                            {
                                // New line (simulate Enter key)
                                screenBuffer.push_back(line);
                                input += '\n';
                                input += line;
                            }
                        }

                        // Optional: redraw
                        currCursorPos = input.size();
                        drawScreen(win, gc, font, screenBuffer, true, scrollOffset);
                    }
                }
                break;
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
