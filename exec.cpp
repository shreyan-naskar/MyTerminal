#include<iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include<cctype>
#include<sys/types.h>
#include<sys/wait.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdio>
#include <err.h>
#include <string>
#include <chrono>
#include <vector>
#include <bits/stdc++.h>
#include <unistd.h>
#include <cctype>
#include <regex>
using namespace std;

string stripANSI(const string &s)
{
    static const regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
    return regex_replace(s, ansi, "");
}

#include <bits/stdc++.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
using namespace std;

vector<string> execCommand(const string &cmd)
{
    if (cmd.empty())
        return {""};

    // Trim
    string trimmed = cmd;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

    // ===== Built-in cd =====
    if (trimmed.rfind("cd ", 0) == 0)
    {
        string path = trimmed.substr(3);
        path.erase(0, path.find_first_not_of(" \t"));
        path.erase(path.find_last_not_of(" \t") + 1);
        if (chdir(path.c_str()) != 0)
            return {"cd: no such file or directory: " + path};
        return {""};
    }
    if (trimmed == "cd" || trimmed == "cd ~")
    {
        chdir(getenv("HOME"));
        return {""};
    }

    // ===== Split by | =====
    vector<string> pipeParts;
    stringstream ss(cmd);
    string part;
    while (getline(ss, part, '|'))
    {
        part.erase(0, part.find_first_not_of(" \t"));
        part.erase(part.find_last_not_of(" \t") + 1);
        if (!part.empty())
            pipeParts.push_back(part);
    }

    int numPipes = pipeParts.size() - 1;
    vector<int> pipefd(2 * numPipes);
    for (int i = 0; i < numPipes; ++i)
        if (pipe(pipefd.data() + i * 2) < 0)
            return {"pipe creation failed"};

    // Final capture pipe for combined stdout+stderr
    int capture[2];
    if (pipe(capture) < 0)
        return {"capture pipe failed"};

    vector<pid_t> pids;

    // ===== Fork children =====
    for (int i = 0; i < (int)pipeParts.size(); ++i)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            // ===== CHILD =====
            // Redirect input/output properly before exec
            if (i > 0)
                dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
            if (i < numPipes)
                dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
            else
                dup2(capture[1], STDOUT_FILENO);

            // Redirect stderr to same place
            dup2(STDOUT_FILENO, STDERR_FILENO);

            // Close all inherited pipe ends
            for (int j = 0; j < 2 * numPipes; ++j)
                close(pipefd[j]);
            close(capture[0]);
            close(capture[1]);

            // Run command
            execlp("/bin/bash", "bash", "-c", pipeParts[i].c_str(), (char *)NULL);
            perror("exec failed");
            _exit(127);
        }
        else if (pid > 0)
            pids.push_back(pid);
        else
            return {"fork failed"};
    }

    // ===== PARENT =====
    // Close unused FDs
    for (int j = 0; j < 2 * numPipes; ++j)
        close(pipefd[j]);
    close(capture[1]);

    // Capture output
    vector<string> lines;
    string bufferStr;
    char buf[1024];
    ssize_t n;

    while ((n = read(capture[0], buf, sizeof(buf))) > 0)
    {
        bufferStr.append(buf, n);
        size_t pos;
        while ((pos = bufferStr.find('\n')) != string::npos)
        {
            lines.push_back(bufferStr.substr(0, pos));
            bufferStr.erase(0, pos + 1);
        }
    }
    if (!bufferStr.empty())
        lines.push_back(bufferStr);

    close(capture[0]);

    // Wait for all
    for (pid_t pid : pids)
        waitpid(pid, nullptr, 0);

    if (lines.empty())
        lines.push_back("");

    return lines;
}