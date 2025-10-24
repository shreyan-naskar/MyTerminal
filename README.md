# Project: MyTerm - A Custom Terminal with X11GUI

A shell that will run as a standalone application program. The shell will accept user
commands (one line at a time), and execute the same.

---

## Functionalities Implemented : 

> **Graphical User Interface -**
- Designed using X11
- Handles keyboard inputs for all keys and typical key combinations
- Multiple independently running Tabs can be spawned and switched between themselves: Alt + Tab & Alt + Shift + Tab or through UI buttons

> **Run an external command -**
- Execute typical linux commands as on bash terminal
- For example: `ls`, `ls -l`, `ls -al`, `cd ..` and so on

> **Take multiline unicode input -**
- Multiline inputs are supported with the use of ".
- For Example:
  ```bash
  echo "Hello
  World"
- **Note:** Unicode inputs can not be displayed.

> **Run an external command by redirecting standard input from a file -** 
- Use `<` for input redirection.
- For example: `./a.out < input.txt`, `sort < somefile.txt` and so on

> **Run an external command by redirecting standard output to a file -**
- Use `>` for output redirection.
- For example: `./a.out > outfile.txt`, `ls > abc` and so on.
- Supports combination of input and output redirection.
- For example: `./a.out < infile.txt > outfile.txt` and so on.

> **Pipe Support -**
- Use symbol `|` to indicate pipe mode of execution.
- For example: `ls *.txt | wc -l`, `cat abc.c | sort | more`, `ls *.txt | xargs rm` and so on.

> **A new command "multiWatch" -**
- **Command:** `multiWatch ["cmd1", "cmd2", "cmd3",...]`
- Starts executing cmd1, cmd2, cmd3... parallelly with multiple processes.
- Execution ends after receiving Ctrl+C from the user.
- For example: `multiWatch [ "echo Hello", "ls", "cat a.txt" ]`

> **Line Navigation with Ctrl+A and Ctrl+E -**
- Pressing Ctrl+A moves the cursor to the start of the current line.
-​ Pressing Ctrl+E moves the cursor to the end of the current line.

> **Interrupting commands running in your shell (using signal call) -** 
- **Note:** Cound not be implemented.

> **A searchable shell history -**
- Maintains a history of the last 10000 commands run in shell.
- Command "history” which will show the most recent 1000 commands.
- To search press "Ctrl+r”, you will be prompted eith "Enter search term”. T
- Takes a string as an input from the user.

> **Implementing auto-complete feature for file names -**
- An auto-complete feature for the shell for the file names in the working directory of the shell.
- Write the first few letters of a file (in the same directory where the shell is running) and press Tab key.

## Bonus Functionalities Implemented :

- **Paste into the terminal from clipboard: "Ctrl + V" to paste single or multiline inputs.**
- **Up and Down arrow for previous inputs just like bash terminal.**
- **Right and Left arrow keys to edit in between already typed inputs**
- **Colour coding: Red - Errors, Yellow: Search History, Autocomplete, Green: Default Prompt ( eg: shre@Term$:)**

---

## 📦 Dependency Installation & opening shreTerm 

Copy and paste this in bash terminal

```bash
# execute the Makefile
make

# execute the .exe file with the command line arguement like /home/<user-name>
# for example: ./termgui /home/shreyan10
./termgui /home/<user-name>

