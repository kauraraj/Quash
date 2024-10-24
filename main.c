#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>

#define MAX_INPUT 10024
#define MAX_ARGS 10000

 

typedef struct {
    int job_id;
    pid_t pid;
    char command[256];
} Job;

Job jobs[100];  // Store background jobs
int num_jobs = 0;  // Track the number of jobs
int next_job_id = 1;  // Track the next available job ID

// Function declarations
char *expand_env_var_in_string(char *arg);
char *expand_env_var_in_string_2(char *arg);

void tokenize_command(char *command, char **args);
void execute_single_command(char **args);
void execute_piped_commands(char *input);
void execute_multiple_pipes(char *input);
int count_pipes(const char *input);
void execute_command_with_redirection(char **args, char *output_file);
void execute_command_with_input_redirection(char **args, char *input_file);
void execute_command_with_append_redirection(char **args, char *output_file);
void execute_command_with_input_output_redirection(char **args, char *input_file, char *output_file);
void execute_command_with_input_output_append_redirection(char **args, char *input_file, char *output_file);
void execute_command(char **args);
void run_shell();
void quash_pwd();
void quash_echo(char **args);
void quash_export(char **args);
void quash_cd(char **args);
void quash_jobs();
void add_job(pid_t pid, char **args);
void remove_job(pid_t pid);
void quash_kill(char **args);
void check_background_jobs();
void sigchld_handler(int signum);




extern Job jobs[];  // Declare the jobs array
extern int num_jobs; // Declare the number of jobs

// Main function
int main() {
    // Set up the signal handler for SIGCHLD
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // Restart interrupted system calls
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Start the shell
    printf("Welcome to Quash Shell!\n");
    run_shell();
    return 0;
}

char *expand_env_var_in_string(char *arg) {
    char *result = malloc(strlen(arg) * 2 + 1);  // Allocate more than needed for safety
    if (result == NULL) {
        perror("malloc failed");
        return NULL;
    }

    result[0] = '\0';  // Initialize the result string

    char *ptr = arg;  // Pointer to traverse the input string
    while (*ptr != '\0') {
        if (*ptr == '$') {
            ptr++;  // Skip the '$'
            char env_var_name[100];
            int i = 0;

            // Extract the environment variable name (letters, digits, and underscores)
            while (isalnum(*ptr) || *ptr == '_') {
                if (i >= sizeof(env_var_name) - 1) break;  // Ensure no overflow
                env_var_name[i++] = *ptr++;
            }
            env_var_name[i] = '\0';  // Null-terminate the environment variable name

            // Get the value of the environment variable
            char *env_value = getenv(env_var_name);
            if (env_value != NULL) {
                strcat(result, env_value);  // Append the environment variable's value to the result
            } else {
                strcat(result, "$");        // Append the '$' back if the variable doesn't exist
                strcat(result, env_var_name);
            }
        } else {
            // Append characters that are not part of environment variables
            strncat(result, ptr, 1);
            ptr++;
        }
    }

    return result;
}


char *expand_env_var_in_string_2(char *arg) {
    char *result = malloc(strlen(arg) + 1);
    char *dollar_sign = strchr(arg, '$');  // Find the '$' symbol in the string

    if (dollar_sign != NULL) {
        // Extract the environment variable name (starts after '$')
        char env_var_name[100];
        int i = 0;
        dollar_sign++;  // Move past the '$'

        // Extract the environment variable name (letters, digits, and underscores)
        while (dollar_sign[i] != '/' && dollar_sign[i] != '\0' && i < sizeof(env_var_name) - 1) {
            env_var_name[i] = dollar_sign[i];
            i++;
        }
        env_var_name[i] = '\0';  // Null-terminate the environment variable name

        // Get the value of the environment variable
        char *env_value = getenv(env_var_name);
        if (env_value != NULL) {
            // Copy the part before the '$'
            strncpy(result, arg, dollar_sign - arg - 1);
            result[dollar_sign - arg - 1] = '\0';  // Null-terminate the result so far
            // Concatenate the expanded environment variable value
            strcat(result, env_value);
            // Concatenate the rest of the string after the environment variable
            strcat(result, dollar_sign + strlen(env_var_name));
        } else {
            fprintf(stderr, "quash: %s: environment variable not set\n", env_var_name);
            free(result);
            return NULL;
        }
    } else {
        // If there's no '$', return the original string
        strcpy(result, arg);
    }

    return result;
}


