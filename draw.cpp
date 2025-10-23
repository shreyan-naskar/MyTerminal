#include "headers.cpp"
#include "helper_funcs.cpp"

static Display *dpy;
static int scr;
static Window root;

static const int ROWS = 24; // kept (not directly used, but retained)
#define POSX 200
#define POSY 200
#define WIDTH 900
#define HEIGHT 600
#define BORDER 8

// navbar constants
static const int NAVBAR_H = 40;
static const int TAB_PADDING = 8;
static const int TAB_SPACING = 4;

// ------------- Per-tab state ---------------

struct TabState {
    // UI buffers / state
    vector<string> screenBuffer;
    string input;
    int currCursorPos = 0;
    bool isSearching = false;
    bool inRec = false;
    string showRec = "";
    vector<string> recs;
    string query = "";
    string forRec = "";
    int inpIdx = 0;
    int scrollOffset = 0;
    bool userScrolled = false;
    bool isMultLine = false;
    int count = 0;
    // cursor blink
    bool showCursor = true;
    chrono::steady_clock::time_point lastBlink = chrono::steady_clock::now();
    // per-tab cwd
    string cwd = "/";
    // title
    string title;
};

// tab chrome

static int active_tab = -1;
static vector<TabState> tabs;

struct TabChromePos {
    int x;
    int w;
    int close_x;
    int close_w;
    bool is_plus;
};

static const int SCROLL_STEP = 3; // lines per wheel/page step

// Globals shared across tabs
vector<string> inputs; // history (shared)

static Window create_window(int x, int y, int h, int w, int b)
{
    Window win;
    XSetWindowAttributes xwa;
    xwa.background_pixel = BlackPixel(dpy, scr);
    xwa.border_pixel = WhitePixel(dpy, scr); // white border
    xwa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;
    win = XCreateWindow(
        dpy, root, x, y, w, h, b,
        DefaultDepth(dpy, scr),
        InputOutput,
        DefaultVisual(dpy, scr),
        CWBackPixel | CWBorderPixel | CWEventMask,
        &xwa);
    return win;
}

