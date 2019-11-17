#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <linux/limits.h>
#include <errno.h>
//#include "list.h"

// Returns full path name to executable with name matching command found in PATH
// Returns NULL if command not found in PATH
// Returned string should be freed
char *locateExecutable(char *command) {
    char *pathRef = getenv("PATH");
    char *pathCopy = strdup(pathRef);
    if (pathCopy == NULL) {
        perror("strdup");
        return NULL;
    }
    char *pathLocations = strtok(pathCopy, ":");
    char *temp = malloc(sizeof(char)*PATH_MAX);
    if (temp == NULL) {
        perror("malloc");
        return NULL;
    }

    while (pathLocations != NULL) {
        sprintf(temp, "%s/%s", pathLocations, command);

        if (access(temp, X_OK) == 0) {
            free(pathCopy);
            return temp;
        }

        pathLocations = strtok(NULL, ":");
    }

    free(pathCopy);
    free(temp);
    return NULL;
}

// Prints prompt 'osh'
// Gets input from line and removes spaces character from the end of it.
// Returned string should be freed
char *getInput() {
    size_t inputSize= ARG_MAX;
    char *input = malloc(sizeof(char)*inputSize);
    if (input == NULL) {
        perror("malloc");
    }

    printf("osh>");

    if (getline(&input, &inputSize, stdin) == -1) {
        perror("getline");
        free(input);
        return NULL;
    }
    // Remove space from end of input string
    input[strcspn(input, "\n")] = '\0';

    return input;
}

// Parses input string into array of words
char **parseInput(char *input) {
    if (input == NULL || strlen(input) == 0) {
        return NULL;
    }

    char *inputCopy = strdup(input);
    if (inputCopy == NULL) {
        perror("strdup");
        return NULL;
    }
    char *words = strtok(inputCopy, " ");
    char **argv = NULL;
    int i = 0;

    while (words != NULL) {
        argv = realloc(argv, (i+2)*sizeof(char*));
        if (argv == NULL) {
            perror("realloc");
            return NULL;
        }
        argv[i] = strdup(words);
        if (argv[i] == NULL) {
            perror("strdup");
            return NULL;
        }

        i++;
        words = strtok(NULL, " ");
    }
    argv[i] = NULL;

    free(inputCopy);
    return argv;
}

// Parses array of words into array of pipes
char ***parsePipes(char *argv[]) {
    if (argv == NULL) {
        return NULL;
    }

    char ***pipes = malloc(2*sizeof(char**));       // char *pipes[][];
    pipes[0] = NULL;
    pipes[1] = NULL;
    int i = 0;
    int j = 0;
    int k = 0;

    for (i = 0;  argv[i] != NULL; i++) {
        if (strcmp(argv[i], "|") == 0) {
            j++;
            pipes = realloc(pipes, (j+2)*sizeof(char**));
            if (pipes == NULL) {
                perror("realloc");
                return NULL;
            }
            pipes[j] = NULL;
            k = 0;
        } else {
            pipes[j] = realloc(pipes[j], (k+2)*sizeof(char*));
            if (pipes[j] == NULL) {
                perror("realloc");
                return NULL;
            }
            pipes[j][k] = argv[i];
            k++;
            pipes[j][k] = NULL;
        }
    }
    pipes[j+1] = NULL;

    return pipes;
}

// Inverts array of pipes
void invertArr(char ***arr) {
    int ptr0 = 0;
    int ptr1 = 0;
    char **temp;

    while (arr[ptr1+1] != NULL) {
        ptr1++;
    }

    while (ptr0 < ptr1) {
        temp = arr[ptr0];
        arr[ptr0] = arr[ptr1];
        arr[ptr1] = temp;
        ptr0++;
        ptr1--;
    }
}

// Return:
// -3 = dup2 error
// -2 = fork error
// -1 = execvp error
//  0 = parent returns
int spawn_proc(int in, int out, char *argv[]) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return -2;
    } else if (pid == 0) {
        if (dup2(in, STDIN_FILENO) == -1) {
            perror("dup2");
            return -3;
        }
        if (dup2(out, STDOUT_FILENO) == -1) {
            perror("dup2");
            return -3;
        }
        if (execvp(argv[0], argv) == -1) {
            perror("execvp");
            printf(-1); // Hacky and generates warnings. To fix.
            return -1;
        }
    }
    return 0;
}

