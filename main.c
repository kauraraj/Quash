#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ctype.h>

#define MAX_INPUT 1024
#define MAX_ARGS 100


// Function declarations
char *expand_env_var_in_string(char *arg);
void execute_command(char **args);
void run_shell();
void quash_pwd();
void quash_echo(char **args);
void quash_export(char **args);
void quash_cd(char **args);

// Main function
int main() {
    printf("Welcome to Quash Shell!\n");
    run_shell();
    return 0;
}

// Function to expand environment variables within a string (for commands)
char *expand_env_var_in_string(char *arg) {
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


void execute_command(char **args) {
    pid_t pid = fork();  // Create a child process

    if (pid < 0) {
        // Fork failed
        perror("Fork failed");
    } else if (pid == 0) {
        // Child process: expand environment variables in arguments
        for (int i = 0; args[i] != NULL; i++) {
            // Expand each argument that contains an environment variable
            char *expanded_arg = expand_env_var_in_string(args[i]);
            if (expanded_arg != NULL) {
                args[i] = expanded_arg;  // Replace the original argument with the expanded one
            }
        }

        // Child process: execute the command
        if (execvp(args[0], args) == -1) {
            perror("quash: command execution failed");
        }

        // Free dynamically allocated memory for expanded arguments
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }

        exit(EXIT_FAILURE);
    } else {
        // Parent process: wait for the child to finish
        int status;
        waitpid(pid, &status, 0);
    }
}

// Function to handle the shell loop
void run_shell() {
    char input[MAX_INPUT];  // Store user input
    char *args[MAX_ARGS];   // Store parsed arguments
    int running = 1;        // Shell running status

    while (running) {
        // Print prompt
        printf("[QUASH]$ ");
        fflush(stdout);

        // Get input from user
        if (fgets(input, MAX_INPUT, stdin) == NULL) {
            // Handle Ctrl+D (EOF)
            printf("\n");
            break;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = '\0';

        // Parse the input into arguments (tokenize the string)
        int i = 0;
        char *token = strtok(input, " ");
        while (token != NULL && i < MAX_ARGS - 1) {  // Reserve space for NULL
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;  // Null-terminate the argument list

        // Check if the input is a built-in command
        if (args[0] == NULL) {
            // No command entered
            continue;
        } else if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) {
            // Exit the shell
            running = 0;
        } else if (strcmp(args[0], "pwd") == 0) {
            // Built-in command: pwd
            quash_pwd();
        } else if (strcmp(args[0], "echo") == 0) {
            // Built-in command: echo
            quash_echo(args);
        } else if (strcmp(args[0], "export") == 0) {
            // Built-in command: export
            quash_export(args);
        } else if (strcmp(args[0], "cd") == 0) {
            // Built-in command: cd
            quash_cd(args);
        } else {
            // If it's not a built-in command, run it as an external command
            execute_command(args);
        }
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