// Tokenize command correctly with environment variable expansion
void tokenize_command(char *command, char **args) {
    int i = 0;
    char *token;

    // Tokenize based on spaces, but handle quotes and environment variables properly
    while ((token = strtok(command, " ")) != NULL) {
        command = NULL;  // For subsequent calls, pass NULL

        // Expand environment variables if any
        char *expanded_value = expand_env_var_in_string(token);
        if (expanded_value != NULL) {
            args[i] = strdup(expanded_value);  // Use the expanded value
            free(expanded_value);  // Free the memory after use
        } else {
            args[i] = strdup(token);  // If no expansion, use the original token
        }

        i++;
    }
    args[i] = NULL;  // Null-terminate the arguments array
}




// Function to handle pipes in commands
void execute_piped_commands(char *input) {
    char *commands[MAX_ARGS];  // Array to store individual commands
    int num_pipes = 0;

    // Split the input into individual commands based on the pipe '|'
    char *command = strtok(input, "|");
    while (command != NULL && num_pipes < MAX_ARGS - 1) {
        commands[num_pipes++] = command;
        command = strtok(NULL, "|");
    }
    commands[num_pipes] = NULL;  // Null-terminate the commands array

    // Array to store file descriptors for pipes
    int pipefds[2 * (num_pipes - 1)];

    // Create the required number of pipes
    for (int i = 0; i < num_pipes - 1; i++) {
        if (pipe(pipefds + 2 * i) == -1) {
            perror("pipe failed");
            return;
        }
    }

    for (int i = 0; i < num_pipes; i++) {
        char *cmd_args[MAX_ARGS];
        tokenize_command(commands[i], cmd_args);

        pid_t pid = fork();
        if (pid == 0) {
            // Child process

            // If not the first command, get input from the previous pipe
            if (i != 0) {
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) == -1) {
                    perror("dup2 input failed");
                    exit(EXIT_FAILURE);
                }
            }

            // If not the last command, write output to the next pipe
            if (i != num_pipes - 1) {
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 output failed");
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe file descriptors in the child process
            for (int j = 0; j < 2 * (num_pipes - 1); j++) {
                close(pipefds[j]);
            }

            // Execute the command
            execvp(cmd_args[0], cmd_args);
            perror("quash: command execution failed");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            // Fork failed
            perror("fork failed");
            return;
        }
    }

    // Parent process: Close all pipes
    for (int i = 0; i < 2 * (num_pipes - 1); i++) {
        close(pipefds[i]);
    }

    // Wait for all child processes to finish
    for (int i = 0; i < num_pipes; i++) {
        wait(NULL);
    }
}

