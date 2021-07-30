#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define ERROR         (-1)
#define SUCCESS       (0)

#define NO_PID        (-1)
#define CHILD_PROC    (0)
#define CHILD_RUNNING (0)
#define NOT_FOUND     (-1)

#define MAX_HISTORY   (100)
#define MAX_PATH      (100)
#define MAX_INPUT     (100)
#define MAX_ARGS      (100)
#define MAX_CHILDREN  (100)

typedef enum { FALSE = 0, TRUE = 1 } bool_t;

typedef struct {
	// raw command (no &)
	char input[MAX_INPUT + 1];
	// command (e.g. cd, ls, etc.)
	char name[MAX_INPUT + 1];
	// after strtok
	char *args[MAX_ARGS + 1];
	// -1 for builtin commands
	pid_t pid;
	// amount of arguments (including command name)
	size_t args_len;
	// foreground
	bool_t fg;
	// finished
	bool_t done;
} Command;

typedef struct {
	// main flag loop
	bool_t running;
	// used for 'history' and 'jobs'
	// last element is the current command
	Command history[MAX_HISTORY];
	size_t history_size;
} ProgramData;

bool_t getInput(char *input);
void printPrompt(void);
void handleError(char *msg);

// general string parsing
void trimSpaces(char *input);
char *skipSpace(char *input);
bool_t isEmpty(char *input);
int lastIndexOf(char *input, char ch);

// command parsing 
int parseCommand(ProgramData *data, char *input);
int fillArgs(char *input, char *args[MAX_ARGS + 1]);
int getCommandIndex(Command *cmd);
void stripAmpersand(char *dest, char *input);

// running and tracking used commands
void printJobs(ProgramData *data);
void printHistory(ProgramData *data);
void runNonBuiltin(ProgramData *data);
void changeDirectory(ProgramData *data);
void exitProgram(ProgramData *data);
void runCommand(ProgramData *data);
bool_t hasFinished(Command *cmd);

int main(void) {
	ProgramData data = { 0 };
	data.running = TRUE;
	char input[MAX_INPUT + 1] = { 0 };
	
	while (data.running) {
		printPrompt();
		if (!getInput(input)) {
			// no input read
			continue;
		}

		if (parseCommand(&data, input) == ERROR) {
			// shouldn't get here
			// because all inputs have even amount of quotation marks
			handleError("An error occurred");
		}

		runCommand(&data);

		Command *cmd = &data.history[data.history_size - 1];
		if (cmd->fg) {
			// We've already finished waiting for a foreground process
			cmd->done = TRUE;
		}
	}
	return 0;
}

void stripAmpersand(char *dest, char *input) {
	int index = lastIndexOf(input, '&');
	// if there's no & then we want to copy the whole string
	if (index == NOT_FOUND) {
		index = strlen(input) - 1;
	} else {
		// otherwise, we're going to copy a substring of input without &
		// and without preceding spaces if there are any
		do {
			index--;
		} while (index >= 0 && isspace(input[index]));
	}
	size_t len = index + 1;
	// we copy exactly len characters 
	// therefore we must manually add null-terminating byte
	strncpy(dest, input, len);
	dest[len] = '\0';

}

char *skipSpace(char *input) {
	// increment input as long as we're reading spaces
	while (*input && isspace(*input)) {
		input++;
	}
	return input;
}

int fillArgs(char *input, char *args[MAX_ARGS + 1]) {
	// use a copy of input
	// because we're going to overwrite 'input'
	char inputCopy[MAX_INPUT + 1];
	strcpy(inputCopy, input);

	// amount of args read so far
	int len = 0;
	bool_t gotQuotes = FALSE;

	// we'll read the input from 'inputCopy'
	char *read = skipSpace(inputCopy);
	// the arguments in args will point to addresses inside 'input'
	// therefore we'll have to overwrite input
	// and add '\0' after every argument
	char *write = input;
	args[len++] = write;
	
	while (*read != '\0') {
		// we want to know if we're inside quotes
		if (*read == '\"') {
			gotQuotes = !gotQuotes;
			read++;
			if (!gotQuotes && *read == '\0') {
				*write++ = '\0';
				// set last arg to NULL (like argv)
				args[len] = NULL;
			}
		} else if (isspace(*read)) {
			// skip spaces only if we're not inside quotes
			if (gotQuotes) {
				*write++ = *read++;
			} else {
				*write++ = '\0';
				read = skipSpace(read);
				args[len++] = write;
			}
		} else {
			// write regular non-space characters
			*write++ = *read++;
			// reached end of input - write the last argument
			if (*read == '\0') {
				*write++ = '\0';
				// set last arg to NULL (like argv)
				args[len] = NULL;
			}
		}
	}

	// unclosed quotes
	if (gotQuotes) {
		return ERROR;
	}

	return len;
}

int parseCommand(ProgramData *data, char *input) {
	// append the command to the history
	Command *cmd = &data->history[data->history_size++];
	
	// copy the command (without & if present)
	stripAmpersand(cmd->input, input);
	
	// fill cmd->args with the actual arguments in input
	int len = fillArgs(input, cmd->args);
	if (len == ERROR) {
		return ERROR;
	}

	// if & is the last argument - set fg to FALSE
	// and overwrite & with NULL
	cmd->fg = (strcmp(cmd->args[len - 1], "&") != 0);
	if (!cmd->fg) {
		cmd->args[len - 1] = NULL;
		len--;
	}
	// get command name (first argument)
	strcpy(cmd->name, cmd->args[0]);
	cmd->args_len = len;
	cmd->pid = NO_PID;
	cmd->done = FALSE;

	return SUCCESS;
}

