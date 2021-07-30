#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>

#define TRUE      (1)
#define FALSE     (0)

#define SUCCESS   (0)
#define ERROR     (-1)

#define MAX_PATH  (150)
#define MAX_BUF   (150)

#define FD_STDIN  (0)
#define FD_STDOUT (1)
#define FD_ERROR  (2)

#define FILE_MODE (0666)

enum {
	NO_C_FILE         = 0,
	COMPILATION_ERROR = 10,
	TIMEOUT           = 20,
	WRONG             = 50,
	SIMILAR           = 75,
	EXCELLENT         = 100
};

struct Data {
	// path for results.csv
	char resultsFilePath[MAX_PATH];
	// compare file path
	char compareFilePath[MAX_PATH];
	// input file for the students' programs
	char inputFilePath[MAX_PATH];
	// output file for comparison
	char outputComparisonPath[MAX_PATH];
	// path to the errorfile
	char errorFilePath[MAX_PATH];
	// main directory path
	char mainDirPath[MAX_PATH];
};

struct StudentData {
	// current (student's) directory
	char dirPath[MAX_PATH];
	// path to the student's binary
	char binFilePath[MAX_PATH];
	// path to the student's code file
	char codeFilePath[MAX_PATH];
	// path to the students' output file
	char outputFilePath[MAX_PATH];
};

void printError(const char *funcName);
void printCustomError(const char *msg);

const char *getReason(int grade);
const char *getGradeStr(int grade);
void writeToCSV(int fd, const char *name, int grade);
char getNextChar(int fd);
int getNextLine(int fd, char *buf);
int skipEntry(struct dirent *dirEntry);
void buildPath(char *buf, const char *dirPath, const char *entryName);

int isCodeFile(const char *fileName);
int getFileSize(int fd);
int isFileEmpty(int fd);

int initStudentTempFiles(struct StudentData *sData);
int initData(struct Data *data, int fd_config);
int destroyData(struct Data *data);

int compileCode(struct Data *data, struct StudentData *sData);
int runCode(struct Data *data, struct StudentData *sData);
int compareOutputs(struct Data *data, struct StudentData *sData);
int gradeStudent(struct Data *data, struct StudentData *sData);
int startGrading(struct Data *data);

int main(int argc, char *argv[]) {
	// return if there aren't enough arguments (i.e. missing config file path)
	if (argc < 2) { return ERROR; }

	const char *configFilePath = argv[1];
	int fd_config = open(configFilePath, O_RDONLY);
	if (fd_config == ERROR) { return ERROR; }

	int status = SUCCESS;
	struct Data data;
	// read config to initialize data
	// call 'startGrading' if the data initializing was successful
	if (initData(&data, fd_config) == ERROR ||
		startGrading(&data) == ERROR) { 
		status = ERROR; 
	}

	// clean resources
	if (close(fd_config) == ERROR) { status = ERROR; }

	return status;
}

// this function simply writes 'msg' to stderr and appends a newline
void printCustomError(const char *msg) {
	char buf[MAX_BUF] = { 0 };
	strcat(buf, msg);
	strcat(buf, "\n");
	if (write(FD_STDOUT, buf, strlen(buf)) == ERROR) {}
}

// this function prints the name of a function that caused an error
void printError(const char *funcName) {
	char buf[MAX_BUF] = { 0 };
	strcat(buf, "Error in: ");
	strcat(buf, funcName);
	printCustomError(buf);
}

// read a character from a file (return 0 if no character was read)
char getNextChar(int fd) {
	char ch;
	int ret = read(fd, &ch, 1);
	if (ret == ERROR) {
		printError("read");
		return ret;
	}
	// return 0 if no character was read
	return ret > 0 ? ch : 0;
}

// read a line into 'buf' (i.e. until '\n' or EOF)
// return the number of characters read
int getNextLine(int fd, char *buf) {
	int i;
	for (i = 0; i < MAX_PATH; i++) {
		// fetch the next character everytime
		char ch = getNextChar(fd);
		if (ch == ERROR) {
			return ERROR;
		}
		// stop at '\0' or '\n' and null-terminate the buffer
		if (ch == 0 || ch == '\n') {
			buf[i] = '\0';
			break;
		}
		// write the character to buf
		buf[i] = ch;
	}
	// return the length of buf
	return i;
}