// Function to handle multiple pipes in commands
void execute_multiple_pipes(char *input) {
    char *commands[MAX_ARGS];  // Array to store individual commands
    int num_pipes = 0;

    // Split the input into individual commands based on the pipe '|'
    char *command = strtok(input, "|");
    while (command != NULL && num_pipes < MAX_ARGS - 1) {
        commands[num_pipes++] = command;
        command = strtok(NULL, "|");
    }
    commands[num_pipes] = NULL;  // Null-terminate the commands array

    // Array to store file descriptors for pipes
    int pipefds[2 * (num_pipes - 1)];

    // Create the required number of pipes
    for (int i = 0; i < num_pipes - 1; i++) {
        if (pipe(pipefds + 2 * i) == -1) {
            perror("pipe failed");
            return;
        }
    }

    for (int i = 0; i < num_pipes; i++) {
        char *cmd_args[MAX_ARGS];
        tokenize_command(commands[i], cmd_args);

        pid_t pid = fork();
        if (pid == 0) {
            // Child process

            // If not the first command, get input from the previous pipe
            if (i != 0) {
                if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) == -1) {
                    perror("dup2 input failed");
                    exit(EXIT_FAILURE);
                }
            }

            // If not the last command, write output to the next pipe
            if (i != num_pipes - 1) {
                if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) == -1) {
                    perror("dup2 output failed");
                    exit(EXIT_FAILURE);
                }
            }

            // Close all pipe file descriptors in the child process
            for (int j = 0; j < 2 * (num_pipes - 1); j++) {
                close(pipefds[j]);
            }

            // Execute the command
            execvp(cmd_args[0], cmd_args);
            perror("quash: command execution failed");
            exit(EXIT_FAILURE);
        } else if (pid < 0) {
            // Fork failed
            perror("fork failed");
            return;
        }
    }

    // Parent process: Close all pipes
    for (int i = 0; i < 2 * (num_pipes - 1); i++) {
        close(pipefds[i]);
    }

    // Wait for all child processes to finish
    for (int i = 0; i < num_pipes; i++) {
        wait(NULL);
    }
}



int count_pipes(const char *input) {
    int count = 0;
    while (*input != '\0') {
        if (*input == '|') {
            count++;
        }
        input++;
    }
    return count;
}


void execute_command_with_redirection(char **args, char *output_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Open the file for output
        int fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd == -1) {
            perror("quash: open failed");
            exit(EXIT_FAILURE);
        }

        // Redirect stdout to the file
        dup2(fd, STDOUT_FILENO);
        close(fd);

        // Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

// Function to handle input redirection
void execute_command_with_input_redirection(char **args, char *input_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Open the input file
        int fd = open(input_file, O_RDONLY);
        if (fd == -1) {
            perror("quash: open failed");
            exit(EXIT_FAILURE);
        }

        // Redirect stdin to the file
        dup2(fd, STDIN_FILENO);
        close(fd);

        // Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

void execute_command_with_append_redirection(char **args, char *output_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Open the file for appending
        int fd = open(output_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd == -1) {
            perror("quash: open failed");
            exit(EXIT_FAILURE);
        }

        // Redirect stdout to the file
        dup2(fd, STDOUT_FILENO);
        close(fd);

        // Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

void execute_command_with_input_output_redirection(char **args, char *input_file, char *output_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Open the input file
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in == -1) {
            perror("quash: open input failed");
            exit(EXIT_FAILURE);
        }

        // Open the output file for truncating
        int fd_out = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd_out == -1) {
            perror("quash: open output failed");
            exit(EXIT_FAILURE);
        }

        // Redirect stdin to input file
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);

        // Redirect stdout to output file
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);

        // Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}

void execute_command_with_input_output_append_redirection(char **args, char *input_file, char *output_file) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // Open the input file
        int fd_in = open(input_file, O_RDONLY);
        if (fd_in == -1) {
            perror("quash: open input failed");
            exit(EXIT_FAILURE);
        }

        // Open the output file for appending
        int fd_out = open(output_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd_out == -1) {
            perror("quash: open output failed");
            exit(EXIT_FAILURE);
        }

        // Redirect stdin to input file
        dup2(fd_in, STDIN_FILENO);
        close(fd_in);

        // Redirect stdout to output file
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);

        // Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for the child to finish
        waitpid(pid, NULL, 0);
    }
}





void execute_command(char **args) {
    int background = 0;

    // Check if the command is a background job (if the last argument is '&')
    int i = 0;
    while (args[i] != NULL) i++;  // Find the last argument
    if (i > 0 && strcmp(args[i - 1], "&") == 0) {
        background = 1;
        args[i - 1] = NULL;  // Remove the '&' from the arguments
    }

    // Tokenize and handle pipes, redirection, etc., as usual

    // Normal or background command execution
    pid_t pid = fork();
    if (pid == 0) {
        // Child process: Execute the command
        execvp(args[0], args);
        perror("quash: command execution failed");
        exit(EXIT_FAILURE);
    } else {
        if (!background) {
            // Parent process: Wait for the child if not in background
            waitpid(pid, NULL, 0);
        } else {
            // Handle background jobs
            printf("Background job started: [%d] %d %s &\n", next_job_id, pid, args[0]);
            add_job(pid, args);  // Add the background job to the list
        }
    }
}



