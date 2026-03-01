#include <dirent.h>
#include <errno.h>
#include <fcntl.h>       
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>    // mkdir, mkfifo
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <signal.h>
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *pch = strtok(buf, splitters);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok(NULL, splitters);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

    // piping to another command
    if (strcmp(arg, "|") == 0) {
      struct command_t *c =
          (struct command_t *)malloc(sizeof(struct command_t));
      int l = strlen(pch);
      pch[l] = splitters[0]; // restore strtok termination
      index = 1;
      while (pch[index] == ' ' || pch[index] == '\t')
        index++; // skip whitespaces

      parse_command(pch + index, c);
      pch[l] = 0; // put back strtok termination
      command->next = c;
      continue;
    }

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}


// looks in path, if it finds will return 0 if not -1
int resolve_path(const char *name, char *result, size_t result_size) {
    
  if (name[0] == '/') {
    if (access(name, X_OK) == 0) {
      strncpy(result, name, result_size);

      return 0;


    }

    return -1;
  }

  char *path_env = getenv("PATH");
  if (!path_env){

     return -1;

  }
   

  char path_copy[4096];
  strncpy(path_copy, path_env, sizeof(path_copy));

  char *dir = strtok(path_copy, ":");
  while (dir != NULL) {


    snprintf(result, result_size, "%s/%s", dir, name);


    // can it run?
    if (access(result, X_OK) == 0)
      return 0; // found

    dir = strtok(NULL, ":");

  }

  return -1; // not found
}



//part 2 
void exec_command(struct command_t *command, int in_fd, int out_fd) {

  if (command->redirects[0]) {
    int fd = open(command->redirects[0], O_RDONLY);
    if (fd == -1) { perror("open input"); exit(1); }
    dup2(fd, STDIN_FILENO); 
    close(fd);
  } else if (in_fd != -1) {
    dup2(in_fd, STDIN_FILENO);
    close(in_fd);

  }



  if (command->redirects[1]) {
    int fd = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) { perror("open output"); exit(1); }
    dup2(fd, STDOUT_FILENO); 
    close(fd);
  }


  else if (command->redirects[2]) {
    int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) { perror("open append"); exit(1); }
    dup2(fd, STDOUT_FILENO);
    close(fd);
  } else if (out_fd != -1) {

    dup2(out_fd, STDOUT_FILENO);
    close(out_fd);

  }

  char resolved[4096];
  if (resolve_path(command->name, resolved, sizeof(resolved)) == 0) {
    execv(resolved, command->args);
  }
  printf("-%s: %s: command not found\n", sysname, command->name);
  exit(127);
}


//recursive 
void run_pipeline(struct command_t *cmd, int in_fd) {
  if (cmd->next == NULL) {
    pid_t pid = fork();
    if (pid == 0) {
      exec_command(cmd, in_fd, -1);
    } else {
      if (in_fd != -1) close(in_fd);
      waitpid(pid, NULL, 0);
    }
  } else {

    int pipefd[2];
    if (pipe(pipefd) == -1) { perror("pipe"); return; }

    pid_t pid = fork();
    if (pid == 0) {
    
      close(pipefd[0]);
      exec_command(cmd, in_fd, pipefd[1]);
    } else {
     
      close(pipefd[1]);
      if (in_fd != -1) close(in_fd);
      run_pipeline(cmd->next, pipefd[0]); // recursive
      waitpid(pid, NULL, 0);
    }
  }
}

//part 3
void builtin_cut(struct command_t *command) {
  char delimiter = '\t'; 
  int fields[256];      
  int field_count = 0;

  for (int i = 1; i < command->arg_count - 1; i++) {
    if (command->args[i] == NULL) break;

    if (strcmp(command->args[i], "-d") == 0 ||
        strcmp(command->args[i], "--delimiter") == 0) {
      if (i + 1 < command->arg_count - 1 && command->args[i + 1] != NULL) {
        delimiter = command->args[i + 1][0];
        i++;
      }
    }

    else if (strcmp(command->args[i], "-f") == 0 || strcmp(command->args[i], "--fields") == 0) {
      if (i + 1 < command->arg_count - 1 && command->args[i + 1] != NULL) {


        char fields_buf[1024];
        strncpy(fields_buf, command->args[i + 1], sizeof(fields_buf));
        char *token = strtok(fields_buf, ",");

        while (token != NULL && field_count < 256) {

          fields[field_count++] = atoi(token); 
          token = strtok(NULL, ",");


        }
        i++; 

      }



    }
  }


  if (field_count == 0) {
    fprintf(stderr, "cut: -f option required\n");
    return;
  }


  char line[4096];
  while (fgets(line, sizeof(line), stdin) != NULL) {
    int line_len = strlen(line);
    if (line_len > 0 && line[line_len - 1] == '\n')
      line[line_len - 1] = '\0';



    char *parts[1024];
    int part_count = 0;
    char line_copy[4096];
    strncpy(line_copy, line, sizeof(line_copy));

  

    char *p = line_copy;
    while (part_count < 1024) {

      parts[part_count++] = p;
      char *found = strchr(p, delimiter);
      if (found == NULL) break; 
      *found = '\0';          
      p = found + 1;    

    }

    for (int i = 0; i < field_count; i++) {
      int idx = fields[i] - 1;
      if (idx >= 0 && idx < part_count)
        printf("%s", parts[idx]);
      if (i < field_count - 1) printf("%c", delimiter);

    }
    printf("\n");
  }
}