// take a directory and an entry and combine them into one path
// write the path into 'buf'
void buildPath(char *buf, const char *dirPath, const char *entryName) {
	strcpy(buf, dirPath);
	size_t len = strlen(buf);
	// add a suffix slash to the dirPath (if missing)
	if (buf[len - 1] != '/') {
		buf[len] = '/';
		len++;
	}
	strcpy(buf + len, entryName);
}

// entries to skip when looking for students' subdirectories
int skipEntry(struct dirent *dirEntry) {
	return !strcmp(dirEntry->d_name, ".") || 
		!strcmp(dirEntry->d_name, "..") || 
		dirEntry->d_type != DT_DIR;
}

// return true if filename ends with ".c"
int isCodeFile(const char *fileName) {
	size_t len = strlen(fileName);
	return len > 1 && fileName[len - 2] == '.' && fileName[len - 1] == 'c';
}

// a simple predicate function that tests if a fileName is "a.out"
int isStudentBinFile(const char *fileName) {
	return !strcmp("a.out", fileName);
}

// use a predicate function to find a file in the directory
int findFile(char *buf, const char *dirPath, int (*predicate)(const char *)) {
	int found = FALSE;
	// run through the directory
	DIR *dir = opendir(dirPath);
	if (dir == NULL) { return ERROR; }
	struct dirent *entry;
	while (entry = readdir(dir)) {
		// let the predicate function determine if the file was found
		const int isFile = (entry->d_type != DT_DIR);
		const int hasCodeExtension = predicate(entry->d_name);
		if (isFile && hasCodeExtension) {
			if (buf) {
				// write the fileName to the buffer
				strcpy(buf, entry->d_name);
				found = TRUE;
			}
			break;
		}
	}
	// close resources
	if (closedir(dir) == ERROR) { return ERROR; }
	// return TRUE if file was found
	return found;
}

int getFileSize(int fd) {
	// go to the end of the file and return the offset
	return lseek(fd, 0, SEEK_END);
}

// file empty <-- file size = 0
int isFileEmpty(int fd) {
	int size = getFileSize(fd);
	if (size == ERROR) { return ERROR; }
	return size == 0;
}

// create a child process to call gcc on the student's code
int compileCode(struct Data *data, struct StudentData *sData) {	
	pid_t pid = fork();
	if (pid == ERROR) {
		printError("fork");
		return ERROR;
	} 
	if (pid == 0) {
		// child
		// redirect output to log file
		int fd = open(data->errorFilePath, O_WRONLY);
		// make sure we append to fd_error 
		// O_APPEND may not work on certain filesystems, so we manually seek the end of the file
		if (fd == ERROR || lseek(fd, 0, SEEK_END) == ERROR) { exit(ERROR); }
		if (dup2(fd, FD_ERROR) == ERROR ||
		    close(fd) == ERROR) {
			exit(ERROR);
		}

		// call gcc to compile the file in 'filePath'
		char *argv[5];
		argv[0] = "gcc";
		argv[1] = sData->codeFilePath;
		argv[2] = "-o";
		argv[3] = sData->binFilePath;
		argv[4] = NULL;
		// we can assume gcc is in the PATH env
		execvp("gcc", argv);
		// exit incase execvp returned an error
		exit(ERROR);
	}

	int status;
	// wait for the child process to finish compiling
	if (waitpid(pid, &status, 0) == ERROR) {
		printError("waitpid");
		return ERROR;
	}

	int ret = 0;
	if (WIFEXITED(status)) {
		ret = WEXITSTATUS(status);
	}

	// gcc returns 0 on success
	return ret == 0 ? EXCELLENT : COMPILATION_ERROR;
}

