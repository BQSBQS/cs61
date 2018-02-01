#include "sh61.h"
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdbool.h>

// struct command
//    Data structure describing a command. Add your own stuff.

typedef struct command command;
typedef struct redirect redirect;

struct command
{
    int argc;              // number of arguments
    char **argv;           // arguments, terminated by NULL
    pid_t pid;             // process ID running this command, -1 if none
    bool background_proc;  // background proc
    int status;            // store status of waitpid
    int condition_type;    // condition type of current link
    command *next;         // next command
    command *prev;         // previous command
    redirect *redirection; // pointer to redirection linked list
};

// Handle Redirection commands
struct redirect
{
    char *file;     // File to redirect to/from
    char *token;    // Token
    redirect *next; // Next link in redirect linked list
};

pid_t foreground = 0;

// command_alloc()
//    Allocate and return a new command structure.

static command *command_alloc(void)
{
    command *c = (command *)malloc(sizeof(command));
    c->argc = 0;
    c->argv = NULL;
    c->pid = -1;
    c->background_proc = false;
    c->condition_type = TOKEN_NORMAL;
    c->redirection = NULL;
    c->next = NULL;
    c->prev = NULL;
    return c;
}

// Allocate and return a redirect structure
static redirect *alloc_redirect(void)
{
    redirect *red = (redirect *)malloc(sizeof(redirect));
    red->next = NULL;
    return red;
}

// Signal to Exit Shell
void signal_handler(int signal)
{
    // Exit Shell on CTRL + c
    _exit(0);
    // To stop "unused signal error"
    printf("%d\n", signal);
}

// Handle redirects like >, <, 2>
void handle_redirects(command *c)
{
    int file;
    redirect *red = c->redirection;
    while (red != NULL)
    {
        // If redirect is >
        if (strcmp(red->token, ">") == 0)
        {
            // Open the file and if it fails then throw error and exit(1)
            if ((file = open(red->file, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU)) == -1)
            {
                printf("%s ", strerror(errno));
                exit(1);
            }
            // Copy file to STDOUT
            dup2(file, STDOUT_FILENO);

            // Close file
            close(file);
        }

        // If redirect is <
        else if (strcmp(red->token, "<") == 0)
        {
            // Open the file and if it fails then throw error and exit(1)
            if ((file = open(red->file, O_RDONLY)) == -1)
            {
                printf("%s ", strerror(errno));
                exit(1);
            }
            // Copy file to stdin
            dup2(file, STDIN_FILENO);

            // Close file
            close(file);
        }

        // If redirect is 2>
        else if (strcmp(red->token, "2>") == 0)
        {
            // Open the file and if it fails then throw error and exit(1)
            if ((file = open(red->file, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU)) == -1)
            {
                printf("%s ", strerror(errno));
                exit(1);
            }
            // Copy file into STDERR
            dup2(file, STDERR_FILENO);

            // Close file
            close(file);
        }
        // Iterate and go to next link in redirect
        red = red->next;
    }
}
// command_free(c)
//    Free command structure `c`, including all its words.

static void command_free(command *c)
{
    command *temp = c;
    while (c != NULL)
    {
        for (int i = 0; i != c->argc; ++i)
        {
            free(c->argv[i]);
        }
        redirect *red;
        while (c->redirection != NULL)
        {
            red = c->redirection->next;
            free(c->redirection);
            c->redirection = red;
        }
        temp = c->next;
        free(c->argv);
        free(c);
        c = temp;
    }
}

// command_append_arg(c, word)
//    Add `word` as an argument to command `c`. This increments `c->argc`
//    and augments `c->argv`.

static void command_append_arg(command *c, char *word)
{
    c->argv = (char **)realloc(c->argv, sizeof(char *) * (c->argc + 2));
    c->argv[c->argc] = word;
    c->argv[c->argc + 1] = NULL;
    ++c->argc;
}

// COMMAND EVALUATION

