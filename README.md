# MyTerminal

A lightweight graphical terminal emulator built in C++ with X11.

`MyTerminal` provides a custom terminal UI with tabbed sessions, shell command execution, command history, autocomplete, and a live multi-command watcher mode.

## Highlights

- X11-based terminal window with custom-rendered UI
- Multiple tabs with independent working directories
- Shell command execution via `bash -c`
- Pipeline and redirection support (`|`, `<`, `>`)
- Command history persistence (`input_log.txt`)
- Reverse history search (`Ctrl+R`)
- Filename autocomplete (`Tab`)
- Clipboard paste (`Ctrl+V`)
- `multiWatch` mode for repeatedly displaying outputs from multiple commands
- Scrollback + keyboard and mouse navigation

## Tech Stack

- Language: C++
- Windowing: X11 (`Xlib`)
- Process control: `fork`, `exec`, `pipe`, `poll`, `waitpid`
- Build: `make` + `g++`

## Repository Layout

- `termgui.cpp`: entry point and X display setup
- `run.cpp`: event loop, keyboard/mouse handling, interaction flow
- `draw.cpp`: window drawing, tab UI, screen rendering
- `exec.cpp`: command execution, pipelines, per-tab cwd logic, `multiWatch`
- `helper_funcs.cpp`: history, search, and autocomplete helpers
- `headers.cpp`: includes and shared dependencies
- `input_log.txt`: persisted command history
- `Makefile`: build instructions

## Prerequisites

Linux environment with X11 development/runtime available:

- `g++`
- `make`
- `libX11` (headers + runtime)
- `bash`

Example (Debian/Ubuntu):

```bash
sudo apt update
sudo apt install -y build-essential libx11-dev bash
```

## Build

```bash
make
```

This produces the executable: `./termgui`.

## Run

```bash
./termgui /home/<your-user>
```

Example:

```bash
./termgui /home/shreyan
```

The startup path argument is currently expected by the program and is used for prompt path formatting.

## Usage

### Tab Management

- Click `+` to open a new tab
- Click tab `x` to close a tab
- Click a tab to switch
- `Ctrl+Tab`: next tab
- `Ctrl+Shift+Tab`: previous tab
- `Esc`: close current tab (or exit when only one tab remains)

### Editing and Navigation

- `Left` / `Right`: move cursor in input
- `Up` / `Down`: navigate command history
- `Ctrl+A`: move cursor to start of input
- `Ctrl+E`: move cursor to end of input
- `Ctrl+V`: paste clipboard text
- Mouse wheel / `PageUp` / `PageDown`: scroll output

### History and Autocomplete

- `history`: print stored command history
- `Ctrl+R`: search history
- `Tab`: autocomplete file/path candidates in current tab directory

### Multi-command Watch Mode

```bash
multiWatch ["cmd1", "cmd2", "cmd3"]
```

- Re-runs commands periodically and refreshes output
- Stop with `Ctrl+C`

## Notes and Current Limitations

- Target platform is Linux with X11 (not native Windows terminal behavior).
- Input rendering path is mostly single-byte oriented in parts of the UI; some Unicode scenarios may not render perfectly.
- The command parser is intentionally simple; complex shell quoting edge cases may behave differently than a full terminal emulator.
- The initial path argument is required by current startup code.

## Clean

```bash
make clean
```
