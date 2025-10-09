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

string getPWD()
{
    char cwd[512];
    string currentDir;
    if (getcwd(cwd, sizeof(cwd)) != nullptr)
    {
        currentDir = string(cwd);
    }
    if (currentDir.size() < 15)
    {
        return currentDir;
    }
    return currentDir.substr(15);
}

vector<string> execCommand(const string &cmd)
{
    if (cmd.empty()) return {""};

    // Split by '|'
    string trimmed = cmd;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));
    trimmed.erase(trimmed.find_last_not_of(" \t") + 1);

    // Built-in 'cd'
    if (trimmed.rfind("cd ", 0) == 0)
    {
        string path = trimmed.substr(3);
        if (chdir(path.c_str()) != 0)
            return {"cd: failed to change directory"};
        return {""};
    }
    if (trimmed == "cd" || trimmed == "cd ~")
    {
        chdir(getenv("HOME"));
        return {""};
    }
    vector<string> pipeParts;
    stringstream ss(cmd);
    string segment;
    while (getline(ss, segment, '|'))
    {
        segment.erase(0, segment.find_first_not_of(" \t"));
        segment.erase(segment.find_last_not_of(" \t") + 1);
        pipeParts.push_back(segment);
    }

    int numPipes = pipeParts.size() - 1;
    vector<int> pipefd(2 * numPipes);

    // Create internal pipes
    for (int i = 0; i < numPipes; i++)
        pipe(pipefd.data() + i * 2);

    // Extra pipe to capture final output
    int capture[2];
    pipe(capture);

    vector<pid_t> pids;
    for (size_t i = 0; i < pipeParts.size(); i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            // Input from previous pipe
            if (i != 0) dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
            // Output to next pipe or to capture
            if (i != pipeParts.size() - 1)
                dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
            else
                dup2(capture[1], STDOUT_FILENO); // last command writes to capture

            // Close all pipes in child
            for (int j = 0; j < 2 * numPipes; j++) close(pipefd[j]);
            close(capture[0]);
            close(capture[1]);

            execl("/bin/bash", "bash", "-c", pipeParts[i].c_str(), nullptr);
            perror("exec failed");
            _exit(1);
        }
        else if (pid < 0)
        {
            perror("fork failed");
            return {"fork failed"};
        }
        else
            pids.push_back(pid);
    }

    // Parent closes write ends
    for (int j = 0; j < 2 * numPipes; j++) close(pipefd[j]);
    close(capture[1]); // close write end of capture pipe

    // Read final output from capture pipe
    vector<string> lines;
    char buffer[1024];
    ssize_t n;
    string partial;
    while ((n = read(capture[0], buffer, sizeof(buffer))) > 0)
    {
        partial.append(buffer, n);
        size_t pos;
        while ((pos = partial.find('\n')) != string::npos)
        {
            lines.push_back(partial.substr(0, pos));
            partial.erase(0, pos + 1);
        }
    }
    if (!partial.empty()) lines.push_back(partial);
    close(capture[0]);

    // Wait for all children
    for (pid_t pid : pids) waitpid(pid, nullptr, 0);

    return lines;
}