// start_command(c, pgid)
//    Start the single command indicated by `c`. Sets `c->pid` to the child
//    process running the command, and returns `c->pid`.
//
//    PART 1: Fork a child process and run the command using `execvp`.
//    PART 5: Set up a pipeline if appropriate. This may require creating a
//       new pipe (`pipe` system call), and/or replacing the child process's
//       standard input/output with parts of the pipe (`dup2` and `close`).
//       Draw pictures!
//    PART 7: Handle redirections.
//    PART 8: The child process should be in the process group `pgid`, or
//       its own process group (if `pgid == 0`). To avoid race conditions,
//       this will require TWO calls to `setpgid`.

pid_t start_command(command *c, pid_t pgid)
{
    (void)pgid;
    // Your code here!

    // if the first argument in SHELL is "cd"(change directory) change to the second arguments PATH
    if (strcmp(c->argv[0], "cd") == 0)
    {
        c->status = chdir(c->argv[1]); // returns 0 on success and -1 on failure
        return 0;
    }

    // If the current process has no background processes running, set foreground to current pid (process id)
    if (c->background_proc == false)
    {
        // set foreground to current process id
        foreground = getpid();
    }

    // If in pipe, set foreground to current pgid
    // Previous command != NULL && Previous Commands condition != (|)TOKEN_PIPE && Current command == (|)TOKEN_PIPE && Current command has no process in the background
    if (c->prev != NULL && c->prev->condition_type != TOKEN_PIPE && c->condition_type == TOKEN_PIPE && c->background_proc == false)
    {
        // mark as current process group for terminal
        claim_foreground(foreground);
        setpgid(foreground, foreground);
    }

    // If no command to run
    if (c->argv == NULL)
    {
        return c->pid;
    }

    // Signal to Exit Shell
    signal(SIGINT, &signal_handler);

    // Status for waitpid
    int status = 0;

    // Count # of pipes
    int pipes = 0;

    // Temp command pointer to iterate over command linked list
    command *temp = c;

    // Iterate over linked list checking for pipes and adding to Pipes counter
    while (temp->condition_type == TOKEN_PIPE)
    {
        pipes++;
        temp = temp->next;
    }

    // If we are currently in a pipe
    if (c->condition_type == TOKEN_PIPE)
    {
        // Create 2 dimensional array of pipes, based on pipes counter
        int pipefd[pipes][2];

        // Create pipes based on pipes counter
        for (int i = 0; i < pipes; i++)
        {
            pipe(pipefd[i]);

            // Fork pipe
            if ((c->pid = fork()) == 0)
            {
                // Handle redirects
                handle_redirects(c);

                // copy/create write pipe
                close(pipefd[i][0]);
                dup2(pipefd[i][1], STDOUT_FILENO);
                close(pipefd[i][1]);

                // Check for Errors
                if (execvp(c->argv[0], c->argv) < 0)
                {
                    printf("Start Command ERROR: No command found in pipes while");
                    _exit(0);
                }
            }
            // If fork failed
            else if (c->pid == -1)
            {
                printf("Start Command ERROR: Fork failed");
            }

            else
            {
                // copy/create read pipe
                close(pipefd[i][1]);
                dup2(pipefd[i][0], STDIN_FILENO);
                close(pipefd[i][0]);

                // Iterate to the next command in linked list
                c = c->next;
            }
        }
    }

    // Fork the previous process
    c->pid = fork();

    // if child or parent
    switch (c->pid)
    {
    // child
    case 0:
        handle_redirects(c);
        // Check for errors
        if (execvp(c->argv[0], c->argv) == -1)
        {
            // fail the world ;__;
            _exit(1);
        }
        break;

    // Fork failed
    case -1:
        break;

    // Check for errors and wait for child
    default:
        if (waitpid(c->pid, &status, 0) < 0)
            printf("waitpid process error on process: %i", c->pid);
    }

    // Update current c status
    c->status = status;

    // return pid process
    return c->pid;
}

