#include "headers.cpp"
#include "draw.cpp"
#include "exec.cpp"

int run(Window win)
{
    // font + gc
    XFontStruct *font = XLoadQueryFont(dpy, "8x16");
    if (!font) font = XLoadQueryFont(dpy, "fixed");
    GC gc = XCreateGC(dpy, win, 0, nullptr);
    XSetFont(dpy, gc, font->fid);
    XSetForeground(dpy, gc, WhitePixel(dpy, scr));

    // XIM/XIC
    XIM xim = XOpenIM(dpy, nullptr, nullptr, nullptr);
    if (!xim) std::cerr << "XOpenIM failed — continuing without input method\n";
    XIC xic = nullptr;
    if (xim)
    {
        xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win, XNFocusWindow, win, nullptr);
        if (!xic) std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XMapWindow(dpy, win);

    // initial tab
    add_tab("/");

    // event loop
    while (true)
    {
        while (XPending(dpy) > 0)
        {
            XEvent event; XNextEvent(dpy, &event);

            if (event.type == Expose)
            {
                XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
                // draw navbar and tabs
                draw_navbar(win, gc, wa.width);
                auto tpos = draw_tabs(win, gc, font);
                // draw active tab content
                if (active_tab >= 0 && active_tab < (int)tabs.size())
                    drawScreen(win, gc, font, tabs[active_tab]);
            }
            else if (event.type == ConfigureNotify)
            {
                // window resized: redraw all
                XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
                draw_navbar(win, gc, wa.width);
                auto tpos = draw_tabs(win, gc, font);
                if (active_tab >= 0 && active_tab < (int)tabs.size())
                    drawScreen(win, gc, font, tabs[active_tab]);
            }
            else if (event.type == ButtonPress)
            {
                // navbar test first
                XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
                auto tpos = draw_tabs(win, gc, font);
                int hit = navbar_hit_test(win, event.xbutton.x, event.xbutton.y, tpos, font);
                if (hit == -1)
                {
                    add_tab("/");
                    draw_navbar(win, gc, wa.width);
                    tpos = draw_tabs(win, gc, font);
                    drawScreen(win, gc, font, tabs[active_tab]);
                    continue;
                }
                else if (hit >= 0)
                {
                    if (hit < (int)tabs.size()) active_tab = hit;
                    draw_navbar(win, gc, wa.width);
                    tpos = draw_tabs(win, gc, font);
                    drawScreen(win, gc, font, tabs[active_tab]);
                    continue;
                }

                // content area events (scroll)
                if (active_tab >= 0 && active_tab < (int)tabs.size())
                {
                    TabState &T = tabs[active_tab];

                    int lineHeight = font->ascent + font->descent;
                    int visibleRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                    if (event.xbutton.button == Button4)
                    { // wheel up
                        T.scrollOffset = max(0, T.scrollOffset - SCROLL_STEP);
                        T.userScrolled = true;
                        drawScreen(win, gc, font, T);
                    }
                    else if (event.xbutton.button == Button5)
                    { // wheel down
                        int totalDisplayLines = drawScreen(win, gc, font, T); // we also get count
                        T.scrollOffset = min(max(0, totalDisplayLines - visibleRows),
                                            T.scrollOffset + SCROLL_STEP);
                        if (T.scrollOffset >= max(0, totalDisplayLines - visibleRows))
                            T.userScrolled = false;
                        drawScreen(win, gc, font, T);
                    }
                }
            }
            else if (event.type == KeyPress)
            {
                if (!(active_tab >= 0 && active_tab < (int)tabs.size()))
                    continue;

                TabState &T = tabs[active_tab];

                // input lookup (wide)
                wchar_t wbuf[32];
                KeySym keysym = 0;
                Status status = 0;
                int len = 0;
                if (xic) len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);
                else { len = 0; keysym = event.xkey.keycode; }

                // metrics
                XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
                int lineHeight = font->ascent + font->descent;
                int visibleRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                auto rebuildScreenBuffer = [&]()
                {
                    while (!T.screenBuffer.empty() && T.screenBuffer.back().rfind("shre@Term:", 0) != 0)
                        T.screenBuffer.pop_back();
                    if (!T.screenBuffer.empty()) T.screenBuffer.pop_back();

                    string sdisp = formatPWD(T.cwd);
                    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");

                    vector<string> parts;
                    size_t last = 0, p;
                    while ((p = T.input.find('\n', last)) != string::npos)
                    {
                        parts.push_back(T.input.substr(last, p - last));
                        last = p + 1;
                    }
                    parts.push_back(T.input.substr(last));

                    if (parts.empty()) { T.screenBuffer.push_back(prompt); return; }

                    for (size_t i = 0; i < parts.size(); ++i)
                    {
                        if (i == 0) T.screenBuffer.push_back(prompt + parts[i]);
                        else T.screenBuffer.push_back(parts[i]);
                    }
                };

                // Escape: exit app
                if (keysym == XK_Escape) {
                    // soft-exit: close current tab if >1, else exit
                    if (tabs.size() > 1) {
                        tabs.erase(tabs.begin() + active_tab);
                        if (active_tab >= (int)tabs.size()) active_tab = (int)tabs.size() - 1;
                        draw_navbar(win, gc, wa.width);
                        auto tpos = draw_tabs(win, gc, font);
                        if (!tabs.empty()) drawScreen(win, gc, font, tabs[active_tab]);
                        continue;
                    } else {
                        return 0;
                    }
                }

                // PageUp/Down, Home/End
                if (keysym == XK_Page_Up)
                {
                    T.scrollOffset = max(0, T.scrollOffset - SCROLL_STEP * 5);
                    T.userScrolled = true;
                    drawScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_Page_Down)
                {
                    int totalDisplayLines = drawScreen(win, gc, font, T);
                    T.scrollOffset = min(max(0, totalDisplayLines - visibleRows), T.scrollOffset + SCROLL_STEP * 5);
                    if (T.scrollOffset >= max(0, totalDisplayLines - visibleRows)) T.userScrolled = false;
                    drawScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_Home && (event.xkey.state & ControlMask))
                {
                    T.scrollOffset = 0;
                    T.userScrolled = true;
                    drawScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_End && (event.xkey.state & ControlMask))
                {
                    int totalDisplayLines = drawScreen(win, gc, font, T);
                    T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                    T.userScrolled = false;
                    drawScreen(win, gc, font, T);
                    continue;
                }

                // History up/down
                if (keysym == XK_Up)
                {
                    if (T.isSearching || T.inRec) { /* ignore */ }
                    else if (!inputs.empty())
                    {
                        T.isMultLine = false;
                        if (T.inpIdx > 0) T.inpIdx--;
                        else T.inpIdx = 0;

                        T.input = inputs[T.inpIdx];
                        for (char c : T.input) if (c=='"') T.isMultLine = !T.isMultLine;
                        T.currCursorPos = (int)T.input.size();
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    continue;
                }
                if (keysym == XK_Down)
                {
                    if (T.isSearching || T.inRec) { /* ignore */ }
                    else if (!inputs.empty())
                    {
                        T.isMultLine = false;
                        if (T.inpIdx < (int)inputs.size() - 1) {
                            T.inpIdx++; T.input = inputs[T.inpIdx];
                        } else { T.inpIdx = (int)inputs.size(); T.input.clear(); }
                        for (char c : T.input) if (c=='"') T.isMultLine = !T.isMultLine;
                        T.currCursorPos = (int)T.input.size();
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                    }
                    continue;
                }

                // Left/Right
                if (keysym == XK_Left)
                {
                    if (T.currCursorPos > 0) T.currCursorPos--;
                    drawScreen(win, gc, font, T);
                    continue;
                }
                if (keysym == XK_Right)
                {
                    if (T.currCursorPos < (int)T.input.size()) T.currCursorPos++;
                    drawScreen(win, gc, font, T);
                    continue;
                }

                // Ctrl+V paste request
                if ((event.xkey.state & ControlMask) && (keysym == XK_v || keysym == XK_V))
                {
                    XConvertSelection(dpy, XInternAtom(dpy, "CLIPBOARD", False),
                                      XInternAtom(dpy, "UTF8_STRING", False),
                                      XInternAtom(dpy, "PASTE_BUFFER", False),
                                      win, CurrentTime);
                    continue;
                }

                // Ctrl+R search
                if ((event.xkey.state & ControlMask) && (keysym == XK_r))
                {
                    T.screenBuffer.push_back("Enter search term:");
                    T.input.clear();
                    string sdisp = formatPWD(T.cwd);
                    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                    T.currCursorPos = 0;
                    T.isSearching = true;
                    drawScreen(win, gc, font, T);
                    continue;
                }

                // Ctrl+A start
                if ((event.xkey.state & ControlMask) && (keysym == XK_a))
                {
                    if (T.isSearching) T.currCursorPos = 0;
                    else T.currCursorPos = T.count;
                    drawScreen(win, gc, font, T);
                    continue;
                }
                // Ctrl+E end
                if ((event.xkey.state & ControlMask) && (keysym == XK_e))
                {
                    T.currCursorPos = (int)T.input.size();
                    drawScreen(win, gc, font, T);
                    continue;
                }

                // Tab completion (your logic)
                if (keysym == XK_Tab)
                {
                    if (!T.input.empty())
                    {
                        T.inRec = true;
                        T.query = getQuery(T.input);
                        if (T.query == T.input && T.query.rfind("./", 0) == 0)
                            T.query = T.query.substr(2);
                        T.forRec = T.input;

                        // list directory candidates under tab cwd
                        auto outputs = execCommandInDir("ls", T.cwd);
                        vector<string> candidates;
                        for (auto &l : outputs) if (!l.empty() && l.rfind("ERROR:", 0) != 0) candidates.push_back(l);

                        T.recs = getRecomm(T.query, candidates);
                        if (T.recs.empty())
                        {
                            T.inRec = false;
                        }
                        else if (T.recs.size() == 1)
                        {
                            T.input += T.recs[0].substr(T.query.size());
                            // update last prompt line
                            if (!T.screenBuffer.empty())
                                T.screenBuffer.back() += T.recs[0].substr(T.query.size());
                            T.inRec = false;
                            T.currCursorPos = (int)T.input.size();
                        }
                        else
                        {
                            for (size_t i = 0; i < T.recs.size(); i++)
                                T.showRec += to_string(i + 1) + ". " + T.recs[i] + "  ";
                            T.screenBuffer.push_back("REC:"+T.showRec);
                            T.screenBuffer.push_back("REC:Choose from above options:");
                            T.input.clear();
                            T.currCursorPos = 0;
                            T.showRec.clear();
                        }
                        drawScreen(win, gc, font, T);
                    }
                    continue;
                }

                // Printable handling via wbuf
                if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                {
                    // ENTER
                    if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                    {
                        if (T.inRec)
                        {
                            int recIdx = min(getRecIdx(T.input), (int)T.recs.size()) - 1;
                            if (recIdx < 0) recIdx = 0;
                            string rec = T.recs[recIdx];
                            T.input = T.forRec + rec.substr(T.query.size());

                            string sdisp = formatPWD(T.cwd);
                            string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");

                            // remove the three pushed lines (list + "Choose..." + current line)
                            if (!T.screenBuffer.empty()) { T.screenBuffer.pop_back(); }
                            if (!T.screenBuffer.empty()) { T.screenBuffer.pop_back(); }
                            if (!T.screenBuffer.empty()) { T.screenBuffer.pop_back(); }
                            T.screenBuffer.push_back(prompt + T.input);
                            T.currCursorPos = (int)T.input.size();
                            T.inRec = false;
                            continue;
                        }

                        if (T.isSearching)
                        {
                            string search_res = searchHistory(T.input);
                            T.input.clear();
                            string sdisp = formatPWD(T.cwd);
                            string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                            if (search_res != "No match for search term in history")
                            {
                                T.input = search_res;
                                T.isMultLine = false;
                                for (char c : T.input) if (c=='"') T.isMultLine = !T.isMultLine;
                                T.screenBuffer.push_back(prompt + T.input);
                                T.currCursorPos = (int)T.input.size();
                            }
                            else
                            {
                                T.screenBuffer.push_back(search_res);
                                T.screenBuffer.push_back(prompt + T.input);
                                T.currCursorPos = 0;
                            }
                            T.isSearching = false;
                            continue;
                        }

                        if (T.isMultLine)
                        {
                            T.input.insert(T.input.begin() + T.currCursorPos, '\n');
                            T.currCursorPos++;
                            T.count = (int)T.input.size();
                            rebuildScreenBuffer();
                            drawScreen(win, gc, font, T);
                            continue;
                        }
                        else
                        {
                            if (!T.input.empty())
                            {
                                if (inputs.empty() || inputs.back() != T.input)
                                {
                                    storeInput(T.input);
                                    inputs.push_back(T.input);
                                }
                            }
                            T.count = 0;
                            T.inpIdx = (int)inputs.size();

                            auto trimLocal = [](const string &s) -> string
                            {
                                size_t a = 0, b = s.size();
                                while (a < b && isspace((unsigned char)s[a])) ++a;
                                while (b > a && isspace((unsigned char)s[b-1])) --b;
                                return s.substr(a, b-a);
                            };
                            string trimmed = trimLocal(T.input);

                            if (trimmed == "history")
                            {
                                ifstream in(FILENAME);
                                if (!in) { cerr << "Error: cannot open file\n"; }
                                else {
                                    string line;
                                    while (getline(in, line))
                                        T.screenBuffer.push_back(line);
                                    in.close();
                                }
                            }
                            if (trimmed == "clear")
                            {
                                T.screenBuffer.clear();
                                string sdisp = formatPWD(T.cwd);
                                string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                                T.screenBuffer.push_back(prompt);
                                T.input.clear();
                                T.currCursorPos = 0;
                                T.isMultLine = false;
                                int totalDisplayLines = drawScreen(win, gc, font, T);
                                T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                                T.userScrolled = false;
                                drawScreen(win, gc, font, T);
                                continue;
                            }

                            // execute in tab cwd
                            vector<string> outputs = execCommandInDir(T.input, T.cwd);
                            T.input.clear();
                            T.currCursorPos = 0;

                            for (const auto &line : outputs)
                                T.screenBuffer.push_back(line);

                            string sdisp = formatPWD(T.cwd);
                            string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                            T.screenBuffer.push_back(prompt);

                            int totalDisplayLines = drawScreen(win, gc, font, T);
                            if (!T.userScrolled)
                            {
                                T.scrollOffset = max(0, totalDisplayLines - visibleRows);
                                drawScreen(win, gc, font, T);
                            }
                            continue;
                        }
                    }

                    // BACKSPACE
                    if (wbuf[0] == 8 || wbuf[0] == 127)
                    {
                        if ((T.isSearching || T.inRec) && !T.input.empty())
                        {
                            string curr = T.screenBuffer.back();
                            T.screenBuffer.pop_back();
                            T.input.erase(T.input.begin() + T.currCursorPos - 1);
                            T.screenBuffer.push_back(curr.substr(0, curr.size() - 1));
                            T.currCursorPos--;
                            continue;
                        }

                        if (T.currCursorPos > 0)
                        {
                            if (T.input[T.currCursorPos - 1] == '"')
                                T.isMultLine = !T.isMultLine;
                            T.input.erase(T.input.begin() + T.currCursorPos - 1);
                            T.currCursorPos--;
                            rebuildScreenBuffer();
                            drawScreen(win, gc, font, T);
                        }
                        continue;
                    }

                    // Regular printable char
                    char ch = (char)wbuf[0];
                    if (T.inRec)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, ch);
                        T.screenBuffer.back() += ch;
                        T.currCursorPos++;
                        continue;
                    }
                    if (T.isSearching)
                    {
                        T.input.insert(T.input.begin() + T.currCursorPos, ch);
                        T.screenBuffer.back() += ch;
                        T.currCursorPos++;
                        continue;
                    }
                    if (isprint((unsigned char)ch) || ch == '\t')
                    {
                        if (ch == '"') T.isMultLine = !T.isMultLine;
                        T.input.insert(T.input.begin() + T.currCursorPos, ch);
                        T.currCursorPos++;
                        rebuildScreenBuffer();
                        drawScreen(win, gc, font, T);
                        continue;
                    }
                }
            }
            else if (event.type == SelectionNotify)
            {
                if (!(active_tab >= 0 && active_tab < (int)tabs.size()))
                    continue;
                TabState &T = tabs[active_tab];

                if (event.xselection.selection == XInternAtom(dpy, "CLIPBOARD", False))
                {
                    Atom type; int format;
                    unsigned long nitems, bytes_after; unsigned char *data = nullptr;

                    XGetWindowProperty(dpy, win,
                                       XInternAtom(dpy, "PASTE_BUFFER", False),
                                       0, (~0L), True, AnyPropertyType,
                                       &type, &format, &nitems, &bytes_after, &data);

                    if (data)
                    {
                        std::string clipText((char *)data, nitems);
                        XFree(data);

                        std::istringstream ss(clipText);
                        std::string line; bool first = true;

                        while (std::getline(ss, line))
                        {
                            if (first)
                            {
                                if (!T.screenBuffer.empty()) T.screenBuffer.back() += line;
                                T.input += line;
                                first = false;
                            }
                            else
                            {
                                T.screenBuffer.push_back(line);
                                T.input += '\n';
                                T.input += line;
                            }
                        }
                        T.currCursorPos = (int)T.input.size();
                        drawScreen(win, gc, font, T);
                    }
                }
            }
        } // while XPending

        // blink active tab cursor only
        if (active_tab >= 0 && active_tab < (int)tabs.size())
        {
            TabState &T = tabs[active_tab];
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<std::chrono::milliseconds>(now - T.lastBlink).count() > 500)
            {
                T.showCursor = !T.showCursor;
                T.lastBlink = now;
                drawScreen(win, gc, font, T);
            }
        }
        // tiny sleep
        this_thread::sleep_for(chrono::milliseconds(20));
    }

    if (xic) XDestroyIC(xic);
    if (xim) XCloseIM(xim);

}