#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // For `getcwd()` and other system calls
#include "command.h"

#define MAX_INPUT_SIZE 1024

// Function to parse and handle commands
void handle_command(char* input) {
    char* token = strtok(input, " ");
    if (token == NULL) return; // No command entered

    if (strcmp(token, "echo") == 0) {
        token = strtok(NULL, "\n"); // Get everything after "echo"
        if (token) {
            printf("%s\n", token);
        }
    } else if (strcmp(token, "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("getcwd() error");
        }
    } else if (strcmp(token, "cd") == 0) {
        token = strtok(NULL, " "); // Get directory argument
        if (token == NULL) {
            fprintf(stderr, "cd: expected argument\n");
        } else {
            if (chdir(token) != 0) {
                perror("cd");
            }
        }
    } else if (strcmp(token, "exit") == 0) {
        exit(0); // Exit the shell
    } else {
        printf("Unknown command: %s\n", token);
    }
}

int main() {
    char input[MAX_INPUT_SIZE];

    while (1) {
        printf("[QUASH]$ ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            // Remove trailing newline
            size_t len = strlen(input);
            if (len > 0 && input[len-1] == '\n') {
                input[len-1] = '\0';
            }

            handle_command(input); // Parse and execute the command
        }
    }

    return 0;
}