// run_list(c)
//    Run the command list starting at `c`.
//
//    PART 1: Start the single command `c` with `start_command`,
//        and wait for it to finish using `waitpid`.
//    The remaining parts may require that you change `struct command`
//    (e.g., to track whether a command is in the background)
//    and write code in run_list (or in helper functions!).
//    PART 2: Treat background commands differently.
//    PART 3: Introduce a loop to run all commands in the list.
//    PART 4: Change the loop to handle conditionals.
//    PART 5: Change the loop to handle pipelines. Start all processes in
//       the pipeline in parallel. The status of a pipeline is the status of
//       its LAST command.
//    PART 8: - Choose a process group for each pipeline.
//       - Call `claim_foreground(pgid)` before waiting for the pipeline.
//       - Call `claim_foreground(0)` once the pipeline is complete.

void run_list(command *c)
{
    // while there are commands still left to be run
    while (c != NULL)
    {
        // forked background process
        pid_t pid;

        // If there is currently a background command
        if (c->background_proc == true)
        {
            // fork the process
            if ((pid = fork()) == 0)
            {
                // While there still are background commands in this fork
                while (c != NULL)
                {
                    // start the command
                    start_command(c, 0);

                    // Skip over pipes since we check it in start command
                    while (c->condition_type == TOKEN_PIPE)
                    {
                        c = c->next;
                    }
                    // commands &&'d together
                    if (c->condition_type == TOKEN_AND)
                    {
                        // if exit status is zero
                        if (WEXITSTATUS(c->status) == 0)
                        {
                            // Iterate to next command
                            c = c->next;
                        }

                        // else skip that command
                        else
                        {
                            c->next->status = c->status;
                            c = c->next->next;
                        }
                    }

                    // // if commands were ||'d together
                    else if (c->condition_type == TOKEN_OR)
                    {
                        // if exit status is not zero go to next command
                        if (WEXITSTATUS(c->status) != 0)
                            c = c->next;
                        // else skip next command
                        else
                        {
                            c->next->status = c->status;
                            c = c->next->next;
                        }
                    }

                    // Else reached end of background commands
                    else
                    {
                        c = c->next;
                        return;
                    }
                }
            }
            // If fork failed
            else if (pid == -1)
            {
                printf("Run List ERROR: fork failed");
            }
            // Else check current parent does not have background command, else skip until one does not
            else
            {
                while (c->condition_type != TOKEN_BACKGROUND)
                {
                    c = c->next;
                }
                // Move to next link in parent
                c = c->next;
            }
        }
        // Don't fork if no background command
        else
        {
            // start the command
            start_command(c, 0);

            // Skip over pipes since we check it in start command
            while (c->condition_type == TOKEN_PIPE)
            {
                c = c->next;
            }

            // If commands &&'d together
            if (c->condition_type == TOKEN_AND)
            {
                // If exit status == 0 go to next command
                if (WEXITSTATUS(c->status) == 0)
                {
                    c = c->next;
                }
                // Skip over commands if they fail exit status to fit &&'d rules
                else
                {
                    while (c->condition_type == TOKEN_AND)
                    {
                        c = c->next;
                    }
                    c = c->next;
                }
            }
            // if commands ||'d together
            else if (c->condition_type == TOKEN_OR)
            {
                // To fit "||" rules it only continues when exit status is non zero, else skip
                if (WEXITSTATUS(c->status) != 0)
                {
                    // Go to next command
                    c = c->next;
                }
                // ELSE Skip commands until condition is not "||"
                else
                {
                    while (c->condition_type == TOKEN_OR)
                    {
                        c = c->next;
                    }
                    c = c->next;
                }
            }
            // ELSE go to the next command link
            else
            {
                c = c->next;
            }
        }
    }
}

// eval_line(c)
//    Parse the command list in `s` and run it via `run_list`.