static int drawScreen(Window win, GC gc, XFontStruct *font,
                      TabState &T)
{
    // window metrics
    XWindowAttributes attrs;
    XGetWindowAttributes(dpy, win, &attrs);
    int winWidth = attrs.width;
    int winHeight = attrs.height;

    // Clear only content area (below navbar)
    XClearArea(dpy, win, 0, NAVBAR_H, winWidth, winHeight - NAVBAR_H, False);

    int lineHeight = font->ascent + font->descent;

    // margins inside content
    int marginLeft = 10;
    int marginTop = NAVBAR_H + 30;

    // allocate colors once
    static bool colorsInit = false;
    static unsigned long greenPixel = WhitePixel(dpy, scr);
    static unsigned long whitePixel = WhitePixel(dpy, scr);
    static unsigned long redPixel = WhitePixel(dpy, scr);
    if (!colorsInit)
    {
        Colormap colormap = DefaultColormap(dpy, scr);
        XColor green, white, red, exact;

        if (XAllocNamedColor(dpy, colormap, "green", &green, &exact))
            greenPixel = green.pixel;

        if (XAllocNamedColor(dpy, colormap, "white", &white, &exact))
            whitePixel = white.pixel;

        if (XAllocNamedColor(dpy, colormap, "red", &red, &exact))
            redPixel = red.pixel;

        colorsInit = true;
    }

    const string promptPrefix = "shre@Term:";

    struct DisplayLine
    {
        string text;
        int promptChars;
    };
    vector<DisplayLine> displayLines;

    for (const auto &origLine : T.screenBuffer)
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

            if (len == 0)
            {
                ++pos;
                ++len;
            }

            string piece = origLine.substr(start, len);
            int promptCharsInPiece = 0;
            if (start == 0 && origLine.rfind(promptPrefix, 0) == 0)
                promptCharsInPiece = (int)min<size_t>(len, promptPrefix.size());

            displayLines.push_back({piece, promptCharsInPiece});
        }
    }

    int visibleRows = max(1, (winHeight - marginTop) / lineHeight);

    int totalLines = (int)displayLines.size();
    if (T.scrollOffset < 0)
        T.scrollOffset = 0;
    if (T.scrollOffset > max(0, totalLines - visibleRows))
        T.scrollOffset = max(0, totalLines - visibleRows);

    int start = T.scrollOffset;
    int end = min(totalLines, T.scrollOffset + visibleRows);

    for (int row = start; row < end; ++row)
    {
        int y = marginTop + (row - start) * lineHeight;
        int x = marginLeft;
        const DisplayLine &dl = displayLines[row];

        unsigned long color = whitePixel;
        string textToDraw = dl.text;

        if (textToDraw.rfind("ERROR:", 0) == 0)
        {
            color = redPixel;
            textToDraw = textToDraw.substr(7);
        }

        if (dl.promptChars > 0)
        {
            string ppart = textToDraw.substr(0, dl.promptChars);
            XSetForeground(dpy, gc, greenPixel);
            XDrawString(dpy, win, gc, x, y, ppart.c_str(), (int)ppart.length());
            x += XTextWidth(font, ppart.c_str(), (int)ppart.length());

            string rpart = textToDraw.substr(dl.promptChars);
            if (!rpart.empty())
            {
                XSetForeground(dpy, gc, color);
                XDrawString(dpy, win, gc, x, y, rpart.c_str(), (int)rpart.length());
            }
        }
        else
        {
            XSetForeground(dpy, gc, color);
            XDrawString(dpy, win, gc, x, y, textToDraw.c_str(), (int)textToDraw.length());
        }
    }

    // Cursor
    if (T.showCursor)
    {
        string sdisp = formatPWD(T.cwd);
        string prompt = (sdisp == "/") ? "shre@Term:" + sdisp + "$ " : "shre@Term:~" + sdisp + "$ ";

        vector<string> lines;
        size_t pos = 0, last = 0;
        while ((pos = T.input.find('\n', last)) != string::npos)
        {
            lines.push_back(T.input.substr(last, pos - last));
            last = pos + 1;
        }
        lines.push_back(T.input.substr(last));

        int curLine = 0, curCol = T.currCursorPos;
        int counted = 0;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (T.currCursorPos <= counted + (int)lines[i].size())
            {
                curLine = (int)i;
                curCol = T.currCursorPos - counted;
                break;
            }
            counted += (int)lines[i].size() + 1;
        }

        string uptoCursor;
        int pxWidth = 0;

        if (curLine == 0)
        {
            if (T.isSearching)
            {
                string searchPrompt = "Enter search term:";
                int promptWidth = XTextWidth(font, searchPrompt.c_str(), searchPrompt.size());
                uptoCursor = lines[curLine].substr(0, curCol);
                int textWidth = XTextWidth(font, uptoCursor.c_str(), uptoCursor.size());
                pxWidth = promptWidth + textWidth;
            }
            else if (T.inRec)
            {
                string searchPrompt = "Choose from above options:";
                int promptWidth = XTextWidth(font, searchPrompt.c_str(), searchPrompt.size());
                uptoCursor = lines[curLine].substr(0, curCol);
                int textWidth = XTextWidth(font, uptoCursor.c_str(), uptoCursor.size());
                pxWidth = promptWidth + textWidth;
            }
            else
            {
                uptoCursor = prompt + lines[curLine].substr(0, curCol);
                pxWidth = XTextWidth(font, uptoCursor.c_str(), uptoCursor.size());
            }
        }
        else
        {
            uptoCursor = lines[curLine].substr(0, curCol);
            pxWidth = XTextWidth(font, uptoCursor.c_str(), uptoCursor.size());
        }

        int contentYOffset = NAVBAR_H + 30;
        int marginLeftX = 10;
        int cursorX = marginLeftX + pxWidth;
        int cursorLineIndex = totalLines - ((int)lines.size() - curLine);

        if (cursorLineIndex >= start && cursorLineIndex < end)
        {
            int row = cursorLineIndex - start;
            int baselineY = contentYOffset + row * lineHeight;
            int yTop = baselineY - font->ascent;
            int yBottom = baselineY + font->descent;

            XSetForeground(dpy, gc, WhitePixel(dpy, scr));
            XDrawLine(dpy, win, gc, cursorX, yTop, cursorX, yBottom);
        }
    }

    return (int)displayLines.size();
}

