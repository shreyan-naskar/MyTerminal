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

vector<string> execCommand(const string &cmd)
{
    if (cmd.empty())
        return {""};

    // Handle 'cd' separately
    if (cmd.rfind("cd ", 0) == 0)
    {
        string path = cmd.substr(3);
        if (chdir(path.c_str()) != 0)
            return {"cd: failed to change directory"};
        return {""};
    }
    if (cmd == "cd" || cmd == "cd ~")
    {
        chdir(getenv("HOME"));
        return {""};
    }

    // Use temporary file for capturing output
    const char *tempFile = "/tmp/cmd_output.txt";

    pid_t pid = fork();

    if (pid == 0)
    {
        // Child: redirect stdout/stderr
        int fd = open(tempFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            perror("open failed");
            _exit(1);
        }

        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        // Let bash parse the command string
        execl("/bin/bash", "bash", "-c", cmd.c_str(), nullptr);

        perror("exec failed");
        _exit(1);
    }
    else if (pid > 0)
    {
        // Parent: wait and read
        wait(NULL);
        ifstream file(tempFile);
        vector<string> lines;
        string line;
        while (getline(file, line))
        {
            // cout<<line;
            lines.push_back(line); // preserves line breaks
        }
        file.close();
        return lines;
    }
    else
    {
        return {"fork failed"};
    }
}

using namespace std;
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
