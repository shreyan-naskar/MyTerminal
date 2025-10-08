#include<iostream>
using namespace std;

string stripANSI(const string &s)
{
    static const regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
    return regex_replace(s, ansi, "");
}

vector<string> execCommand(const string &cmd)
{
    if (cmd.empty()) return {""};

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {"Error: failed to run command\n"};

    string result;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;

    pclose(pipe);

    // Remove ANSI sequences
    result = stripANSI(result);

    // Keep only printable + newline
    vector<string> cleaned;
    string clean = "";
    for (char c : result)
    {
        if (c == '\n' || c == '\r' || (c >= 32 && c < 127))
        {   
            if (c == '\r'){
                clean.push_back(' ');
                continue;
            }
            else if(c == '\n')
            {
                cleaned.push_back(clean);
                clean = "";
                continue;
            }
            clean.push_back(c);
        }
    }
    cleaned.push_back(clean);

    return cleaned;
}

string getPWD()
{
    char cwd[512];
    string currentDir;
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
    {
        currentDir = string(cwd);
    }
    return currentDir.substr(15);
}
