#include<iostream>
#include <sstream>
#include <fstream>
#include <fcntl.h>
#include<cctype>
#include<sys/types.h>
#include<sys/wait.h>
using namespace std;

string stripANSI(const string &s)
{
    static const regex ansi("\x1b\\[[0-9;]*[A-Za-z]");
    return regex_replace(s, ansi, "");
}

vector<string> execCommand(const string &cmd)
{   

    cout<<"lund";
    if (cmd.empty())
        return {""};

    // Handle built-in 'cd'
    if (cmd.rfind("cd ", 0) == 0)
    {
        string path = cmd.substr(3);
        if (chdir(path.c_str()) != 0)
            return {"cd: failed to change directory\n"};
        return {""};
    }
    if (cmd == "cd" || cmd == "cd ~")
    {
        chdir(getenv("HOME"));
        return {""};
    }

    // Tokenize command
    vector<string> args;
    istringstream iss(cmd);
    string token;
    while (iss >> token)
        args.push_back(token);
    if (args.empty())
        return {""};

    vector<char *> argv;
    for (auto &a : args)
        argv.push_back(&a[0]);
    argv.push_back(nullptr);

    // Temporary file for output
    const char *tempFile = "/tmp/cmd_output.txt";

    pid_t pid = fork();

    if (pid == 0)
    {
        // Child: redirect stdout + stderr to file
        int fd = open(tempFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
        {
            perror("open failed");
            _exit(1);
        }

        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        close(fd);

        execvp(argv[0], argv.data());

        perror("exec failed");
        _exit(1);
    }
    else if (pid > 0)
    {
        // Parent waits
        wait(NULL);
        ifstream file(tempFile);
        vector<string> lines;
        string line;

        while (getline(file, line))
        {
            lines.push_back(line);
        }

        file.close();
        return lines;
    }
    else
    {
        return {"fork failed\n"};
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