void eval_line(const char *s)
{
    int type;
    char *token;
    // Your code here!

    // build the command
    command *c = command_alloc();

    // First link in command linked list
    command *head = c;

    // Previous link of temp
    command *previous = NULL;

    // Link used to iterate through the linked list
    command *temp = NULL;

    // create a redirect pointer to traverse redirect list
    redirect *red;

    // If the last command had a token
    bool prev_token = false;

    // while there are commands left to be parsed
    while ((s = parse_shell_token(s, &type, &token)) != NULL)
    {
        // if the previous token was last in the command
        if (prev_token)
        {
            // alloc a new command struct
            c->next = command_alloc();

            // set prev to previous
            c->prev = previous;

            // incement previous
            previous = c;

            // increment c
            c = c->next;

            // append the token to incremented command struct
            command_append_arg(c, token);

            // no longer the last token in command
            prev_token = false;
        }

        // if token is of type TOKEN_REDIRECTION
        else if (type == TOKEN_REDIRECTION)
        {
            // Setup redirect and alloc mem
            red = alloc_redirect();

            // set the token in that struct
            red->token = token;

            // get the file, incrementing s
            s = parse_shell_token(s, &type, &token);

            // set the file in the redirect struct
            red->file = token;

            // insert at the head of the linked list
            red->next = c->redirection;
            c->redirection = red;

            // if last token, break out of loop
            if (s == NULL)
            {
                break;
            }
        }

        // if token is of some other specified type
        else if (type == TOKEN_SEQUENCE || type == TOKEN_BACKGROUND || type == TOKEN_AND || type == TOKEN_OR ||
                 type == TOKEN_PIPE)
        {

            // this is the last token in this command
            prev_token = true;

            // set the condition_type field
            c->condition_type = type;

            // if type is of token background
            if (type == TOKEN_BACKGROUND)
            {
                // Set background process to true
                c->background_proc = true;

                // set all previous commands liked by || or && to background
                temp = previous;
                while (temp != NULL)
                {
                    if (temp->condition_type == TOKEN_SEQUENCE || temp->condition_type == TOKEN_BACKGROUND)
                    {
                        break;
                    }
                    temp->background_proc = true;
                    temp = temp->prev;
                }
            }
        }

        // otherwise just append the token
        else
        {
            command_append_arg(c, token);
        }
    }

    // execute it
    if (head->argc)
    {
        run_list(head);
    }
    command_free(head);
}

int main(int argc, char *argv[])
{
    FILE *command_file = stdin;
    int quiet = 0;

    // Check for '-q' option: be quiet (print no prompts)
    if (argc > 1 && strcmp(argv[1], "-q") == 0)
    {
        quiet = 1;
        --argc, ++argv;
    }

    // Check for filename option: read commands from file
    if (argc > 1)
    {
        command_file = fopen(argv[1], "rb");
        if (!command_file)
        {
            perror(argv[1]);
            _exit(1);
        }
    }

    // - Put the shell into the foreground
    // - Ignore the SIGTTOU signal, which is sent when the shell is put back
    //   into the foreground
    claim_foreground(0);
    set_signal_handler(SIGTTOU, SIG_IGN);

    char buf[BUFSIZ];
    int bufpos = 0;
    int needprompt = 1;

    while (!feof(command_file))
    {
        // Print the prompt at the beginning of the line
        if (needprompt && !quiet)
        {
            printf("sh61[%d]$ ", getpid());
            fflush(stdout);
            needprompt = 0;
        }

        // Read a string, checking for error or EOF
        if (fgets(&buf[bufpos], BUFSIZ - bufpos, command_file) == NULL)
        {
            if (ferror(command_file) && errno == EINTR)
            {
                // ignore EINTR errors
                clearerr(command_file);
                buf[bufpos] = 0;
            }
            else
            {
                if (ferror(command_file))
                {
                    perror("sh61");
                }
                break;
            }
        }

        // If a complete command line has been provided, run it
        bufpos = strlen(buf);
        if (bufpos == BUFSIZ - 1 || (bufpos > 0 && buf[bufpos - 1] == '\n'))
        {
            eval_line(buf);
            bufpos = 0;
            needprompt = 1;
        }

        // Handle zombie processes and/or interrupt requests
        // Your code here!
    }

    return 0;
}