// Print to STDOUT from fd
// Duplicate characters 'c', 'm', 'p' and 't'
void printSTDOUT(int fd) {
    char buf;
    while (read(fd, &buf, sizeof(char)) != 0) {
        printf("%c", buf);
        if (buf == 'c' || buf == 'm' || buf == 'p' || buf == 't') {
            printf("%c", buf);
        }
    }
}

// Determine presence of output overwrite
// Returns filename to write to
// Removes filename and '<' symbol from input
char *locateOutputOverwrite(char **list) {
    char *ans = NULL;
    int i;

    if (list == NULL || list[1] == NULL) {
        return NULL;
    }

    if (strcmp(list[1], "<") == 0) {
        ans = list[0];
        list[0] = list[2];
        free(list[1]);
        list[1] = list[3];
        for (i = 2; list[i+1] != NULL; i++) {
            list[i] = list[i+2];
        }
        list[i] = NULL;
    }
    /*
    for (i = 0; list[i] != NULL; i++) {
        if (strcmp(list[i], ">") == 0) {
            ans = strdup(list[i+1]);
            break;
        }
    }
    if (ans != NULL) {
        while (list[i+2] != NULL) {
            free(list[i]);
            list[i] = list[i+2];
            i++;
        }
        free(list[i]);
        list[i] = NULL;
    }
    */

    return ans;
}

// Execute the parsed input
int runExec(char **argv[]) {
    int wstatus;
    int i = 0;
    int pipefd[2][2];
    int in, out;
    char *redirectionFileName;
    FILE *redirectionFile;

    if (argv == NULL) {
        return 1;
    }

    while (argv[i] != NULL) {
        if (argv[i][0] != NULL) {
            if (pipe(pipefd[i%2]) == -1) {
                perror("pipe");
                exit(2);
            }
            if (i == 0) {
                // First command reads from STDIN
                in = STDIN_FILENO;
            } else {
                // Rest of commands read from previous pipe
                in = pipefd[(i-1)%2][0];
            }
            // Determine if output is being redirected to a file
            redirectionFileName = locateOutputOverwrite(argv[i]);
            if (redirectionFileName != NULL) {
                // Print output to file
                redirectionFile = fopen(redirectionFileName, "w");
                if (redirectionFile == NULL) {
                    perror("fopen");   
                    out = pipefd[i%2][1];
                } else {
                    out = fileno(redirectionFile);
                    if (out == -1) {
                        perror("fileno");
                        out = pipefd[i%2][1];
                    } else {
                        if (close(pipefd[i%2][1]) == -1) {
                            perror("close");
                        }
                    }
                }
            } else {
                // Print output to next pipe
                out = pipefd[i%2][1];
            }
            // Execute command
            spawn_proc(in, out, argv[i]);
            if (i > 0) {
                if (close(pipefd[(i-1)%2][0]) == -1) {
                    perror("close");
                }
            }
            if (redirectionFileName != NULL) {
                if (fclose(redirectionFile) != 0) {
                    perror("fclose");
                }
            } else {
                if (close(pipefd[i%2][1]) == -1) {
                    perror("close");
                }
            }
        }
        i++;
    }
    printSTDOUT(pipefd[(i+1)%2][0]);
    if (close(pipefd[(i+1)%2][0]) == -1) {
        perror("close");
    }

    // Wait for spawned chilled processes to return
    while (i != 0) {
        if (wait(&wstatus) == -1) {
            perror("wait");
        }
        i--;
    }

    return 0;
}

void printArr(char ***arr) {
    for (int i = 0; arr[i] != NULL; i++) {
        for (int j = 0; arr[i][j] != NULL; j++) {
            printf("arr[%d][%d]: %s\n", i, j, arr[i][j]);
        }
    }
}

void shellLoop() {
    char *input = getInput();
    if (input == NULL) {
        return;
    }
    
    char **argv = parseInput(input);
    if (argv == NULL) {
        return;
    }

    free(input);

    char ***pipes = parsePipes(argv);
    if (pipes == NULL) {
        return;
    }

    invertArr(pipes);

    runExec(pipes);

    for (int i = 0; pipes[i] != NULL; i++) {
        for (int j = 0; pipes[i][j] != NULL; j++) {
            free(pipes[i][j]);
        }
        free(pipes[i]);
    }
    free(pipes);
}

int main(int argc, char *argv[]) {

    while (1) {
        shellLoop();
    }

    return EXIT_SUCCESS;
}