// Function to run the shell
void run_shell() {
    char input[MAX_INPUT];  // Store user input
    char *args[MAX_ARGS];   // Store parsed arguments
    int running = 1;        // Shell running status

    while (running) {
        // Print prompt
        printf("[QUASH]$ ");
        fflush(stdout);

        // Check for completed background jobs
        check_background_jobs();

        // Get input from user
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            // Handle Ctrl+D (EOF)
            printf("\n");
            break;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = '\0';

        // Check if the input is empty
        if (input[0] == '\0') {
            continue;
        }

        // Expand environment variables in the entire input
        char *expanded_input = expand_env_var_in_string(input);
        if (expanded_input == NULL) {
            continue;
        }

        // Process the expanded input
        int pipe_count = count_pipes(expanded_input);
        if (pipe_count == 1) {
            // Single pipe case
            execute_piped_commands(expanded_input);
            free(expanded_input);
            continue;
        } else if (pipe_count > 1) {
            // Multiple pipes case
            execute_multiple_pipes(expanded_input);
            free(expanded_input);
            continue;
        }

        // Handle input and output redirection as before
        
                // Handle input and output redirection
        char *redirect_in = strchr(input, '<');
        char *redirect_out = strchr(input, '>');
        if (redirect_in != NULL && redirect_out != NULL) {
            *redirect_in = '\0';
            *redirect_out = '\0';
            char *input_file = strtok(redirect_in + 1, " ");
            char *output_file = NULL;

            // Check if output redirection is append (>>) or overwrite (>)
            if (*(redirect_out + 1) == '>') {
                output_file = strtok(redirect_out + 2, " ");
                tokenize_command(input, args);
                execute_command_with_input_output_append_redirection(args, input_file, output_file);
            } else {
                output_file = strtok(redirect_out + 1, " ");
                tokenize_command(input, args);
                execute_command_with_input_output_redirection(args, input_file, output_file);
            }
            continue;
        }

        // Handle individual input or output redirection
        if (redirect_in != NULL) {
            *redirect_in = '\0';
            char *input_file = strtok(redirect_in + 1, " ");
            tokenize_command(input, args);
            execute_command_with_input_redirection(args, input_file);
            continue;
        }

        if (redirect_out != NULL) {
            if (*(redirect_out + 1) == '>') {
                *redirect_out = '\0';
                char *output_file = strtok(redirect_out + 2, " ");
                tokenize_command(input, args);
                execute_command_with_append_redirection(args, output_file);
            } else {
                *redirect_out = '\0';
                char *output_file = strtok(redirect_out + 1, " ");
                tokenize_command(input, args);
                execute_command_with_redirection(args, output_file);
            }
            continue;
        }


        // Tokenize the command into arguments
        tokenize_command(expanded_input, args);

        // Check if the input is a built-in command or needs execution
        if (args[0] == NULL) {
            free(expanded_input);
            continue;  // No command entered
        }

        // Handle built-in commands
        if (strcmp(args[0], "pwd") == 0) {
            quash_pwd();
        } else if (strcmp(args[0], "echo") == 0) {
            quash_echo(args);
        } else if (strcmp(args[0], "export") == 0) {
            quash_export(args);
        } else if (strcmp(args[0], "cd") == 0) {
            quash_cd(args);
        } else if (strcmp(args[0], "jobs") == 0) {
            quash_jobs();
        } else if (strcmp(args[0], "kill") == 0) {
            quash_kill(args);
        } else if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
            running = 0;  // Exit the shell
        } else {
            // Handle regular command execution (foreground or background)
            execute_command(args);
        }

        free(expanded_input);  // Clean up expanded input
    }
}








// Built-in command: pwd
void quash_pwd() {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("quash: getcwd failed");
    }
}


