#include "headers.cpp"
#include "draw.cpp"
#include "exec.cpp"

void run(Window win)
{
    // font + gc
    XFontStruct *font = XLoadQueryFont(disp, "8x16");

    if (!font)
        font = XLoadQueryFont(disp, "fixed");
    GC gc = XCreateGC(disp, win, 0, nullptr);
    XSetFont(disp, gc, font->fid);
    XSetForeground(disp, gc, WhitePixel(disp, scr));

    // XIM/XIC
    XIM xim = XOpenIM(disp, nullptr, nullptr, nullptr);
    if (!xim)
        std::cerr << "XOpenIM failed — continuing without input method\n";
    XIC xic = nullptr;
    if (xim)
    {
        xic = XCreateIC(xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing,
                        XNClientWindow, win, XNFocusWindow, win, nullptr);
        if (!xic)
            std::cerr << "XCreateIC failed — continuing without input context\n";
    }

    XMapWindow(disp, win);

    // initial tab
    addTab("/");

    // event loop
    while (true)
    {
        while (XPending(disp) > 0)
        {
            XEvent event;
            XNextEvent(disp, &event);

            if (event.type == Expose)
            {
                XWindowAttributes wa;
                XGetWindowAttributes(disp, win, &wa);
                // draw navbar and tabs
                makeNavBar(win, gc, wa.width);
                auto tpos = makeTabs(win, gc, font);
                // draw active tab content
                if (tabActive >= 0 && tabActive < (int)tabs.size())
                    makeScreen(win, gc, font, tabs[tabActive]);
            }
            else if (event.type == ConfigureNotify)
            {
                // window resized: redraw all
                XWindowAttributes wa;
                XGetWindowAttributes(disp, win, &wa);
                makeNavBar(win, gc, wa.width);
                auto tpos = makeTabs(win, gc, font);
                if (tabActive >= 0 && tabActive < (int)tabs.size())
                    makeScreen(win, gc, font, tabs[tabActive]);
            }
            else if (event.type == ButtonPress)
            {
                // navbar test first
                XWindowAttributes wa;
                XGetWindowAttributes(disp, win, &wa);
                auto tpos = makeTabs(win, gc, font);

                int tabIdx = -1;
                int hit = navbarHit(event.xbutton.x, event.xbutton.y, tpos, &tabIdx);

                if (hit == -2) // "+" clicked
                {
                    addTab("/");
                    makeNavBar(win, gc, wa.width);
                    tpos = makeTabs(win, gc, font);
                    makeScreen(win, gc, font, tabs[tabActive]);
                    continue;
                }
                else if (hit == -3 && tabIdx >= 0) // "×" close clicked
                {
                    if (tabIdx < (int)tabs.size())
                    {
                        tabs.erase(tabs.begin() + tabIdx);
                        if (tabActive >= (int)tabs.size())
                            tabActive = (int)tabs.size() - 1;
                        makeNavBar(win, gc, wa.width);
                        tpos = makeTabs(win, gc, font);
                        if (!tabs.empty())
                            makeScreen(win, gc, font, tabs[tabActive]);
                    }
                    continue;
                }
                else if (hit >= 0) // clicked on tab
                {
                    if (hit < (int)tabs.size())
                        tabActive = hit;
                    makeNavBar(win, gc, wa.width);
                    tpos = makeTabs(win, gc, font);
                    makeScreen(win, gc, font, tabs[tabActive]);
                    continue;
                }

                // content area events (scroll)
                if (tabActive >= 0 && tabActive < (int)tabs.size())
                {
                    tabState &T = tabs[tabActive];

                    int lineHeight = font->ascent + font->descent;
                    int seeRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                    if (event.xbutton.button == Button4)
                    { // wheel up
                        T.scrlOffset = max(0, T.scrlOffset - SCROLL_STEP);
                        T.userScrolled = true;
                        makeScreen(win, gc, font, T);
                    }
                    else if (event.xbutton.button == Button5)
                    { // wheel down
                        int totalDisplayLines = makeScreen(win, gc, font, T);
                        T.scrlOffset = min(max(0, totalDisplayLines - seeRows),
                                             T.scrlOffset + SCROLL_STEP);
                        if (T.scrlOffset >= max(0, totalDisplayLines - seeRows))
                            T.userScrolled = false;
                        makeScreen(win, gc, font, T);
                    }
                }
            }
            else if (event.type == MotionNotify)
            {
                howerXClose = -1;
                howerPlusTab = false;

                int mx = event.xmotion.x;
                // int my = event.xmotion.y;

                auto tpos = makeTabs(win, gc, font); // or cache it globally

                for (size_t i = 0; i < tpos.size(); ++i)
                {
                    if (tpos[i].isPlus)
                    {
                        if (mx >= tpos[i].x && mx <= tpos[i].x + tpos[i].w)
                            howerPlusTab = true;
                    }
                    else
                    {
                        if (mx >= tpos[i].xClose && mx <= tpos[i].xClose + tpos[i].wClose)
                            howerXClose = i;
                    }
                }

                makeTabs(win, gc, font); // redraw with hover state
            }

            else if (event.type == KeyPress)
            {
                if (!(tabActive >= 0 && tabActive < (int)tabs.size()))
                    continue;

                tabState &T = tabs[tabActive];

                // input lookup (wide)
                wchar_t wbuf[32];
                KeySym keysym = 0;
                Status status = 0;
                int len = 0;
                if (xic)
                    len = XwcLookupString(xic, &event.xkey, wbuf, 32, &keysym, &status);
                else
                {
                    len = 0;
                    keysym = event.xkey.keycode;
                }

                // metrics
                XWindowAttributes wa;
                XGetWindowAttributes(disp, win, &wa);
                int lineHeight = font->ascent + font->descent;
                int seeRows = max(1, (wa.height - (NAVBAR_H + 30)) / lineHeight);

                auto remakeScreenBuf = [&]()
                {
                    while (!T.displayBuffer.empty() && T.displayBuffer.back().rfind("shre@Term:", 0) != 0)
                        T.displayBuffer.pop_back();
                    if (!T.displayBuffer.empty())
                        T.displayBuffer.pop_back();

                    string sdisp = editPWD(T.cwd);
                    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");

                    vector<string> parts;
                    size_t last = 0, p;
                    while ((p = T.input.find('\n', last)) != string::npos)
                    {
                        parts.push_back(T.input.substr(last, p - last));
                        last = p + 1;
                    }
                    parts.push_back(T.input.substr(last));

                    if (parts.empty())
                    {
                        T.displayBuffer.push_back(prompt);
                        return;
                    }

                    for (size_t i = 0; i < parts.size(); ++i)
                    {
                        if (i == 0)
                            T.displayBuffer.push_back(prompt + parts[i]);
                        else
                            T.displayBuffer.push_back(parts[i]);
                    }
                };

                // Escape: exit app
                if (keysym == XK_Escape)
                {
                    // soft-exit: close current tab if >1, else exit
                    if (tabs.size() > 1)
                    {
                        tabs.erase(tabs.begin() + tabActive);
                        if (tabActive >= (int)tabs.size())
                            tabActive = (int)tabs.size() - 1;
                        makeNavBar(win, gc, wa.width);
                        auto tpos = makeTabs(win, gc, font);
                        if (!tabs.empty())
                            makeScreen(win, gc, font, tabs[tabActive]);
                        continue;
                    }
                    else
                    {
                        return;
                    }
                }

                // PageUp/Down, Home/End
                if (keysym == XK_Page_Up)
                {
                    T.scrlOffset = max(0, T.scrlOffset - SCROLL_STEP * 5);
                    T.userScrolled = true;
                    makeScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_Page_Down)
                {
                    int totalDisplayLines = makeScreen(win, gc, font, T);
                    T.scrlOffset = min(max(0, totalDisplayLines - seeRows), T.scrlOffset + SCROLL_STEP * 5);
                    if (T.scrlOffset >= max(0, totalDisplayLines - seeRows))
                        T.userScrolled = false;
                    makeScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_Home && (event.xkey.state & ControlMask))
                {
                    T.scrlOffset = 0;
                    T.userScrolled = true;
                    makeScreen(win, gc, font, T);
                    continue;
                }
                else if (keysym == XK_End && (event.xkey.state & ControlMask))
                {
                    int totalDisplayLines = makeScreen(win, gc, font, T);
                    T.scrlOffset = max(0, totalDisplayLines - seeRows);
                    T.userScrolled = false;
                    makeScreen(win, gc, font, T);
                    continue;
                }

                // History up/down
                if (keysym == XK_Up)
                {
                    if (T.searchFlag || T.recommFlag)
                    { /* ignore */
                    }
                    else if (!inputs.empty())
                    {
                        T.multLineFlag = false;
                        if (T.inpIdx > 0)
                            T.inpIdx--;
                        else
                            T.inpIdx = 0;

                        T.input = inputs[T.inpIdx];
                        for (char c : T.input)
                            if (c == '"')
                                T.multLineFlag = !T.multLineFlag;
                        T.currentCursorPosition = (int)T.input.size();
                        remakeScreenBuf();
                        makeScreen(win, gc, font, T);
                    }
                    continue;
                }
                if (keysym == XK_Down)
                {
                    if (T.searchFlag || T.recommFlag)
                    { /* ignore */
                    }
                    else if (!inputs.empty())
                    {
                        T.multLineFlag = false;
                        if (T.inpIdx < (int)inputs.size() - 1)
                        {
                            T.inpIdx++;
                            T.input = inputs[T.inpIdx];
                        }
                        else
                        {
                            T.inpIdx = (int)inputs.size();
                            T.input.clear();
                        }
                        for (char c : T.input)
                            if (c == '"')
                                T.multLineFlag = !T.multLineFlag;
                        T.currentCursorPosition = (int)T.input.size();
                        remakeScreenBuf();
                        makeScreen(win, gc, font, T);
                    }
                    continue;
                }

                // Left/Right
                if (keysym == XK_Left)
                {
                    if (T.currentCursorPosition > 0)
                        T.currentCursorPosition--;
                    makeScreen(win, gc, font, T);
                    continue;
                }
                if (keysym == XK_Right)
                {
                    if (T.currentCursorPosition < (int)T.input.size())
                        T.currentCursorPosition++;
                    makeScreen(win, gc, font, T);
                    continue;
                }

                // Ctrl+V paste request
                if ((event.xkey.state & ControlMask) && (keysym == XK_v || keysym == XK_V))
                {
                    XConvertSelection(disp, XInternAtom(disp, "CLIPBOARD", False),
                                      XInternAtom(disp, "UTF8_STRING", False),
                                      XInternAtom(disp, "PASTE_BUFFER", False),
                                      win, CurrentTime);
                    continue;
                }

                // Ctrl+R search
                if ((event.xkey.state & ControlMask) && (keysym == XK_r || keysym == XK_R))
                {
                    T.displayBuffer.push_back("REC:Enter search term:");
                    T.input.clear();
                    string sdisp = editPWD(T.cwd);
                    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                    T.currentCursorPosition = 0;
                    T.searchFlag = true;
                    makeScreen(win, gc, font, T);
                    continue;
                }

                // Ctrl+A start
                if ((event.xkey.state & ControlMask) && (keysym == XK_a || keysym == XK_A))
                {
                    if (T.searchFlag)
                        T.currentCursorPosition = 0;
                    else
                        T.currentCursorPosition = T.count;
                    makeScreen(win, gc, font, T);
                    continue;
                }
                // Ctrl+E end
                if ((event.xkey.state & ControlMask) && (keysym == XK_e|| keysym == XK_E))
                {
                    T.currentCursorPosition = (int)T.input.size();
                    makeScreen(win, gc, font, T);
                    continue;
                }
                if ((event.xkey.state & ControlMask) && keysym == XK_Tab)
                {
                    if (!tabs.empty())
                    {
                        tabActive = (tabActive + 1) % tabs.size();
                        makeNavBar(win, gc, wa.width);
                        makeTabs(win, gc, font);
                        makeScreen(win, gc, font, tabs[tabActive]);
                    }
                    continue;
                }

                // Ctrl + Shift + Tab → previous tab
                if ((event.xkey.state & ControlMask) && (event.xkey.state & ShiftMask) && keysym == XK_ISO_Left_Tab)
                {
                    if (!tabs.empty())
                    {
                        tabActive = (tabActive - 1 + tabs.size()) % tabs.size();
                        makeNavBar(win, gc, wa.width);
                        makeTabs(win, gc, font);
                        makeScreen(win, gc, font, tabs[tabActive]);
                    }
                    continue;
                }
                // Tab completion (your logic)
                if (keysym == XK_Tab)
                {
                    if (!T.input.empty())
                    {
                        T.recommFlag = true;
                        T.query = extractQuery(T.input);
                        if (T.query == T.input && T.query.rfind("./", 0) == 0)
                            T.query = T.query.substr(2);
                        T.forRec = T.input;

                        // list directory allCandidates under tab cwd
                        auto outputs = execInDir("ls", T.cwd);
                        vector<string> allCandidates;
                        for (auto &l : outputs)
                            if (!l.empty() && l.rfind("ERROR:", 0) != 0)
                                allCandidates.push_back(l);

                        T.recs = getRecommendations(T.query, allCandidates);
                        if (T.recs.empty())
                        {
                            T.recommFlag = false;
                        }
                        else if (T.recs.size() == 1)
                        {
                            T.input += T.recs[0].substr(T.query.size());
                            // update last prompt line
                            if (!T.displayBuffer.empty())
                                T.displayBuffer.back() += T.recs[0].substr(T.query.size());
                            T.recommFlag = false;
                            T.currentCursorPosition = (int)T.input.size();
                        }
                        else
                        {
                            for (size_t i = 0; i < T.recs.size(); i++)
                                T.showRec += to_string(i + 1) + ". " + T.recs[i] + "  ";
                            T.displayBuffer.push_back("REC:" + T.showRec);
                            T.displayBuffer.push_back("REC:Choose from above options:");
                            T.input.clear();
                            T.currentCursorPosition = 0;
                            T.showRec.clear();
                        }
                        makeScreen(win, gc, font, T);
                    }
                    continue;
                }
                // Ctrl + C handling for multiWatch stop
                // Unified Ctrl + C handling for normal commands and multiWatch
                if ((event.xkey.state & ControlMask) && (keysym == XK_c || keysym == XK_C))
                {
                    // Request stop from execute.cpp (async-safe)
                        getSigint();

                        // Give watcher thread a short timeout to finish its cleanup & restore buffer.
                        // We poll mwDone (set by execute.cpp when multiWatch ends).
                        for (int i = 0; i < 10; ++i)
                        {
                            if (mwDone.load())
                                break;
                            this_thread::sleep_for(chrono::milliseconds(50));
                        }

                        // Reset stop flags so the next command can run normally.
                        mwStopReq.store(false);
                        mwDone.store(false);
                        cmdRunning.store(false);

                        // Append ^C and prompt to screenBuffer and redraw
                        tabState &T2 = tabs[tabActive];
                        T2.displayBuffer.push_back("^C");

                        string sdisp = editPWD(T2.cwd);
                        string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                        T2.displayBuffer.push_back(prompt);
                        T2.input.clear();
                        T2.currentCursorPosition = 0;

                        makeScreen(win, gc, font, T2);
                        
                    continue;
                }

                // Printable handling via wbuf
                if ((status == XLookupChars || status == XLookupBoth) && len > 0)
                {
                    // ENTER
                    if (wbuf[0] == L'\r' || wbuf[0] == L'\n')
                    {
                        if (T.recommFlag)
                        {
                            int recIdx = min(getRecIdx(T.input), (int)T.recs.size()) - 1;
                            if (recIdx < 0)
                                recIdx = 0;
                            string rec = T.recs[recIdx];
                            T.input = T.forRec + rec.substr(T.query.size());

                            string sdisp = editPWD(T.cwd);
                            string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");

                            // remove the three pushed lines (list + "Choose..." + current line)
                            if (!T.displayBuffer.empty())
                            {
                                T.displayBuffer.pop_back();
                            }
                            if (!T.displayBuffer.empty())
                            {
                                T.displayBuffer.pop_back();
                            }
                            if (!T.displayBuffer.empty())
                            {
                                T.displayBuffer.pop_back();
                            }
                            T.displayBuffer.push_back(prompt + T.input);
                            T.currentCursorPosition = (int)T.input.size();
                            T.recommFlag = false;
                            continue;
                        }

                        if (T.searchFlag)
                        {
                            string search_res = searchFromHistory(T.input);
                            T.input.clear();
                            string sdisp = editPWD(T.cwd);
                            string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                            if (search_res != "No match for search term in history")
                            {
                                T.input = search_res;
                                T.multLineFlag = false;
                                for (char c : T.input)
                                    if (c == '"')
                                        T.multLineFlag = !T.multLineFlag;
                                T.displayBuffer.push_back(prompt + T.input);
                                T.currentCursorPosition = (int)T.input.size();
                            }
                            else
                            {
                                T.displayBuffer.push_back(search_res);
                                T.displayBuffer.push_back(prompt + T.input);
                                T.currentCursorPosition = 0;
                            }
                            T.searchFlag = false;
                            continue;
                        }

                        if (T.multLineFlag)
                        {
                            T.input.insert(T.input.begin() + T.currentCursorPosition, '\n');
                            T.currentCursorPosition++;
                            T.count = (int)T.input.size();
                            remakeScreenBuf();
                            makeScreen(win, gc, font, T);
                            continue;
                        }
                        else
                        {
                            if (!T.input.empty())
                            {
                                if (inputs.empty() || inputs.back() != T.input)
                                {
                                    storeHistory(T.input);
                                    inputs.push_back(T.input);
                                }
                            }
                            T.count = 0;
                            T.inpIdx = (int)inputs.size();

                            auto trimLocal = [](const string &s) -> string
                            {
                                size_t a = 0, b = s.size();
                                while (a < b && isspace((unsigned char)s[a]))
                                    ++a;
                                while (b > a && isspace((unsigned char)s[b - 1]))
                                    --b;
                                return s.substr(a, b - a);
                            };
                            string stripped = trimLocal(T.input);

                            if (stripped == "history")
                            {
                                ifstream in(historyPath);
                                if (!in)
                                {
                                    cerr << "Error: cannot open file\n";
                                }
                                else
                                {
                                    string line;
                                    while (getline(in, line))
                                        T.displayBuffer.push_back(line);
                                    in.close();
                                }
                            }
                            if (stripped == "clear")
                            {
                                T.displayBuffer.clear();
                                string sdisp = editPWD(T.cwd);
                                string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                                T.displayBuffer.push_back(prompt);
                                T.input.clear();
                                T.currentCursorPosition = 0;
                                T.multLineFlag = false;
                                int totalDisplayLines = makeScreen(win, gc, font, T);
                                T.scrlOffset = max(0, totalDisplayLines - seeRows);
                                T.userScrolled = false;
                                makeScreen(win, gc, font, T);
                                continue;
                            }
                            if (stripped.rfind("multiWatch", 0) == 0)
                                {
                                    size_t start = T.input.find('[');
                                    size_t end = T.input.find(']');
                                    if (start != string::npos && end != string::npos && end > start)
                                    {
                                        string inside = T.input.substr(start + 1, end - start - 1);
                                        vector<string> cmds;
                                        regex r("\"([^\"]+)\"");
                                        smatch m;
                                        string::const_iterator s(inside.cbegin());
                                        while (regex_search(s, inside.cend(), m, r))
                                        {
                                            cmds.push_back(m[1]);
                                            s = m.suffix().first;
                                        }

                                        if (cmds.empty())
                                        {
                                            T.displayBuffer.push_back("multiWatch: No valid commands found.");
                                        }
                                        else
                                        {
                                            // ✅ Copy old screen buffer safely
                                            vector<string> oldBuffer;
                                            oldBuffer.insert(oldBuffer.end(), T.displayBuffer.begin(), T.displayBuffer.end());

                                            // Clear screen for watch mode
                                            T.displayBuffer.clear();
                                            T.displayBuffer.push_back("multiWatch — starting...");

                                            // Mark multiwatch active
                                            mwStopReq.store(false);
                                            mwDone.store(false);

                                            // ✅ Pass oldBuffer to thread so it can restore later
                                            thread([cmds, tab_index = tabActive, oldBuffer]()
                                                   { multiWatchThreaded_using_pipes(cmds, tab_index, oldBuffer); })
                                                .detach();
                                        }
                                    }
                                    else
                                    {
                                        T.displayBuffer.push_back("Usage: multiWatch [\"cmd1\", \"cmd2\", ...]");
                                    }

                                    // Clear input for next command
                                    T.input.clear();
                                    T.currentCursorPosition = 0;
                                    break;
                                }
                            // execute in tab cwd
                            vector<string> outputs = execInDir(T.input, T.cwd);
                            T.input.clear();
                            T.currentCursorPosition = 0;

                            // Check if this command is multiWatch
                            bool isMultiWatch = (T.input.find("multiWatch") != string::npos);

                            // Push command output lines
                            for (const auto &line : outputs)
                                T.displayBuffer.push_back(line);

                            // Only show prompt if NOT multiWatch
                            if (!isMultiWatch)
                            {
                                string sdisp = editPWD(T.cwd);
                                string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                                T.displayBuffer.push_back(prompt);
                            }

                            int totalDisplayLines = makeScreen(win, gc, font, T);
                            if (!T.userScrolled)
                            {
                                T.scrlOffset = max(0, totalDisplayLines - seeRows);
                                makeScreen(win, gc, font, T);
                            }
                            continue;
                        }
                    }

                    // BACKSPACE
                    if (wbuf[0] == 8 || wbuf[0] == 127)
                    {
                        if ((T.searchFlag || T.recommFlag) && !T.input.empty())
                        {
                            string curr = T.displayBuffer.back();
                            T.displayBuffer.pop_back();
                            T.input.erase(T.input.begin() + T.currentCursorPosition - 1);
                            T.displayBuffer.push_back(curr.substr(0, curr.size() - 1));
                            T.currentCursorPosition--;
                            continue;
                        }

                        if (T.currentCursorPosition > 0)
                        {
                            if (T.input[T.currentCursorPosition - 1] == '"')
                                T.multLineFlag = !T.multLineFlag;
                            T.input.erase(T.input.begin() + T.currentCursorPosition - 1);
                            T.currentCursorPosition--;
                            remakeScreenBuf();
                            makeScreen(win, gc, font, T);
                        }
                        continue;
                    }

                    // Regular printable char
                    char ch = (char)wbuf[0];
                    if (T.recommFlag)
                    {
                        T.input.insert(T.input.begin() + T.currentCursorPosition, ch);
                        T.displayBuffer.back() += ch;
                        T.currentCursorPosition++;
                        continue;
                    }
                    if (T.searchFlag)
                    {
                        T.input.insert(T.input.begin() + T.currentCursorPosition, ch);
                        T.displayBuffer.back() += ch;
                        T.currentCursorPosition++;
                        continue;
                    }
                    if (isprint((unsigned char)ch) || ch == '\t')
                    {
                        if (ch == '"')
                            T.multLineFlag = !T.multLineFlag;
                        T.input.insert(T.input.begin() + T.currentCursorPosition, ch);
                        T.currentCursorPosition++;
                        remakeScreenBuf();
                        makeScreen(win, gc, font, T);
                        continue;
                    }
                }
            }
            else if (event.type == SelectionNotify)
            {
                if (!(tabActive >= 0 && tabActive < (int)tabs.size()))
                    continue;
                tabState &T = tabs[tabActive];

                if (event.xselection.selection == XInternAtom(disp, "CLIPBOARD", False))
                {
                    Atom type;
                    int format;
                    unsigned long nitems, bytes_after;
                    unsigned char *data = nullptr;

                    XGetWindowProperty(disp, win,
                                       XInternAtom(disp, "PASTE_BUFFER", False),
                                       0, (~0L), True, AnyPropertyType,
                                       &type, &format, &nitems, &bytes_after, &data);

                    if (data)
                    {
                        std::string clipText((char *)data, nitems);
                        XFree(data);

                        std::istringstream ss(clipText);
                        std::string line;
                        bool first = true;

                        while (std::getline(ss, line))
                        {
                            if (first)
                            {
                                if (!T.displayBuffer.empty())
                                    T.displayBuffer.back() += line;
                                T.input += line;
                                first = false;
                            }
                            else
                            {
                                T.displayBuffer.push_back(line);
                                T.input += '\n';
                                T.input += line;
                            }
                        }
                        T.currentCursorPosition = (int)T.input.size();
                        makeScreen(win, gc, font, T);
                    }
                }
            }
        } // while XPending
        // ===== drain multiWatch queue (main GUI thread MUST do this) =====
        {
            std::lock_guard<std::mutex> lk(mwQueueMutex);
            while (!mwQueue.empty())
            {
                auto msg = mwQueue.front();
                mwQueue.pop();

                if (msg.text == "__MULTIWATCH_DONE__")
                {
                    // MultiWatch finished → show prompt
                    if (msg.tabIdx >= 0 && msg.tabIdx < (int)tabs.size())
                    {
                        tabState &T = tabs[msg.tabIdx];
                        string sdisp = editPWD(T.cwd);
                        string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
                        T.displayBuffer.push_back(prompt);
                        makeScreen(win, gc, font, T);
                    }
                    continue;
                }

                if (msg.tabIdx >= 0 && msg.tabIdx < (int)tabs.size())
                {
                    tabs[msg.tabIdx].displayBuffer.push_back(msg.text);
                    if (msg.tabIdx == tabActive)
                        makeScreen(win, gc, font, tabs[tabActive]);
                }
            }
        }

        // blink active tab cursor only
        if (tabActive >= 0 && tabActive < (int)tabs.size())
        {
            tabState &T = tabs[tabActive];
            auto now = chrono::steady_clock::now();
            if (chrono::duration_cast<std::chrono::milliseconds>(now - T.lastBlink).count() > 500)
            {
                T.dispCursor = !T.dispCursor;
                T.lastBlink = now;
                makeScreen(win, gc, font, T);
            }
        }
        // tiny sleep
        this_thread::sleep_for(chrono::milliseconds(20));
    }

    if (xic)
        XDestroyIC(xic);
    if (xim)
        XCloseIM(xim);

    XUnmapWindow(disp, win);
    XDestroyWindow(disp, win);
    return;

    // hello
}