//part 3 chatroom
void send_message(const char *room_path, const char *username, const char *msg) {
  DIR *dir = opendir(room_path);
  if (!dir) return;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0) continue;
    if (strcmp(entry->d_name, "..") == 0) continue;
    if (strcmp(entry->d_name, username) == 0) continue;

    pid_t pid = fork();
    if (pid == 0) {
      char pipe_path[1024];
      snprintf(pipe_path, sizeof(pipe_path), "%s/%s", room_path, entry->d_name);

    
      int fd = open(pipe_path, O_WRONLY | O_NONBLOCK);
      if (fd != -1) {
        write(fd, msg, strlen(msg));
        close(fd);
      }
      exit(0);
    }
  }
  closedir(dir);

  while (wait(NULL) > 0);
}


void reader_process(const char *my_pipe, const char *room, const char *username) {
  int fd = open(my_pipe, O_RDONLY);
  if (fd == -1) { perror("open pipe for reading"); exit(1); }

  char buf[4096];
  while (1) {
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      printf("\r%s\n[%s] %s > ", buf, room, username);
      fflush(stdout);
    }
  }
}

void builtin_chatroom(struct command_t *command) {
  if (command->arg_count < 4) {
    fprintf(stderr, "Usage: chatroom <roomname> <username>\n");
    return;
  }

  const char *roomname = command->args[1];
  const char *username = command->args[2];

  char room_path[1024];
  snprintf(room_path, sizeof(room_path), "/tmp/chatroom-%s", roomname);

  if (mkdir(room_path, 0777) == -1 && errno != EEXIST) {
    perror("mkdir room");
    return;
  }

  char my_pipe[2048];
  snprintf(my_pipe, sizeof(my_pipe), "%s/%s", room_path, username);

  if (mkfifo(my_pipe, 0666) == -1 && errno != EEXIST) {
    perror("mkfifo");
    return;
  }

  printf("Welcome to %s!\n", roomname);
  fflush(stdout);

  pid_t reader_pid = fork();
  if (reader_pid == 0) {
    reader_process(my_pipe, roomname, username);
    exit(0);
  }


  struct termios normal_termios;
  tcgetattr(STDIN_FILENO, &normal_termios);
  normal_termios.c_lflag |= (ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSANOW, &normal_termios);


  char input[4096];
  while (1) {
    printf("[%s] %s > ", roomname, username);
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) break;

    int len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') input[len - 1] = '\0';

    if (strcmp(input, "exit") == 0) break;

    if (strlen(input) == 0) continue;

    char formatted[8192];
    snprintf(formatted, sizeof(formatted), "[%s] %s: %s\n", roomname, username, input);

    send_message(room_path, username, formatted);

    printf("[%s] %s: %s\n", roomname, username, input);
    fflush(stdout);
  }

  kill(reader_pid, SIGTERM);
  waitpid(reader_pid, NULL, 0);

  printf("Left room %s.\n", roomname);
}

//remind 
void builtin_remind(struct command_t *command) {
  if (command->arg_count < 4) {
    fprintf(stderr, "Usage: remind <seconds> <message>\n");
    fprintf(stderr, "Example: remind 10 cay koydun\n");
    return;
  }

  int seconds = atoi(command->args[1]);
  if (seconds <= 0) {
    fprintf(stderr, "remind: seconds must be a positive number\n");
    return;
  }

  char message[4096] = "";
  for (int i = 2; i < command->arg_count - 1; i++) {
    if (command->args[i] == NULL) break;
    if (i > 2) strncat(message, " ", sizeof(message) - strlen(message) - 1);
    strncat(message, command->args[i], sizeof(message) - strlen(message) - 1);
  }

  pid_t pid = fork();
  if (pid == 0) {
    sleep(seconds);
    printf("\n\n--------------------\n");
    printf("REMINDER: %-14s\n", message);
    printf("--------------------\n\n");
    fflush(stdout);
    exit(0);
  } else if (pid > 0) {

    printf("Reminder set: \"%s\" in %d second(s)\n", message, seconds);
    waitpid(pid, NULL, WNOHANG);
  } else {
    perror("fork");
  }
}

int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0)
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0)
    return EXIT;

  if (strcmp(command->name, "cd") == 0) {
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }

  //part 3 buit in cut
  if (strcmp(command->name, "cut") == 0) {
    builtin_cut(command);
    return SUCCESS;
  }

  // part 3 b builtin chatroom
  if (strcmp(command->name, "chatroom") == 0) {
    builtin_chatroom(command);
    return SUCCESS;
  }

  // part 3 c builtin remind
  if (strcmp(command->name, "remind") == 0) {
    builtin_remind(command);
    return SUCCESS;
  }

  if (command->next != NULL) {
    if (command->background) {
      pid_t pid = fork();
      if (pid == 0) {

        run_pipeline(command, -1);
        exit(0);

      } else {
        printf("[background] PID: %d\n", pid);
        waitpid(pid, NULL, WNOHANG);
      }
    } else {
      run_pipeline(command, -1);
    }
    return SUCCESS;
  }

  pid_t pid = fork();
  if (pid == 0)  // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    //execvp(command->name, command->args); // exec+args+path

    /*
    // part 1 execv
    char resolved[4096];
    if (resolve_path(command->name, resolved, sizeof(resolved)) == 0) {
      execv(resolved, command->args);
    }
    printf("-%s: %s: command not found\n", sysname, command->name);
    exit(127);
          */

    // part 2: I/O redirection
    exec_command(command, -1, -1);

  } else if (pid > 0) {
    if (command->background) {
      printf("[background] PID: %d\n", pid);
      waitpid(pid, NULL, WNOHANG);
    } else {

      waitpid(pid, NULL, 0);

    }
    return SUCCESS;

  } else {
    perror("fork");
    return UNKNOWN;
  }

  return SUCCESS;
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t));

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}