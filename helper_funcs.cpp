#include "headers.cpp"

string historyPath = "./input_log.txt";
int len = 0;

string getPWD()
{
    char cwd[512];
    string currentDir;
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
        currentDir = string(cwd);
    if (int(currentDir.size()) < len)
        return currentDir;
    return currentDir.substr(len);
}

// format per-tab CWD the same way
static string editPWD(const string &cwd)
{
    if (int(cwd.size()) < len) return cwd;
    return cwd.substr(len);
}

static string getTimeNow()
{
    time_t now = time(nullptr);
    struct tm tmbuf;
    localtime_r(&now, &tmbuf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmbuf);
    return string(buf);
}

int getMatchingPrefixLength(const string &a, const string &b)
{
    int len = min(a.size(), b.size());
    for (int i = 0; i < len; ++i)
        if (a[i] != b[i])
            return i;
    return len;
}

string searchFromHistory(const string &input, const string &historyPath = historyPath)
{
    ifstream in(historyPath);
    if (!in)
        return "No match for search term in history";

    vector<string> history;
    string line;

    while (getline(in, line))
    {
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos) history.push_back(line.substr(pos));
        else history.push_back("");
    }
    in.close();

    string fullMatch = "";
    vector<string> allCandidates;
    int maxLenPrefix = 0;

    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        string cmd = *it;

        if (cmd == input)
        {
            fullMatch = cmd;
            break;
        }
        int prefixLen = getMatchingPrefixLength(cmd, input);
        if (prefixLen > maxLenPrefix)
        {
            maxLenPrefix = prefixLen;
            allCandidates.clear();
            allCandidates.push_back(cmd);
        }
        else if (prefixLen == maxLenPrefix)
        {
            allCandidates.push_back(cmd);
        }
    }

    if (!fullMatch.empty())
        return fullMatch;

    if (maxLenPrefix >= 2)
        return allCandidates[0];

    return "No match for search term in history";
}

int getLatestHistoryIdx()
{
    ifstream in(historyPath);
    if (!in)
        return 0;

    string line; int lastIdx = 0;
    while (getline(in, line))
    {
        istringstream iss(line);
        int num;
        if (iss >> num) lastIdx = max(lastIdx, num);
    }
    in.close();
    return lastIdx;
}

void storeHistory(const string &input)
{
    int histNum = getLatestHistoryIdx() + 1;
    ofstream out(historyPath, ios::app);
    if (!out)
    {
        cerr << "Error opening file for writing.\n";
        return;
    }
    out << "  " << histNum << "  " << input << endl;
    out.close();
}

vector<string> getHistory()
{
    ifstream in(historyPath);
    vector<string> inputs;
    if (!in) return inputs;

    string line;
    while (getline(in, line))
    {
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos)
            inputs.push_back(line.substr(pos));
        else
            inputs.push_back("");
    }

    in.close();
    return inputs;
}

string extractQuery(string input)
{
    string query = "";
    for (auto c : input)
    {
        if (c == ' ') { query = ""; continue; }
        query += c;
    }
    return query;
}

int getRecIdx(string inp)
{
    string recIdx = "";
    for (auto c : inp) if (c >= '0' && c <= '9') recIdx += c;
    return !recIdx.empty() ? stoi(recIdx) : 1;
}

vector<string> getRecommendations(string query, vector<string> list)
{
    vector<string> recs;
    for (auto &ele : list)
        if (ele.rfind(query, 0) == 0)
            recs.push_back(ele);
    return recs;
}