// navbar drawing
static void draw_navbar(Window win, GC gc, int win_w)
{
    XSetForeground(dpy, gc, BlackPixel(dpy, scr));
    XFillRectangle(dpy, win, gc, 0, 0, win_w, NAVBAR_H);

    XSetForeground(dpy, gc, WhitePixel(dpy, scr));
    XDrawLine(dpy, win, gc, 0, NAVBAR_H - 1, win_w, NAVBAR_H - 1);
}

// tab chrome
// struct TabChromePos { int x; int w; };
// static int active_tab = -1;

// Globals to track hover state (set these from MotionNotify handler)
int hovered_close_tab = -1;
bool hovered_plus = false;

static vector<TabChromePos> draw_tabs(Window win, GC gc, XFontStruct *font)
{
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, win, &wa);
    int win_w = wa.width;

    vector<TabChromePos> pos;

    // Close window if no tabs
    if (tabs.empty()) {
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
        exit(0);
    }

    int y = 6;
    int tab_h = NAVBAR_H - 10;
    int radius = 10;
    int gap = 6;

    // --- Reserve space for "+" button ---
    int plus_w = 50;
    int available_w = win_w - plus_w - (gap * (int)tabs.size()) - 20;
    int total_tabs = max(1, (int)tabs.size());
    int tab_w = available_w / total_tabs;

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        string label = "TAB " + to_string((int)i + 1);
        int x = 10 + i * (tab_w + gap);
        bool active = ((int)i == active_tab);

        unsigned long active_bg   = 0xF7F9FF;
        unsigned long inactive_bg = 0x2C2F36;
        unsigned long active_text = 0x202020;
        unsigned long inactive_text = 0xEAEAEA;
        unsigned long border_color = 0x808899;

        unsigned long bg    = active ? active_bg   : inactive_bg;
        unsigned long textc = active ? active_text : inactive_text;

        // --- Tab background ---
        XSetForeground(dpy, gc, bg);
        XFillArc(dpy, win, gc, x, y, radius * 2, radius * 2, 90 * 64, 90 * 64);
        XFillArc(dpy, win, gc, x + tab_w - radius * 2, y, radius * 2, radius * 2, 0, 90 * 64);
        XFillRectangle(dpy, win, gc, x + radius, y, tab_w - 2 * radius, tab_h);
        XFillRectangle(dpy, win, gc, x, y + radius, tab_w, tab_h - radius);

        // --- Active tab indicator ---
        if (active) {
            XSetForeground(dpy, gc, 0xFF0000);
            XFillRectangle(dpy, win, gc, x, y + tab_h - 3, tab_w, 3);
        }

        // --- Border ---
        XSetForeground(dpy, gc, border_color);
        XDrawRectangle(dpy, win, gc, x, y, tab_w - 1, tab_h);

        // --- Label text ---
        XCharStruct overall;
        int dir, ascent, descent;
        XTextExtents(font, label.c_str(), (int)label.size(), &dir, &ascent, &descent, &overall);

        int text_x = x + (tab_w - overall.width) / 2;
        int text_y = y + (tab_h + ascent - descent) / 2 + 2;

        XSetForeground(dpy, gc, textc);
        XDrawString(dpy, win, gc, text_x, text_y, label.c_str(), (int)label.size());

        // --- Close button ---
        int close_size = 18;
        int close_x = x + tab_w - close_size - 8;
        int close_y = y + (tab_h - close_size) / 2;

        unsigned long close_bg = (hovered_close_tab == (int)i) ? 0xC0392B : (active ? 0xE74C3C : 0x555555);
        unsigned long close_fg = 0xFFFFFF;

        XSetForeground(dpy, gc, close_bg);
        XFillArc(dpy, win, gc, close_x, close_y, close_size, close_size, 0, 360 * 64);

        // --- Centered "X" ---
        string cross = "X";
        XCharStruct cross_overall;
        int dir2, ascent2, descent2;
        XTextExtents(font, cross.c_str(), cross.size(), &dir2, &ascent2, &descent2, &cross_overall);

        int cx = close_x + (close_size - cross_overall.width) / 2;
        int cy = close_y + (close_size + ascent2 - descent2) / 2;
        XSetForeground(dpy, gc, close_fg);
        XDrawString(dpy, win, gc, cx, cy, cross.c_str(), (int)cross.size());

        pos.push_back({x, tab_w, close_x, close_size, false});
    }

    // --- "+" button ---
    int plus_x = win_w - plus_w - 10;
    int plus_y = y;
    unsigned long plus_bg = hovered_plus ? 0x1E8449 : 0x27AE60; // darker on hover

    XSetForeground(dpy, gc, plus_bg);
    XFillArc(dpy, win, gc, plus_x, plus_y, radius * 2, radius * 2, 90 * 64, 90 * 64);
    XFillArc(dpy, win, gc, plus_x + plus_w - radius * 2, plus_y, radius * 2, radius * 2, 0, 90 * 64);
    XFillRectangle(dpy, win, gc, plus_x + radius, plus_y, plus_w - 2 * radius, tab_h);
    XFillRectangle(dpy, win, gc, plus_x, plus_y + radius, plus_w, tab_h - radius);

    XSetForeground(dpy, gc, 0xFFFFFF);
    XDrawRectangle(dpy, win, gc, plus_x, plus_y, plus_w - 1, tab_h);

    // --- Centered "+" ---
    string plus = "+";
    XCharStruct p_overall;
    int dir_p, ascent_p, descent_p;
    XTextExtents(font, plus.c_str(), (int)plus.size(), &dir_p, &ascent_p, &descent_p, &p_overall);

    int px = plus_x + (plus_w - p_overall.width) / 2;
    int py = plus_y + (tab_h + ascent_p - descent_p) / 2 + 2;
    XDrawString(dpy, win, gc, px, py, plus.c_str(), (int)plus.size());

    pos.push_back({plus_x, plus_w, plus_x, plus_w, true});

    return pos;
}

