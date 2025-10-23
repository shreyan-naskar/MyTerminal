#include "headers.cpp"
#include "helper_funcs.cpp"

static Display *disp;
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

struct tabState
{
    // UI buffers / state
    vector<string> displayBuffer;
    string input;
    int currentCursorPosition = 0;
    bool searchFlag = false;
    bool recommFlag = false;
    string showRec = "";
    vector<string> recs;
    string query = "";
    string forRec = "";
    int inpIdx = 0;
    int scrlOffset = 0;
    bool userScrolled = false;
    bool multLineFlag = false;
    int count = 0;
    // cursor blink
    bool dispCursor = true;
    chrono::steady_clock::time_point lastBlink = chrono::steady_clock::now();
    // per-tab cwd
    string cwd = "/";
    // title
    string title;
};

// tab chrome

static int tabActive = -1;
static vector<tabState> tabs;

struct tabPosNavbar
{
    int x;
    int w;
    int xClose;
    int wClose;
    bool isPlus;
};

static const int SCROLL_STEP = 3; // lines per wheel/page step

// Globals shared across tabs
vector<string> inputs; // history (shared)

static Window makeWindow(int x, int y, int h, int w, int b)
{
    Window win;
    XSetWindowAttributes xwa;
    xwa.background_pixel = BlackPixel(disp, scr);
    xwa.border_pixel = WhitePixel(disp, scr); // white border
    xwa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;
    win = XCreateWindow(
        disp, root, x, y, w, h, b,
        DefaultDepth(disp, scr),
        InputOutput,
        DefaultVisual(disp, scr),
        CWBackPixel | CWBorderPixel | CWEventMask,
        &xwa);
    return win;
}