int lastIndexOf(char *input, char ch) {
	// start at the last character of the string
	int i = strlen(input) - 1;
	while (i >= 0) {
		// if we found ch - return its index
		if (input[i] == ch) {
			return i;
		}
		i--;
	}
	return NOT_FOUND;
}

void trimSpaces(char *input) {
	if (input) {
		// skip spaces at the beginning of the string
		int start = 0;
		while (isspace(input[start])) {
			start++;
		}
		// skip spaces at the end of the string
		int end = strlen(input) - 1;
		while (isspace(input[end])) {
			end--;
		}
		int len;
		// string doesn't contain non-space characters
		if (end < start) {
			len = 0;
		} else {
			len = end - start + 1;
			memmove(input, &input[start], len);
		}
		// set input to the resulting substring (trimmed spaces)
		input[len] = '\0';
	}
}

bool_t isEmpty(char *input) {
	return input == NULL || strlen(input) == 0;
}

bool_t getInput(char *input) {
	// fetch user input from stdin
	char *ret = fgets(input, MAX_INPUT, stdin);
	trimSpaces(ret);
	return !isEmpty(ret);
}

void handleError(char *msg) {
	printf("%s\n", msg);
}

void printPrompt(void) {
	printf("$ ");
	fflush(stdout);
}

bool_t hasFinished(Command *cmd) {
	// we already marked the command done
	if (cmd->done) {
		return TRUE;
	}
	// foreground command that's not marked done
	if (cmd->fg) {
		return FALSE;
	}

	// check if the process is done
	pid_t ret = waitpid(cmd->pid, NULL, WNOHANG);
	if(ret == ERROR) {
		// handleError("An error occurred");
	}
	// process is done if waitpid returned anything that is not CHILD_RUNNING
	cmd->done = (ret != CHILD_RUNNING);
	return cmd->done;
}

int getCommandIndex(Command *cmd) {
	// static array of builtin command keywords
	static char *builtin[] = {"jobs", "history", "cd", "exit", NULL};
	int i;
	// search cmd->name in builtin
	for (i = 0; builtin[i] != NULL; i++) {
		if (strcmp(builtin[i], cmd->name) == 0) {
			// simply return the index of the command
			return i;
		}
	}
	return NOT_FOUND;
}

void runNonBuiltin(ProgramData *data) {
	Command *cmd = &data->history[data->history_size - 1];
	pid_t pid = fork();
	if (pid == ERROR) {
		handleError("fork failed");
	// is child process
	} else if (pid == CHILD_PROC) {
		
		// call the appropriate binary file with execvp
		if (execvp(cmd->name, cmd->args) == ERROR) {
			handleError("exec failed");
			exit(ERROR);
		}
	// is parent process
	} else {
		cmd->pid = pid;
		// is foreground
		if (cmd->fg) {
			// wait for child process to finish
			if (waitpid(pid, NULL, 0) == ERROR) {
				// handleError("An error occurred");
			}
		}
	}
}

void printHistory(ProgramData *data) {
	int i;
	for (i = 0; i < data->history_size; i++) {
		Command *cmd = &data->history[i];
		char *status = hasFinished(cmd) ? "DONE" : "RUNNING";
		printf("%s %s\n", cmd->input, status);
	}
}

void exitProgram(ProgramData *data) {
	// kill all children processes
	int i;
	for (i = 0; i < data->history_size; i++) {
		Command *cmd = &data->history[i];
		// command is not foreground and not finished 
		if (!cmd->fg && !hasFinished(cmd)) {
			// finish the process
			kill(cmd->pid, SIGKILL);
		}
	}
	data->running = FALSE;
}

void printJobs(ProgramData *data) {
	int i;
	for (i = 0; i < data->history_size; i++) {
		Command *cmd = &data->history[i];
		// print all background commands which aren't yet finished
		if (!cmd->fg && !hasFinished(cmd)) {
			printf("%s\n", cmd->input);
		}
	}
}

void runCommand(ProgramData *data) {	
	Command *cmd = &data->history[data->history_size - 1];
	switch(getCommandIndex(cmd)) {
	case 0:
		printJobs(data);
		break;
	case 1:
		printHistory(data);
		break;
	case 2:
		changeDirectory(data);
		break;
	case 3:
		exitProgram(data);
		break;
	default:
		runNonBuiltin(data);
		break;
	}
}

void handleTilde(char *input) {
	// copy home path into buf
	char *homePath = getenv("HOME");
	char buf[MAX_PATH + 1];
	strcpy(buf, homePath);
	// copy the rest of the path into buf
	// and skip the first char (tilde)
	strcpy(buf + strlen(buf), input + 1);
	// copy it all back into input
	strcpy(input, buf);
}

void changeDirectory(ProgramData *data) {
	static char lastPath[MAX_PATH + 1] = { 0 }; 
	Command *cmd = &data->history[data->history_size - 1];
	// wrong number of arguments (first arg is always 'cd')
	if (cmd->args_len > 2) {
		handleError("Too many arguments");
		return;
	} else if (cmd->args_len == 1) {
		// set to default directory - HOME
		cmd->args[1] = "~";
	}

	char currPath[MAX_PATH + 1];
	if (getcwd(currPath, MAX_PATH + 1) == NULL) {
		handleError("An error occurred");
	}
	char newPath[MAX_PATH + 1];
	strcpy(newPath, cmd->args[1]);
	// swap tilde with home path
	if (newPath[0] == '~') {
		handleTilde(newPath);
	// move to the previous path
	} else if (strcmp(newPath, "-") == 0) {
		// if there is no previous directory to go to
		if (isEmpty(lastPath)) {
			strcpy(newPath, currPath);
		} else {
			strcpy(newPath, lastPath);
		}
	}
	
	// update lastPath
	strcpy(lastPath, currPath);

	// change directory to newPath
	if (chdir(newPath) == ERROR) {
		handleError("chdir failed");
	}
}