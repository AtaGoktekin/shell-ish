# shell-ish

Ata Berke Göktekin 80277

A Unix-style interactive shell implemented in C for COMP 304 Assignment 1.

## Repository

> **GitHub:** `https://github.com/AtaGoktekin/shell-ish`

## How to Build & Run

```bash
gcc -o shell-ish shellish-skeleton.c
./shell-ish
```

---

## Features

### Part I — Command Execution & Background Processes

Shell-ish executes standard Linux commands by manually resolving the `PATH` environment variable using `execv()` (not `execvp()`).

```bash
ls -la
mkdir test
date
gcc -o hello hello.c
```

Background execution with `&` — shell returns to prompt immediately:

```bash
sleep 10 &
# [background] PID: 12345
```

### Part II — I/O Redirection & Pipes

Output redirection (truncate):
```bash
ls -la >output.txt
```

Output redirection (append):
```bash
ls >>output.txt
```

Input redirection:
```bash
cat <output.txt
```

> **Note:** No space between redirection symbol and filename (e.g. `>file.txt` not `> file.txt`)

Piping — single and chained:
```bash
ls -la | grep shell
ls -la | grep shell | wc
cat /etc/passwd | cut -d : -f 1,6
```

### Part III — Builtin Commands

#### `cut` — Field Extractor

Reads lines from stdin and prints specified fields. Implemented in C.

Options:
- `-d <char>` or `--delimiter <char>` — field delimiter (default: TAB)
- `-f <list>` or `--fields <list>` — comma-separated list of field indices (1-based)

```bash
cat /etc/passwd | cut -d : -f 1,6
# root:/root
# bin:/bin
# ...

echo -e "a\tb\tc" | cut -f 2
# b
```

#### `chatroom` — Group Chat via Named Pipes

Simple multi-user chat using named pipes under `/tmp/chatroom-<roomname>/`.

```bash
chatroom <roomname> <username>
```

- Creates the room folder if it doesn't exist
- Creates a named pipe for the user
- Receives messages from others continuously (reader child process)
- Sends messages to all other users in the room
- Type `exit` to leave the room

Example (open two terminals):
```bash
# Terminal 1:
chatroom comp304 ali

# Terminal 2:
chatroom comp304 mehmet
```

#### `remind` — Terminal Reminder

Sets a reminder that appears after a specified number of seconds.

```bash
remind <seconds> <message>
```

Examples:
```bash
remind 10 tea time!
```

After the specified time:
```
--------------------
REMINDER: tea time!           
--------------------
```

How it works: `remind` forks a child process that calls `sleep()` for the given duration, then prints the message. The parent returns immediately so the shell stays responsive.

---

## File Structure

```
shellish/
├── shellish-skeleton.c   # full shell implementation
├── README.md             # this file
├── imgs/                 # screenshots of each feature
│   ├── part1_exec.png
│   ├── part1_background.png
│   ├── part2_redirection.png
│   ├── part2_pipe.png
│   ├── part3_cut.png
│   ├── part3_chatroom.png
│   └── part3_remind.png
```

---

## Notes

- Tested on macOS
- `cut` is implemented as a shell builtin (not an external binary)
- `chatroom` uses `mkfifo` for named pipes under `/tmp/`
- `remind` uses `fork` + `sleep` to avoid blocking the shell

## Assistance

During the development of this readme and bugs on the project, **ChatGPT** was used for assistance in the following areas:

- Debugging compiler errors and warnings (e.g. implicit function declarations, buffer truncation warnings)
- Fixing the `termios` canonical mode issue in the `chatroom` command
- Writing and formatting this README