// run the student's code
// feed its input according the config file using redirection
// make sure it doesn't print to the terminal by redirecting output to errors.txt
int runCode(struct Data *data, struct StudentData *sData) {
	pid_t pid = fork();
	if (pid == ERROR) {
		printError("fork");
		return ERROR;
	} 
	if (pid == 0) {
		// child
		// redirect standard descriptors
		int fd_input = open(data->inputFilePath, O_RDONLY);
		int fd_output = open(sData->outputFilePath, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
		// make sure we append to fd_error 
		// O_APPEND may not work on certain filesystems, so we manually seek the end of the file
		int fd_error = open(data->errorFilePath, O_WRONLY);
		if (fd_error == ERROR || lseek(fd_error, 0, SEEK_END) == ERROR) { exit(ERROR); }
		if (fd_input == ERROR ||
		    fd_output == ERROR ||
		    dup2(fd_input, FD_STDIN) == ERROR ||
		    dup2(fd_output, FD_STDOUT) == ERROR ||
		    dup2(fd_error, FD_ERROR) == ERROR ||
		    close(fd_input) == ERROR ||
		    close(fd_output) == ERROR ||
		    close(fd_error) == ERROR) {
			exit(ERROR);
		}

		// run the student's program (a.out)
		char *argv[2];
		argv[0] = sData->binFilePath;
		argv[1] = NULL;
		execvp(sData->binFilePath, argv);
		// exit incase execvp returned an error
		exit(ERROR);
	}

	// start a timer and wait for the child process to end
	time_t startTime = time(NULL);
	if (waitpid(pid, NULL, 0) == ERROR) {
		printError("waitpid");
		return ERROR;
	}
	time_t endTime = time(NULL);
	// if the child took more than MAX_SECONDS to run we return TIMEOUT
	const int MAX_SECONDS = 5;
	return endTime - startTime > MAX_SECONDS ? TIMEOUT : EXCELLENT;
}

// this function uses 'comp.out' to compare the student's output to the correct output
int compareOutputs(struct Data *data, struct StudentData *sData) {
	pid_t pid = fork();
	if (pid == ERROR) {
		printError("fork");
		return ERROR;
	}
	if (pid == 0) {
		// child
		// redirect output to dev/null
		// because we don't want to print to the terminal
		int fd_out = open("/dev/null", O_WRONLY);
		if (fd_out == ERROR || 
		    dup2(fd_out, FD_STDOUT) == ERROR || 
		    dup2(fd_out, FD_ERROR) == ERROR ||
		    close(fd_out) == ERROR) {
			exit(ERROR);
		}
		// run comp.out
		char *argv[4];
		argv[0] = data->compareFilePath;
		argv[1] = data->outputComparisonPath;
		argv[2] = sData->outputFilePath;
		argv[3] = NULL;
		execvp(data->compareFilePath, argv);
		// exit incase execvp returned an error
		exit(ERROR);
	}

	int status;
	if (waitpid(pid, &status, 0) == ERROR) {
		perror("waitpid");
		return ERROR;
	}

	// get comp.out's return value
	int ret = 0;
	if (WIFEXITED(status)) {
		ret = WEXITSTATUS(status);
	}

	switch (ret) {
	case 1:  return EXCELLENT;
	case 2:  return WRONG;
	case 3:  return SIMILAR;
	default: return ERROR;
	}
}

// read the config file and initialize data's fields
int initData(struct Data *data, int fd_config) {
	// read paths from config file
	if (getNextLine(fd_config, data->mainDirPath) == ERROR ||
		getNextLine(fd_config, data->inputFilePath) == ERROR ||
		getNextLine(fd_config, data->outputComparisonPath) == ERROR) {
		return ERROR;
	}
	if (access(data->mainDirPath, F_OK) == ERROR) {
		if (errno == ENOENT) {
			printCustomError("Not a valid directory");	
		} else {
			printError("access");
		}
		return ERROR;
	}
	// check if input file exists
	if (access(data->inputFilePath, F_OK) == ERROR) {
		if (errno == ENOENT) {
			printCustomError("Input file not exist");
		} else {
			printError("access");
		}
		return ERROR;
	}
	// check if the output file exists
	if (access(data->outputComparisonPath, F_OK) == ERROR) {
		if (errno == ENOENT) {
			printCustomError("Output file not exist");
		} else {
			printError("access");
		}
		return ERROR;
	}

	// save compare file path
	strcpy(data->compareFilePath, "./comp.out");
	// create errors.txt file and save its path
	strcpy(data->errorFilePath, "./errors.txt");
	int fd_error = open(data->errorFilePath, O_CREAT | O_TRUNC, FILE_MODE);
	if (fd_error == ERROR) { 
		printError("open");
		return ERROR; 
	}
	if (close(fd_error) == ERROR) { 
		printError("close");
		return ERROR; 
	}

	return SUCCESS;
}

// helper function to get the appropriate reason for a grade
const char *getReason(int grade) {
	switch(grade) {
	case NO_C_FILE:         return "NO_C_FILE";
	case COMPILATION_ERROR: return "COMPILATION_ERROR";
	case TIMEOUT:           return "TIMEOUT";
	case WRONG:             return "WRONG";
	case SIMILAR:           return "SIMILAR";
	case EXCELLENT:         return "EXCELLENT";
	default:                return "No reason";
	}
}

// helper function to get a string format of a grade
// instead of using a conversion function
const char *getGradeStr(int grade) {
	switch(grade) {
	case NO_C_FILE:         return "0";
	case COMPILATION_ERROR: return "10";
	case TIMEOUT:           return "20";
	case WRONG:             return "50";
	case SIMILAR:           return "75";
	case EXCELLENT:         return "100";
	default:                return "0";
	}	
}

// this function writes a line into results.csv
void writeToCSV(int fd, const char *name, int grade) {
	const char *gradeStr = getGradeStr(grade);
	const char *reason = getReason(grade);

	char buf[MAX_BUF] = { 0 };
	strcat(buf, name);
	strcat(buf, ",");
	strcat(buf, gradeStr);
	strcat(buf, ",");
	strcat(buf, reason);
	strcat(buf, "\n");
	if(write(fd, buf, strlen(buf)) == ERROR) {}
}

// this function goes through the whole process of testing and grading one student
int gradeStudent(struct Data *data, struct StudentData *sData) {	
	// make sure the code file exists
	char codeFileName[MAX_PATH];
	if (findFile(codeFileName, sData->dirPath, isCodeFile) == FALSE) {
		return NO_C_FILE;
	}
	// save its path
	buildPath(sData->codeFilePath, sData->dirPath, codeFileName);

	// make sure the code compiles
	int compilationStatus = compileCode(data, sData);
	switch (compilationStatus) {
	case ERROR:
	case COMPILATION_ERROR:
		return compilationStatus;
	}

	// run the program and save its output
	int ranSuccessfully = runCode(data, sData);
	// delete the binary
	if (remove(sData->binFilePath) == ERROR) { return ERROR; }

	switch (ranSuccessfully) {
	case ERROR:
	case TIMEOUT:
		// delete the output file
		if (remove(sData->outputFilePath) == ERROR) { return ERROR; }
		return ranSuccessfully;
	}

	// compare the student's output to the correct output
	int compareResult = compareOutputs(data, sData);
	// delete the output file
	if (remove(sData->outputFilePath) == ERROR) { return ERROR; }

	// if we got here - no errors were found 
	return compareResult;
}

int startGrading(struct Data *data) {
	// open the directory that contains all of the students' directories
	DIR *mainDir = opendir(data->mainDirPath);
	// no need to print invalid dir path because we've already checked in init
	if (mainDir == NULL) {
		printError("opendir");
		return ERROR;
	}

	// create the results.csv file (or overwrite it)
	int fd_results = open("./results.csv", O_RDWR | O_CREAT | O_TRUNC, FILE_MODE);
	if (fd_results == ERROR) {
		printError("open");
		closedir(mainDir);
		return ERROR; 
	}

	// run through all the directory's entries
	struct dirent *dirEntry;
	while (dirEntry = readdir(mainDir)) {
		// we only care about directories (that aren't . or ..)
		if (skipEntry(dirEntry)) { continue; }

		// initialize paths
		struct StudentData sData;
		buildPath(sData.dirPath, data->mainDirPath, dirEntry->d_name);
		buildPath(sData.binFilePath, sData.dirPath, "a.out");
		buildPath(sData.outputFilePath, sData.dirPath, "student.out");
		// get the user's grade
		int grade = gradeStudent(data, &sData);
		if (grade != ERROR) {
			// finally, write the grade and reason into the csv file
			writeToCSV(fd_results, dirEntry->d_name, grade);
		}
	}

	// close resources
	int status = SUCCESS;
	if (closedir(mainDir) == ERROR) { status = ERROR; }
	if (close(fd_results) == ERROR) { status = ERROR; }
	return status;
}