// Returns:
//  -2 if "+" button clicked
//  -3 if a close button clicked (and sets out_index)
//  >=0 if a tab was clicked
//  -1 if nothing clicked
int navbar_hit_test(int mx, int my, const vector<TabChromePos> &pos,  int *out_index = nullptr)
{
    
    for (size_t i = 0; i < pos.size(); ++i)
    {
        const auto &tp = pos[i];

        // "+" button
        if (tp.is_plus)
        {
            if (mx >= tp.x && mx <= tp.x + tp.w && my >= 6 && my <= NAVBAR_H - 6)
                return -2;
            continue;
        }

        // Close (×) button
        if (mx >= tp.close_x && mx <= tp.close_x + tp.close_w && my >= 6 && my <= NAVBAR_H - 6)
        {
            if (out_index)
                *out_index = (int)i;
            return -3;
        }

        // Tab body
        if (mx >= tp.x && mx <= tp.x + tp.w && my >= 6 && my <= NAVBAR_H - 6)
        {
            if (out_index)
                *out_index = (int)i;
            return (int)i;
        }
    }

    return -1; // nothing
}

// add new tab
static void add_tab(const string &initial_cwd = "/")
{
    TabState t;
    t.cwd = initial_cwd;
    string sdisp = formatPWD(t.cwd);
    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
    t.screenBuffer.push_back(prompt);
    t.inpIdx = (int)inputs.size() - 1;
    t.title = "Tab " + to_string((int)tabs.size() + 1);
    tabs.push_back(std::move(t));
    active_tab = (int)tabs.size() - 1;
}

// ------------- main loop (adapted from your run()) -------------