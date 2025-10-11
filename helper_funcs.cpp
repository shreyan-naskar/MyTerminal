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


const string FILENAME = "/home/shreyan10/Desktop/GITHUB/MyTerminal/input_log.txt";

string getQuery(string input)
{
    string query = "";
    for (auto c : input)
    {
        if (c == ' ')
        {
            query = "";
            continue;
        }
        query += c;
    }

    return query;
}

int getRecIdx(string inp)
{
    string recIdx = "";
    for (auto c : inp)
    {
        if (c >= 48 && c <= 57)
        {
            recIdx += c;
        }
        
    }

    return !recIdx.empty() ? stoi(recIdx) : 1;
}

vector<string> getRecomm(string query, vector<string> list)
{
    vector<string> recs;
    for (auto ele : list)
    {
        if (ele.rfind(query, 0) == 0)
        {
            recs.push_back(ele);
        }
    }

    return recs;
}

string stripQuotes(const string &s)
{
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// Compute length of common prefix of two strings
int commonPrefixLength(const string &a, const string &b)
{
    int len = min(a.size(), b.size());
    for (int i = 0; i < len; ++i)
        if (a[i] != b[i])
            return i;
    return len;
}

// Search history file for exact match or longest prefix (ignoring quotes)
string searchHistory(const string &input, const string &filename = FILENAME)
{
    ifstream in(filename);
    if (!in)
        return "No match for search term in history";

    vector<string> history;
    string line;

    while (getline(in, line))
    {
        // Find the first character that is NOT a digit or space
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos)
            history.push_back(line.substr(pos)); // push from first non-digit, non-space
        else
            history.push_back(""); // line was all digits/spaces
    }
    in.close();

    string exactMatch = "";
    vector<string> candidates;
    int maxPrefixLen = 0;

    // Traverse from the end (most recent first)
    for (auto it = history.rbegin(); it != history.rend(); ++it)
    {
        string cmd = *it;

        if (cmd == input)
        {
            exactMatch = cmd;
            break;
        }

        int prefixLen = commonPrefixLength(cmd, input);
        if (prefixLen > maxPrefixLen)
        {
            maxPrefixLen = prefixLen;
            candidates.clear();
            candidates.push_back(cmd);
        }
        else if (prefixLen == maxPrefixLen)
        {
            candidates.push_back(cmd);
        }
    }

    if (!exactMatch.empty())
        return exactMatch;

    if (maxPrefixLen >= 2)
        return candidates[0]; // most recent with longest prefix

    return "No match for search term in history";
}

// Store input (each line in quotes)
int getLastHistoryNumber()
{
    ifstream in(FILENAME);
    if (!in)
        return 0;

    string line;
    int lastNum = 0;
    while (getline(in, line))
    {
        istringstream iss(line);
        int num;
        if (iss >> num)
        {
            lastNum = max(lastNum, num);
        }
    }
    in.close();
    return lastNum;
}

// Store input with incremental history number (space separated)
void storeInput(const string &input)
{
    int histNum = getLastHistoryNumber() + 1;

    ofstream out(FILENAME, ios::app);
    if (!out)
    {
        cerr << "Error opening file for writing.\n";
        return;
    }
    out << "  " << histNum << "  " << input << endl; // tab-separated
    out.close();
}

// Load inputs into vector (ignoring history number)
vector<string> loadInputs()
{
    ifstream in(FILENAME);
    vector<string> inputs;
    if (!in)
        return inputs;

    string line;
    while (getline(in, line))
    {
        // Find the first character that is NOT a digit or space
        size_t pos = line.find_first_not_of(" 0123456789");
        if (pos != string::npos)
            inputs.push_back(line.substr(pos)); // push from first non-digit, non-space
        else
            inputs.push_back(""); // line was all digits/spaces
    }

    in.close();
    return inputs;
}
