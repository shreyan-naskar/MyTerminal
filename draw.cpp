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
static const int NAVBAR_H = 30;
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
struct TabChromePos { int x; int w; };
static int active_tab = -1;
static vector<TabState> tabs;

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


// draw one tab content (adapted from your drawScreen; clears only content area)
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

    struct DisplayLine { string text; int promptChars; };
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

            if (len == 0) { ++pos; ++len; }

            string piece = origLine.substr(start, len);
            int promptCharsInPiece = 0;
            if (start == 0 && origLine.rfind(promptPrefix, 0) == 0)
                promptCharsInPiece = (int)min<size_t>(len, promptPrefix.size());

            displayLines.push_back({piece, promptCharsInPiece});
        }
    }

    int visibleRows = max(1, (winHeight - marginTop) / lineHeight);

    int totalLines = (int)displayLines.size();
    if (T.scrollOffset < 0) T.scrollOffset = 0;
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

        if (textToDraw.rfind("ERROR:", 0) == 0) {
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
    XDrawLine(dpy, win, gc, 0, NAVBAR_H-1, win_w, NAVBAR_H-1);
}

static vector<TabChromePos> draw_tabs(Window win, GC gc, XFontStruct* font)
{
    XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
    int win_w = wa.width;

    vector<TabChromePos> pos;
    int x = 6;
    int y = 6;
    int tab_h = NAVBAR_H - 12;

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        string label = tabs[i].title.empty() ? ("Tab " + to_string((int)i+1)) : tabs[i].title;
        int w = XTextWidth(font, label.c_str(), (int)label.size()) + TAB_PADDING*2;
        unsigned long fill = ((int)i == active_tab) ? WhitePixel(dpy, scr) : BlackPixel(dpy, scr);
        unsigned long textc = ((int)i == active_tab) ? BlackPixel(dpy, scr) : WhitePixel(dpy, scr);

        // fill
        XSetForeground(dpy, gc, fill);
        XFillRectangle(dpy, win, gc, x, y, w, tab_h);
        // border
        XSetForeground(dpy, gc, WhitePixel(dpy, scr));
        XDrawRectangle(dpy, win, gc, x, y, w, tab_h);
        // text
        XSetForeground(dpy, gc, textc);
        XDrawString(dpy, win, gc, x + TAB_PADDING, y + tab_h - 4, label.c_str(), (int)label.size());

        pos.push_back({x, w});
        x += w + TAB_SPACING;
    }

    // plus button
    string plus = "+";
    int plus_w = XTextWidth(font, plus.c_str(), (int)plus.size()) + TAB_PADDING*2;
    int plus_x = win_w - plus_w - 6;

    XSetForeground(dpy, gc, WhitePixel(dpy, scr));
    XFillRectangle(dpy, win, gc, plus_x, y, plus_w, tab_h);
    XSetForeground(dpy, gc, WhitePixel(dpy, scr));
    XDrawRectangle(dpy, win, gc, plus_x, y, plus_w, tab_h);
    XSetForeground(dpy, gc, BlackPixel(dpy, scr));
    XDrawString(dpy, win, gc, plus_x + TAB_PADDING, y + tab_h - 4, plus.c_str(), (int)plus.size());

    return pos;
}

static int navbar_hit_test(Window win, int mx, int my, const vector<TabChromePos>& pos, XFontStruct* font)
{
    if (my < 6 || my > NAVBAR_H-6) return -2;
    for (size_t i = 0; i < pos.size(); ++i)
    {
        if (mx >= pos[i].x && mx <= pos[i].x + pos[i].w) return (int)i;
    }
    // plus
    XWindowAttributes wa; XGetWindowAttributes(dpy, win, &wa);
    string plus = "+";
    int plus_w = XTextWidth(font, plus.c_str(), (int)plus.size()) + TAB_PADDING*2;
    int plus_x = wa.width - plus_w - 6;
    int y = 6;
    int tab_h = NAVBAR_H - 12;
    if (mx >= plus_x && mx <= plus_x + plus_w && my >= y && my <= y + tab_h) return -1;
    return -2;
}

// add new tab
static void add_tab(const string& initial_cwd = "/")
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
