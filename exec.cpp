#include "headers.cpp"

// ---------------- your original helpers (kept) ----------------

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
            return {"ERROR: cd: no such file or directory: " + path};
        return {""};
    }
    if (trimmed == "cd" || trimmed == "cd ~")
    {
        chdir(getenv("HOME"));
        return {""};
    }

    // ===== Split by | =====
    vector<string> pipeParts;
    {
        stringstream ss(cmd);
        string part;
        while (getline(ss, part, '|'))
        {
            part.erase(0, part.find_first_not_of(" \t"));
            part.erase(part.find_last_not_of(" \t") + 1);
            if (!part.empty())
                pipeParts.push_back(part);
        }
    }
    if (pipeParts.empty())
        return {""};

    int nParts = (int)pipeParts.size();
    int numPipes = max(0, nParts - 1);

    // create chain pipes
    vector<int> chainFds(2 * numPipes, -1);
    for (int i = 0; i < numPipes; ++i)
    {
        if (pipe(chainFds.data() + i * 2) < 0)
        {
            for (int j = 0; j < i; ++j) { close(chainFds[j*2]); close(chainFds[j*2+1]); }
            return {"ERROR: pipe creation failed"};
        }
    }

    // capture pipes for stdout & stderr
    int capture_out[2] = {-1,-1}, capture_err[2] = {-1,-1};
    if (pipe(capture_out) < 0) { for (int fd : chainFds) if (fd>=0) close(fd); return {"ERROR: capture_out pipe failed"}; }
    if (pipe(capture_err) < 0) { close(capture_out[0]); close(capture_out[1]); for (int fd : chainFds) if (fd>=0) close(fd); return {"ERROR: capture_err pipe failed"}; }

    vector<pid_t> pids;
    bool forkError = false;

    // fork children
    for (int i = 0; i < nParts; ++i)
    {
        pid_t pid = fork();
        if (pid < 0) { forkError = true; break; }

        if (pid == 0)
        {
            // child
            if (i > 0)
            {
                int in_fd = chainFds[(i-1)*2];
                dup2(in_fd, STDIN_FILENO);
            }
            if (i < numPipes)
            {
                int out_fd = chainFds[i*2 + 1];
                dup2(out_fd, STDOUT_FILENO);
            }
            else
            {
                dup2(capture_out[1], STDOUT_FILENO);
            }

            // send stderr to capture_err
            dup2(capture_err[1], STDERR_FILENO);

            // close inherited fds
            for (int fd : chainFds) if (fd >= 0) close(fd);
            close(capture_out[0]); close(capture_out[1]);
            close(capture_err[0]); close(capture_err[1]);

            execlp("bash", "bash", "-c", pipeParts[i].c_str(), (char*)NULL);
            perror("execlp failed");
            _exit(127);
        }
        else
        {
            pids.push_back(pid);
        }
    }

    if (forkError)
    {
        for (int fd : chainFds) if (fd >= 0) close(fd);
        close(capture_out[0]); close(capture_out[1]);
        close(capture_err[0]); close(capture_err[1]);
        for (pid_t p : pids) if (p>0) waitpid(p, nullptr, 0);
        return {"ERROR: fork failed"};
    }

    // parent closes chain fds and write-ends of capture pipes
    for (int fd : chainFds) if (fd >= 0) close(fd);
    close(capture_out[1]);
    close(capture_err[1]);

    // read both pipes with poll
    string outBuf, errBuf;
    const int BUF_SZ = 4096;
    char buffer[BUF_SZ];
    struct pollfd pfds[2];
    pfds[0].fd = capture_out[0]; pfds[0].events = POLLIN | POLLHUP | POLLERR;
    pfds[1].fd = capture_err[0]; pfds[1].events = POLLIN | POLLHUP | POLLERR;
    int active = 2;
    while (active > 0)
    {
        int r = poll(pfds, 2, -1);
        if (r < 0) { if (errno==EINTR) continue; break; }
        for (int i = 0; i < 2; ++i)
        {
            if (pfds[i].fd < 0) continue;
            if (pfds[i].revents & POLLIN)
            {
                ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                if (n > 0)
                {
                    if (i == 0) outBuf.append(buffer, n);
                    else errBuf.append(buffer, n);
                }
                else
                {
                    close(pfds[i].fd);
                    pfds[i].fd = -1;
                    active--;
                }
            }
            else if (pfds[i].revents & (POLLHUP|POLLERR))
            {
                while (true)
                {
                    ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                    if (n > 0) { if (i==0) outBuf.append(buffer,n); else errBuf.append(buffer,n); }
                    else { close(pfds[i].fd); pfds[i].fd = -1; active--; break; }
                }
            }
        }
    }
    if (pfds[0].fd >= 0) { close(pfds[0].fd); pfds[0].fd = -1; }
    if (pfds[1].fd >= 0) { close(pfds[1].fd); pfds[1].fd = -1; }

    // wait children and record exit statuses
    bool hadError = false;
    int lastStatus = 0;
    for (pid_t pid : pids)
    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { hadError = true; }
        else
        {
            lastStatus = status;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) hadError = true;
        }
    }

    // If anything was written to stderr, treat as error
    if (!errBuf.empty()) hadError = true;

    auto splitLines = [](const string &s)->vector<string>{
        vector<string> out; size_t pos=0;
        while (true)
        {
            size_t nl = s.find('\n', pos);
            if (nl == string::npos) break;
            out.push_back(s.substr(pos, nl-pos));
            pos = nl+1;
        }
        if (pos < s.size()) out.push_back(s.substr(pos));
        return out;
    };

    vector<string> outLines = splitLines(outBuf);
    vector<string> errLines = splitLines(errBuf);

    vector<string> result;

    if (hadError)
    {
        if (!errLines.empty())
        {
            for (auto &l : errLines) result.push_back(string("ERROR: ") + l);
        }
        else
        {
            if (!outLines.empty())
            {
                for (auto &l : outLines) result.push_back(string("ERROR: ") + l);
            }
            else
            {
                int exitCode = (WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : -1);
                result.push_back(string("ERROR: (process exited with code ") + to_string(exitCode) + ")");
            }
        }
    }
    else
    {
        result = std::move(outLines);
        if (result.empty()) result.push_back("");
    }

    return result;
}