// Built-in command: echo
void quash_echo(char **args) {
    int inside_single_quotes = 0;
    int inside_double_quotes = 0;

    for (int i = 1; args[i] != NULL; i++) {
        // Check if the argument starts with a single quote
        if (args[i][0] == '\'' && args[i][strlen(args[i]) - 1] != '\'') {
            inside_single_quotes = 1;  // We are inside a single quoted string
            printf("%s", args[i] + 1);  // Print without the leading single quote
        } 
        // Check if the argument ends with a single quote
        else if (inside_single_quotes && args[i][strlen(args[i]) - 1] == '\'') {
            printf(" %.*s", (int)(strlen(args[i]) - 1), args[i]);  // Print without the trailing single quote
            inside_single_quotes = 0;  // We're done with the quoted string
        } 
        // Handle the middle of the single quoted string
        else if (inside_single_quotes) {
            printf(" %s", args[i]);  // Print with a space
        } 
        // Handle arguments with single quotes on both sides
        else if (args[i][0] == '\'' && args[i][strlen(args[i]) - 1] == '\'') {
            // Print the argument without surrounding single quotes
            printf("%.*s", (int)(strlen(args[i]) - 2), args[i] + 1);
        } 
        
        // Check if the argument starts with a double quote
        else if (args[i][0] == '\"' && args[i][strlen(args[i]) - 1] != '\"') {
            inside_double_quotes = 1;  // We are inside a double quoted string
            printf("%s", args[i] + 1);  // Print without the leading double quote
        } 
        // Check if the argument ends with a double quote
        else if (inside_double_quotes && args[i][strlen(args[i]) - 1] == '\"') {
            printf(" %.*s", (int)(strlen(args[i]) - 1), args[i]);  // Print without the trailing double quote
            inside_double_quotes = 0;  // We're done with the quoted string
        } 
        // Handle the middle of the double quoted string
        else if (inside_double_quotes) {
            printf(" %s", args[i]);  // Print with a space
        } 
        // Handle arguments with double quotes on both sides
        else if (args[i][0] == '\"' && args[i][strlen(args[i]) - 1] == '\"') {
            // Print the argument without surrounding double quotes
            printf("%.*s", (int)(strlen(args[i]) - 2), args[i] + 1);
        } 
        
        // Handle environment variables and regular arguments
        else {
            char *expanded_value = expand_env_var_in_string(args[i]);
            if (expanded_value != NULL) {
                printf("%s", expanded_value);
                free(expanded_value);  // Free dynamically allocated memory
            } else {
                printf("%s", args[i]);
            }
        }


        // Add a space between arguments if more arguments exist
        if (args[i + 1] != NULL && !(inside_single_quotes || inside_double_quotes)) {
            printf(" ");
        }
    }
    printf("\n");
}








// Built-in command: export
void quash_export(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "quash: export: missing argument\n");
        return;
    }

    // Split the argument at the '=' sign to get the variable name and value
    char *name = strtok(args[1], "=");
    char *value = strtok(NULL, "=");

    if (name == NULL || value == NULL) {
        fprintf(stderr, "quash: export: invalid syntax\n");
        return;
    }

    // Check if the value contains an environment variable (e.g., $HOME)
    if (value[0] == '$') {
        value = expand_env_var_in_string(value);  // Expand the variable if it starts with '$'
    }

    // Set the environment variable
    if (setenv(name, value, 1) != 0) {
        perror("quash: export: setenv failed");
    }
}



// Function to handle built-in cd command
void quash_cd(char **args) {
    char *dir;

    if (args[1] == NULL) {
        // If no directory is specified, change to the HOME directory
        dir = getenv("HOME");
        if (dir == NULL) {
            fprintf(stderr, "quash: cd: HOME environment variable not set\n");
            return;
        }
    } else {
        // Expand environment variable if necessary
        dir = expand_env_var_in_string(args[1]);
        if (dir == NULL) {
            return;  // If expansion fails, do nothing
        }
    }

    // Attempt to change the directory
    if (chdir(dir) != 0) {
        perror("quash: cd");
        free(dir);  // Free the allocated memory
        return;
    }

    // Get the new current directory and update PWD
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        setenv("PWD", cwd, 1);  // Update the PWD environment variable
        printf("%s\n", cwd);    // Print the new current directory
    } else {
        perror("quash: cd: getcwd failed");
    }

    if (dir != getenv("HOME")) {
        free(dir);  // Free dynamically allocated memory if it was not the home directory
    }
}


