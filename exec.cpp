#include "headers.cpp"

// // ---------------- your original helpers (kept) ----------------

// forward declarations
struct TabState;
extern vector<TabState> tabs;

struct WatchMessage { string text; int tab_index; };
static mutex mw_queue_mutex;
static queue<WatchMessage> mw_queue;

static string getCurrentTime()
{
    time_t now = time(nullptr);
    struct tm tmbuf;
    localtime_r(&now, &tmbuf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmbuf);
    return string(buf);
}

// multiWatch stop requested by UI (run.cpp should set this on Ctrl+C for multiWatch)
atomic<bool> mw_stop_requested(false);

// For interrupting arbitrary running commands
static mutex current_pids_mutex;
static vector<pid_t> current_child_pids; // guarded by current_pids_mutex
static atomic<bool> cmd_running(false);

// This is an async-safe request flag set by the UI (or by signal handler).
// run-time loops will check this flag and kill children as soon as possible.
static volatile sig_atomic_t sigint_request_flag = 0;

// Called by your UI (run.cpp) when user presses Ctrl+C; keeps it async-safe.
extern "C" void notify_sigint_from_ui()
{
    sigint_request_flag = 1;
    // Also request multiWatch stop
    mw_stop_requested.store(true);
}

// A helper executed in runtime loops to handle an outstanding sigint_request_flag.
// It is safe to call from normal code (not from signal handler).
static void handle_pending_sigint()
{
    if (sigint_request_flag == 0) return;
    // clear the flag atomically
    sigint_request_flag = 0;

    // Kill all children we've recorded
    lock_guard<mutex> lk(current_pids_mutex);
    for (pid_t p : current_child_pids)
    {
        if (p > 0)
            kill(p, SIGINT);
    }
    // Leave current_child_pids in place until parent reaps/waits them.
}

// =====================================================================
//                      ORIGINAL execCommand()
// (kept behaviorally the same; this function is used by some code paths)
// =====================================================================

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

    // Record pids so UI-triggered interrupts can kill them.
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids = pids;
        cmd_running.store(true);
    }

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
        // check for UI-requested SIGINT and handle it (kill children)
        handle_pending_sigint();

        int r = poll(pfds, 2, -1);
        if (r < 0) { if (errno==EINTR) { handle_pending_sigint(); continue; } break; }
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

    // clear current child list
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
        cmd_running.store(false);
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

    // Record pids so UI-triggered interrupts can kill them.
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids = pids;
        cmd_running.store(true);
    }

    string outBuf, errBuf; const int BUF_SZ = 4096; char buffer[BUF_SZ];
    struct pollfd pfds[2];
    pfds[0].fd = capture_out[0]; pfds[0].events = POLLIN | POLLHUP | POLLERR;
    pfds[1].fd = capture_err[0]; pfds[1].events = POLLIN | POLLHUP | POLLERR;
    int active = 2;
    while (active > 0)
    {
        // handle any pending UI-requested SIGINT
        handle_pending_sigint();

        int r = poll(pfds, 2, -1);
        if (r < 0) { if (errno == EINTR) { handle_pending_sigint(); continue; } break; }
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

    // clear current child list
    {
        lock_guard<mutex> lk(current_pids_mutex);
        current_child_pids.clear();
        cmd_running.store(false);
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

// =====================================================================
//                        MULTIWATCH SECTION
// =====================================================================

void handle_sigint_multiwatch(int) { mw_stop_requested.store(true); }

// multiWatch: line-by-line updates + runs in tab's cwd (read from tabs[])
// Note: multiWatch is stoppable by mw_stop_requested (UI should set it on Ctrl+C)
void multiWatchThreaded_using_pipes(const vector<string> &cmds, int tab_index)
{
    if (cmds.empty())
    {
        lock_guard<mutex> lk(mw_queue_mutex);
        mw_queue.push({"multiWatch: no commands provided", tab_index});
        return;
    }

    mw_stop_requested.store(false);

    {
        lock_guard<mutex> lk(mw_queue_mutex);
        mw_queue.push({"multiWatch: started — press Ctrl+C to stop.", tab_index});
    }

    while (!mw_stop_requested.load())
    {
        string tab_cwd;
        if (tab_index >= 0 && tab_index < (int)tabs.size())
        {
            // tab_cwd = tabs[tab_index].cwd;
        }

        vector<thread> workers;

        for (const auto &cmd : cmds)
        {
            workers.emplace_back([&, cmd]() {
                int pipefd[2];
                if (pipe(pipefd) < 0) return;

                pid_t pid = fork();
                if (pid == 0)
                {
                    // CHILD
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    dup2(pipefd[1], STDERR_FILENO);

                    if (!tab_cwd.empty())
                        chdir(tab_cwd.c_str());

                    execlp("bash", "bash", "-c", cmd.c_str(), (char *)NULL);
                    _exit(127);
                }
                else if (pid > 0)
                {
                    // PARENT
                    close(pipefd[1]);
                    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);

                    {
                        lock_guard<mutex> lk(current_pids_mutex);
                        current_child_pids.push_back(pid);
                        cmd_running.store(true);
                    }

                    string outBuf;
                    const int BUF_SZ = 4096;
                    char buf[BUF_SZ];
                    struct pollfd pfd{pipefd[0], POLLIN | POLLHUP | POLLERR, 0};
                    bool done = false;

                    while (!done && !mw_stop_requested.load())
                    {
                        handle_pending_sigint();

                        int r = poll(&pfd, 1, 200);
                        if (r > 0 && (pfd.revents & POLLIN))
                        {
                            ssize_t n = read(pipefd[0], buf, BUF_SZ);
                            if (n > 0)
                                outBuf.append(buf, n);
                            else if (n == 0)
                                done = true;
                        }
                        else if (pfd.revents & (POLLHUP | POLLERR))
                            done = true;
                    }

                    close(pipefd[0]);
                    waitpid(pid, nullptr, 0);

                    {
                        lock_guard<mutex> lk(current_pids_mutex);
                        current_child_pids.erase(
                            remove(current_child_pids.begin(), current_child_pids.end(), pid),
                            current_child_pids.end());
                        if (current_child_pids.empty())
                            cmd_running.store(false);
                    }

                    // Push output immediately
                    vector<string> lines;
                    stringstream ss(outBuf);
                    string line;
                    while (getline(ss, line))
                        lines.push_back(line);
                    if (lines.empty()) lines.push_back("(no output)");

                    lock_guard<mutex> qlk(mw_queue_mutex);
                    ostringstream header;
                    header << "\"" << cmd << "\" , " << getCurrentTime() << " :";
                    mw_queue.push({header.str(), tab_index});
                    mw_queue.push({"----------------------------------------------------", tab_index});
                    for (auto &l : lines) mw_queue.push({l, tab_index});
                    mw_queue.push({"----------------------------------------------------", tab_index});
                }
            });
        }

        // Wait for all commands to finish before next cycle
        for (auto &t : workers)
            if (t.joinable())
                t.join();

        if (mw_stop_requested.load()) break;

        this_thread::sleep_for(chrono::seconds(2));
    }

    {
        lock_guard<mutex> lk(mw_queue_mutex);
        // mw_queue.push({"multiWatch: stopped. Returning to prompt...", tab_index});
    }
}