// -------- tab-aware exec: same as yours, but with per-tab CWD isolation --------

static vector<string> execCommandInDir(const string &cmd, string &cwd_for_tab)
{
    if (cmd.empty())
        return {""};

    auto trim = [](string s){
        s.erase(0, s.find_first_not_of(" \t"));
        if (!s.empty()) s.erase(s.find_last_not_of(" \t") + 1);
        return s;
    };

    string trimmed = trim(cmd);

    // Built-in cd (tab-local)
    if (trimmed.rfind("cd ", 0) == 0)
    {
        string path = trim(trimmed.substr(3));
        string target;
        if (path == "~" || path.empty()) {
            const char* home = getenv("HOME");
            target = home ? string(home) : string("/");
        } else if (!path.empty() && path[0] == '/') {
            target = path;
        } else {
            target = cwd_for_tab + "/" + path;
        }

        char resolved[PATH_MAX];
        if (realpath(target.c_str(), resolved))
        {
            struct stat st{};
            if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode))
            {
                cwd_for_tab = resolved;
                return {""};
            }
        }
        return {string("ERROR: cd: no such file or directory: ") + path};
    }
    if (trimmed == "cd" || trimmed == "cd ~")
    {
        const char* home = getenv("HOME");
        if (home) cwd_for_tab = home;
        else cwd_for_tab = "/";
        return {""};
    }

    // Split pipeline
    vector<string> pipeParts;
    {
        stringstream ss(cmd);
        string part;
        while (getline(ss, part, '|'))
        {
            part = trim(part);
            if (!part.empty()) pipeParts.push_back(part);
        }
    }
    if (pipeParts.empty()) return {""};

    int nParts = (int)pipeParts.size();
    int numPipes = max(0, nParts - 1);

    vector<int> chainFds(2 * numPipes, -1);
    for (int i = 0; i < numPipes; ++i)
    {
        if (pipe(chainFds.data() + i * 2) < 0)
        {
            for (int j = 0; j < i; ++j) { close(chainFds[j*2]); close(chainFds[j*2+1]); }
            return {"ERROR: pipe creation failed"};
        }
    }

    int capture_out[2] = {-1,-1}, capture_err[2] = {-1,-1};
    if (pipe(capture_out) < 0) { for (int fd : chainFds) if (fd>=0) close(fd); return {"ERROR: capture_out pipe failed"}; }
    if (pipe(capture_err) < 0) { close(capture_out[0]); close(capture_out[1]); for (int fd : chainFds) if (fd>=0) close(fd); return {"ERROR: capture_err pipe failed"}; }

    vector<pid_t> pids;
    bool forkError = false;

    for (int i = 0; i < nParts; ++i)
    {
        pid_t pid = fork();
        if (pid < 0) { forkError = true; break; }

        if (pid == 0)
        {
            // CHILD: set CWD to tab's cwd before exec
            if (!cwd_for_tab.empty())
                chdir(cwd_for_tab.c_str());

            if (i > 0) dup2(chainFds[(i-1)*2], STDIN_FILENO);
            if (i < numPipes) dup2(chainFds[i*2 + 1], STDOUT_FILENO);
            else dup2(capture_out[1], STDOUT_FILENO);
            dup2(capture_err[1], STDERR_FILENO);

            for (int fd : chainFds) if (fd >= 0) close(fd);
            close(capture_out[0]); close(capture_out[1]);
            close(capture_err[0]); close(capture_err[1]);

            execlp("bash", "bash", "-c", pipeParts[i].c_str(), (char*)NULL);
            perror("execlp failed");
            _exit(127);
        }
        else
        {
            pids.push_back(pid);
        }
    }

    if (forkError)
    {
        for (int fd : chainFds) if (fd >= 0) close(fd);
        close(capture_out[0]); close(capture_out[1]);
        close(capture_err[0]); close(capture_err[1]);
        for (pid_t p : pids) if (p>0) waitpid(p, nullptr, 0);
        return {"ERROR: fork failed"};
    }

    for (int fd : chainFds) if (fd >= 0) close(fd);
    close(capture_out[1]);
    close(capture_err[1]);

    string outBuf, errBuf; const int BUF_SZ = 4096; char buffer[BUF_SZ];
    struct pollfd pfds[2];
    pfds[0].fd = capture_out[0]; pfds[0].events = POLLIN | POLLHUP | POLLERR;
    pfds[1].fd = capture_err[0]; pfds[1].events = POLLIN | POLLHUP | POLLERR;
    int active = 2;
    while (active > 0)
    {
        int r = poll(pfds, 2, -1);
        if (r < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < 2; ++i)
        {
            if (pfds[i].fd < 0) continue;
            if (pfds[i].revents & POLLIN)
            {
                ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                if (n > 0) { (i==0 ? outBuf : errBuf).append(buffer, n); }
                else { close(pfds[i].fd); pfds[i].fd = -1; active--; }
            }
            else if (pfds[i].revents & (POLLHUP | POLLERR))
            {
                while (true)
                {
                    ssize_t n = read(pfds[i].fd, buffer, BUF_SZ);
                    if (n > 0) { (i==0 ? outBuf : errBuf).append(buffer, n); }
                    else { close(pfds[i].fd); pfds[i].fd = -1; active--; break; }
                }
            }
        }
    }
    if (pfds[0].fd >= 0) { close(pfds[0].fd); pfds[0].fd = -1; }
    if (pfds[1].fd >= 0) { close(pfds[1].fd); pfds[1].fd = -1; }

    bool hadError = false;
    int lastStatus = 0;
    for (pid_t pid : pids)
    {
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { hadError = true; }
        else
        {
            lastStatus = status;
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) hadError = true;
        }
    }
    if (!errBuf.empty()) hadError = true;

    auto splitLines = [](const string &s)->vector<string>{
        vector<string> out; size_t pos=0;
        while (true)
        {
            size_t nl = s.find('\n', pos);
            if (nl == string::npos) break;
            out.push_back(s.substr(pos, nl-pos));
            pos = nl+1;
        }
        if (pos < s.size()) out.push_back(s.substr(pos));
        return out;
    };

    vector<string> outLines = splitLines(outBuf);
    vector<string> errLines = splitLines(errBuf);
    vector<string> result;

    if (hadError)
    {
        if (!errLines.empty()) { for (auto &l : errLines) result.push_back(string("ERROR: ") + l); }
        else if (!outLines.empty()) { for (auto &l : outLines) result.push_back(string("ERROR: ") + l); }
        else
        {
            int exitCode = (WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : -1);
            result.push_back(string("ERROR: (process exited with code ") + to_string(exitCode) + ")");
        }
    }
    else
    {
        result = std::move(outLines);
        if (result.empty()) result.push_back("");
    }

    return result;
}