static int makeScreen(Window win, GC gc, XFontStruct *font,
                      tabState &T)
{
    // window metrics
    XWindowAttributes atrbs;
    XGetWindowAttributes(disp, win, &atrbs);
    int winWidth = atrbs.width;
    int winHeight = atrbs.height;

    // Clear only content area (below navbar)
    XClearArea(disp, win, 0, NAVBAR_H, winWidth, winHeight - NAVBAR_H, False);

    int lineH = font->ascent + font->descent;

    // margins inside content
    int marginLeft = 10;
    int marginTop = NAVBAR_H + 30;

    // allocate colors once
    static bool colorsInit = false;
    static unsigned long greenPixel = WhitePixel(disp, scr);
    static unsigned long whitePixel = WhitePixel(disp, scr);
    static unsigned long redPixel = WhitePixel(disp, scr);
    static unsigned long yellowPixel = WhitePixel(disp, scr);
    if (!colorsInit)
    {
        Colormap colormap = DefaultColormap(disp, scr);
        XColor green, white, red, yellow, exact;

        if (XAllocNamedColor(disp, colormap, "green", &green, &exact))
            greenPixel = green.pixel;

        if (XAllocNamedColor(disp, colormap, "white", &white, &exact))
            whitePixel = white.pixel;

        if (XAllocNamedColor(disp, colormap, "red", &red, &exact))
            redPixel = red.pixel;

        if (XAllocNamedColor(disp, colormap, "yellow", &yellow, &exact))
            yellowPixel = yellow.pixel;

        colorsInit = true;
    }

    const string defaultPrefix = "shre@Term:";

    struct dispLine
    {
        string text;
        int promptChars;
    };
    vector<dispLine> dispLines;

    for (const auto &orgLine : T.displayBuffer)
    {
        if (orgLine.empty())
        {
            dispLines.push_back({"", 0});
            continue;
        }

        size_t pos = 0;
        while (pos < orgLine.size())
        {
            int curWidth = 0;
            size_t start = pos;
            size_t len = 0;

            for (; pos < orgLine.size(); ++pos)
            {
                char c = orgLine[pos];
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

            string piece = orgLine.substr(start, len);
            int findInPiece = 0;
            if (start == 0 && orgLine.rfind(defaultPrefix, 0) == 0)
                findInPiece = (int)min<size_t>(len, defaultPrefix.size());

            dispLines.push_back({piece, findInPiece});
        }
    }

    int seeRows = max(1, (winHeight - marginTop) / lineH);

    int alllines = (int)dispLines.size();
    if (T.scrlOffset < 0)
        T.scrlOffset = 0;
    if (T.scrlOffset > max(0, alllines - seeRows))
        T.scrlOffset = max(0, alllines - seeRows);

    int start = T.scrlOffset;
    int end = min(alllines, T.scrlOffset + seeRows);

    for (int row = start; row < end; ++row)
    {
        int y = marginTop + (row - start) * lineH;
        int x = marginLeft;
        const dispLine &dl = dispLines[row];

        unsigned long color = whitePixel;
        string drawText = dl.text;

        if (drawText.rfind("ERROR:", 0) == 0)
        {
            color = redPixel;
            drawText = drawText.substr(7);
        }
        if (drawText.rfind("REC:", 0) == 0)
        {
            color = yellowPixel;
            drawText = drawText.substr(4);
        }

        if (dl.promptChars > 0)
        {
            string ppart = drawText.substr(0, dl.promptChars);
            XSetForeground(disp, gc, greenPixel);
            XDrawString(disp, win, gc, x, y, ppart.c_str(), (int)ppart.length());
            x += XTextWidth(font, ppart.c_str(), (int)ppart.length());

            string rpart = drawText.substr(dl.promptChars);
            if (!rpart.empty())
            {
                XSetForeground(disp, gc, color);
                XDrawString(disp, win, gc, x, y, rpart.c_str(), (int)rpart.length());
            }
        }
        else
        {
            XSetForeground(disp, gc, color);
            XDrawString(disp, win, gc, x, y, drawText.c_str(), (int)drawText.length());
        }
    }

    // Cursor
    if (T.dispCursor)
    {
        string sdisp = editPWD(T.cwd);
        string prompt = (sdisp == "/") ? "shre@Term:" + sdisp + "$ " : "shre@Term:~" + sdisp + "$ ";

        vector<string> lines;
        size_t pos = 0, last = 0;
        while ((pos = T.input.find('\n', last)) != string::npos)
        {
            lines.push_back(T.input.substr(last, pos - last));
            last = pos + 1;
        }
        lines.push_back(T.input.substr(last));

        int curLine = 0, curCol = T.currentCursorPosition;
        int counted = 0;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (T.currentCursorPosition <= counted + (int)lines[i].size())
            {
                curLine = (int)i;
                curCol = T.currentCursorPosition - counted;
                break;
            }
            counted += (int)lines[i].size() + 1;
        }

        string uptoCursor;
        int pxWidth = 0;

        if (curLine == 0)
        {
            if (T.searchFlag)
            {
                string searchPrompt = "Enter search term:";
                int promptWidth = XTextWidth(font, searchPrompt.c_str(), searchPrompt.size());
                uptoCursor = lines[curLine].substr(0, curCol);
                int textWidth = XTextWidth(font, uptoCursor.c_str(), uptoCursor.size());
                pxWidth = promptWidth + textWidth;
            }
            else if (T.recommFlag)
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
        int cursorLineIdx = alllines - ((int)lines.size() - curLine);

        if (cursorLineIdx >= start && cursorLineIdx < end)
        {
            int row = cursorLineIdx - start;
            int baselineY = contentYOffset + row * lineH;
            int yTop = baselineY - font->ascent;
            int yBottom = baselineY + font->descent;

            XSetForeground(disp, gc, WhitePixel(disp, scr));
            XDrawLine(disp, win, gc, cursorX, yTop, cursorX, yBottom);
        }
    }

    return (int)dispLines.size();
}

// navbar drawing
static void makeNavBar(Window win, GC gc, int windowW)
{
    XSetForeground(disp, gc, BlackPixel(disp, scr));
    XFillRectangle(disp, win, gc, 0, 0, windowW, NAVBAR_H);

    XSetForeground(disp, gc, WhitePixel(disp, scr));
    XDrawLine(disp, win, gc, 0, NAVBAR_H - 1, windowW, NAVBAR_H - 1);
}

// tab chrome
// struct tabPosNavbar { int x; int w; };
// static int tabActive = -1;

// Globals to track hover state (set these from MotionNotify handler)
int howerXClose = -1;
bool howerPlusTab = false;

static vector<tabPosNavbar> makeTabs(Window win, GC gc, XFontStruct *font)
{
    XWindowAttributes wa;
    XGetWindowAttributes(disp, win, &wa);
    int windowW = wa.width;

    vector<tabPosNavbar> pos;

    // Close window if no tabs
    if (tabs.empty())
    {
        XDestroyWindow(disp, win);
        XCloseDisplay(disp);
        exit(0);
    }

    int y = 6;
    int tabH = NAVBAR_H - 10;
    int rad = 10;
    int gap = 6;

    // --- Reserve space for "+" button ---
    int plusW = 50;
    int availableW = windowW - plusW - (gap * (int)tabs.size()) - 20;
    int totalTabs = max(1, (int)tabs.size());
    int tabW = availableW / totalTabs;

    for (size_t i = 0; i < tabs.size(); ++i)
    {
        string label = "TAB " + to_string((int)i + 1);
        int x = 10 + i * (tabW + gap);
        bool active = ((int)i == tabActive);

        unsigned long activeBg = 0xF7F9FF;
        unsigned long inactiveBg = 0x2C2F36;
        unsigned long activeText = 0x202020;
        unsigned long inactiveText = 0xEAEAEA;
        unsigned long borderColor = 0x000000;

        unsigned long bg = active ? activeBg : inactiveBg;
        unsigned long textc = active ? activeText : inactiveText;

        // --- Tab background ---
        XSetForeground(disp, gc, bg);
        XFillArc(disp, win, gc, x, y, rad * 2, rad * 2, 90 * 64, 90 * 64);
        XFillArc(disp, win, gc, x + tabW - rad * 2, y, rad * 2, rad * 2, 0, 90 * 64);
        XFillRectangle(disp, win, gc, x + rad, y, tabW - 2 * rad, tabH);
        XFillRectangle(disp, win, gc, x, y + rad, tabW, tabH - rad);

        // --- Active tab indicator ---
        if (active)
        {
            XSetForeground(disp, gc, 0xFF0000);
            XFillRectangle(disp, win, gc, x, y + tabH - 3, tabW, 3);
        }

        // --- Border ---
        XSetForeground(disp, gc, borderColor);
        XDrawRectangle(disp, win, gc, x, y, tabW - 1, tabH);

        // --- Label text ---
        XCharStruct fullTab;
        int dir, ascent, descent;
        XTextExtents(font, label.c_str(), (int)label.size(), &dir, &ascent, &descent, &fullTab);

        int textX = x + (tabW - fullTab.width) / 2;
        int textY = y + (tabH + ascent - descent) / 2 + 2;

        XSetForeground(disp, gc, textc);
        XDrawString(disp, win, gc, textX, textY, label.c_str(), (int)label.size());

        // --- Close button ---
        int closeSize = 18;
        int xClose = x + tabW - closeSize - 8;
        int closeY = y + (tabH - closeSize) / 2;

        unsigned long closeBg = (howerXClose == (int)i) ? 0xC0392B : (active ? 0xE74C3C : 0x555555);
        unsigned long close_fg = 0xFFFFFF;

        XSetForeground(disp, gc, closeBg);
        XFillArc(disp, win, gc, xClose, closeY, closeSize, closeSize, 0, 360 * 64);

        // --- Centered "X" ---
        string cross = "x";
        XCharStruct cross_fullTab;
        int dir2, ascent2, descent2;
        XTextExtents(font, cross.c_str(), cross.size(), &dir2, &ascent2, &descent2, &cross_fullTab);

        int cx = xClose + (closeSize - cross_fullTab.width) / 2;
        int cy = closeY + (closeSize + ascent2 - descent2) / 2;
        XSetForeground(disp, gc, close_fg);
        XDrawString(disp, win, gc, cx, cy, cross.c_str(), (int)cross.size());

        pos.push_back({x, tabW, xClose, closeSize, false});
    }

    // --- "+" button ---
    int plusX = windowW - plusW - 10;
    int plusY = y;
    unsigned long plusBg = howerPlusTab ? 0x1E8449 : 0x27AE60; // darker on hover

    XSetForeground(disp, gc, plusBg);
    XFillArc(disp, win, gc, plusX, plusY, rad * 2, rad * 2, 90 * 64, 90 * 64);
    XFillArc(disp, win, gc, plusX + plusW - rad * 2, plusY, rad * 2, rad * 2, 0, 90 * 64);
    XFillRectangle(disp, win, gc, plusX + rad, plusY, plusW - 2 * rad, tabH);
    XFillRectangle(disp, win, gc, plusX, plusY + rad, plusW, tabH - rad);

    XSetForeground(disp, gc, 0x000000);
    XDrawRectangle(disp, win, gc, plusX, plusY, plusW - 1, tabH);

    // --- Centered "+" ---
    string plus = "+";
    XCharStruct pFullTab;
    int dir_p, ascent_p, descent_p;
    XTextExtents(font, plus.c_str(), (int)plus.size(), &dir_p, &ascent_p, &descent_p, &pFullTab);

    int px = plusX + (plusW - pFullTab.width) / 2;
    int py = plusY + (tabH + ascent_p - descent_p) / 2 + 2;
    XDrawString(disp, win, gc, px, py, plus.c_str(), (int)plus.size());

    pos.push_back({plusX, plusW, plusX, plusW, true});

    return pos;
}

int navbarHit(int mx, int my, const vector<tabPosNavbar> &pos, int *outIdx = nullptr)
{

    for (size_t i = 0; i < pos.size(); ++i)
    {
        const auto &tp = pos[i];

        // "+" button
        if (tp.isPlus)
        {
            if (mx >= tp.x && mx <= tp.x + tp.w && my >= 6 && my <= NAVBAR_H - 6)
                return -2;
            continue;
        }

        // Close (×) button
        if (mx >= tp.xClose && mx <= tp.xClose + tp.wClose && my >= 6 && my <= NAVBAR_H - 6)
        {
            if (outIdx)
                *outIdx = (int)i;
            return -3;
        }

        // Tab body
        if (mx >= tp.x && mx <= tp.x + tp.w && my >= 6 && my <= NAVBAR_H - 6)
        {
            if (outIdx)
                *outIdx = (int)i;
            return (int)i;
        }
    }

    return -1; // nothing
}

// add new tab
static void addTab(const string &initial_cwd = "/")
{
    tabState t;
    t.cwd = initial_cwd;
    string sdisp = editPWD(t.cwd);
    string prompt = (sdisp == "/") ? ("shre@Term:" + sdisp + "$ ") : ("shre@Term:~" + sdisp + "$ ");
    t.displayBuffer.push_back(prompt);
    t.inpIdx = (int)inputs.size() - 1;
    t.title = "Tab " + to_string((int)tabs.size() + 1);
    tabs.push_back(std::move(t));
    tabActive = (int)tabs.size() - 1;
}