// Function to add a background job
void add_job(pid_t pid, char **args) {
    if (num_jobs < 100) {
        jobs[num_jobs].job_id = next_job_id++;
        jobs[num_jobs].pid = pid;

        // Build the full command string from the args array
        char command[256] = "";
        for (int i = 0; args[i] != NULL; i++) {
            strcat(command, args[i]);
            if (args[i + 1] != NULL) strcat(command, " ");
        }
        strncpy(jobs[num_jobs].command, command, sizeof(jobs[num_jobs].command) - 1);
        jobs[num_jobs].command[sizeof(jobs[num_jobs].command) - 1] = '\0';  // Null-terminate the command

        num_jobs++;
    }
}

// Function to remove a job when it finishes
void remove_job(pid_t pid) {
    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].pid == pid) {
            // Remove the job by shifting the rest of the array
            for (int j = i; j < num_jobs - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            num_jobs--;
            break;
        }
    }
}

// Function to print all background jobs
void quash_jobs() {
    for (int i = 0; i < num_jobs; i++) {
        printf("[%d] %d %s &\n", jobs[i].job_id, jobs[i].pid, jobs[i].command);
    }
}

// Function to kill a background job or a process
void quash_kill(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "quash: kill: missing argument\n");
        return;
    }

    pid_t pid = 0;
    int job_id = 0;

    // Check if the user is trying to kill a job using %job_id
    if (args[1][0] == '%') {
        // Extract job ID (e.g., %1 becomes 1)
        job_id = atoi(&args[1][1]);

        // Find the corresponding job in the jobs array
        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].job_id == job_id) {
                pid = jobs[i].pid;
                break;
            }
        }

        if (pid == 0) {
            fprintf(stderr, "quash: kill: no such job [%d]\n", job_id);
            return;
        }
    } else {
        // Otherwise, treat it as a direct PID
        pid = atoi(args[1]);
    }

    // Attempt to kill the process or job
    if (kill(pid, SIGKILL) == -1) {
        perror("quash: kill");
    } else {
        printf("Killed process %d\n", pid);

        // Remove the job from the job list if it's a background job
        if (job_id > 0) {
            remove_job(pid);
        }
    }
}

// Function to check for completed background jobs and notify the user
void check_background_jobs() {
    int status;
    pid_t pid;

    for (int i = 0; i < num_jobs; ) {
        // Use waitpid() with WNOHANG to check if the job has completed
        pid = waitpid(jobs[i].pid, &status, WNOHANG);

        if (pid == 0) {
            // Job is still running, continue checking the next job
            i++;
        } else if (pid > 0) {
            // Job has completed, print a notification
            printf("\n[QUASH] Job [%d] %d (%s) completed\n", jobs[i].job_id, jobs[i].pid, jobs[i].command);

            // Remove the job from the job list
            remove_job(jobs[i].pid);
        } else {
            // Error or no more child processes, move to the next job
            i++;
        }
    }
}


void sigchld_handler(int signum) {
    int status;
    pid_t pid;

    // Check for all background jobs that have completed
    for (int i = 0; i < num_jobs; ) {
        pid = waitpid(jobs[i].pid, &status, WNOHANG);

        if (pid > 0) {
            // Job has completed
            printf("\n[QUASH] Job [%d] %d (%s) completed\n", jobs[i].job_id, jobs[i].pid, jobs[i].command);
            remove_job(jobs[i].pid);  // Remove the job from the list
        } else {
            // Move to the next job
            i++;
        }
    }
    fflush(stdout);  // Ensure output is displayed immediately
}
