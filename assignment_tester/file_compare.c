#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define TRUE      (1)
#define FALSE     (0)

#define SUCCESS   (0)
#define ERROR     (-1)

#define BUF_SIZE  (512)
#define FD_STDOUT (1)

// compareFiles return value
enum ComparisonStatus {
	FILES_ERROR     = -1,
	FILES_IDENTICAL = 1,
	FILES_DIFFERENT = 2,
	FILES_SIMILAR   = 3,
};

struct File {
	// file descriptor
	int fd;
	// read position in the buffer
	int pos;
	// amount of bytes read into the buffer
	int size;
	char buf[BUF_SIZE];
};

void printCustomError(const char *msg);
void printError(const char *funcName);

int openFile(struct File *file, const char *path);
int resetFilePosition(struct File *file);
int closeFile(struct File *file);
int readFile(struct File *file, char *buf, int bytes);

char getNextChar(struct File *file);
char skipSpace(struct File *file);

int compareFiles(struct File *firstFile, struct File *secondFile, int similar);
enum ComparisonStatus getCmpStat(struct File *firstFile, struct File *secondFile);

int main(int argc, char *argv[]) {
	if (argc < 3) {
		// not enough arguments
		return ERROR;
	}

	// grab the paths to the input files
	const char *firstPath = argv[1];
	const char *secondPath = argv[2];
	
	// allocate file structs on the stack
	struct File firstFile;
	struct File secondFile;

	// open the files
	if (openFile(&firstFile, firstPath) == ERROR) {
		return ERROR;		
	}
	if (openFile(&secondFile, secondPath) == ERROR) {
		// we don't have to check for the return value
		// because we return ERROR either way
		closeFile(&firstFile);		
		return ERROR;		
	}

	// check if files are identical, similar or different
	enum ComparisonStatus status = getCmpStat(&firstFile, &secondFile);
	
	// get rid of open resources
	int firstClose = closeFile(&firstFile);
	int secondClose = closeFile(&secondFile);
	if (firstClose == ERROR || secondClose == ERROR || status == FILES_ERROR) {
		// a system call somewhere in the code returned an error
		return ERROR;
	}
	
	return status;
}

void printCustomError(const char *msg) {
	char buf[BUF_SIZE] = { 0 };
	strcat(buf, msg);
	strcat(buf, "\n");
	if (write(FD_STDOUT, buf, strlen(buf)) == ERROR) {}
}

void printError(const char *funcName) {
	char buf[BUF_SIZE] = { 0 };
	strcat(buf, "Error in: ");
	strcat(buf, funcName);
	printCustomError(buf);
}

// a simple wrapper for open
int openFile(struct File *file, const char *path) {
	int fd = open(path, O_RDONLY);
	if (fd == ERROR) {
		printError("open");
		return ERROR;
	}

	// initialize the file's fields
	file->fd = fd;
	file->size = 0;
	file->pos = 0;
	
	return SUCCESS;
}

// a simple wrapper for lseek
int resetFilePosition(struct File *file) {
	// move the offset to the beginning of the file
	if (lseek(file->fd, 0, SEEK_SET) == ERROR) {
		printError("lseek");
		return ERROR;
	}
	file->size = 0;
	file->pos = 0;
	return SUCCESS;
}

int closeFile(struct File *file) {
	if (close(file->fd) == ERROR) {
		printError("close");
		return ERROR;
	}
	return SUCCESS;
}

// this function is similar to what we've seen in the recitation
// fill 'buf' with the file's data
int readFile(struct File *file, char *buf, int bytes) {
	int i = 0;
	for (i = 0; i < bytes; i++) {
		// if we've read all the data in the buffer
		if (file->pos == file->size) {
			// reset the position and read new data from the file
			file->pos = 0;
			file->size = read(file->fd, file->buf, BUF_SIZE);
			if (file->size == ERROR) {
				printError("read");
				return ERROR;
			}
			// no more data to read from the file - simply return i
			if (file->size == 0) {
				return i;
			}
		}

		// fill buf with the file's data
		buf[i] = file->buf[file->pos];
		file->pos++;
	}

	// return the amount of bytes we've read
	return i;
}

// simple wrapper for readFile
char getNextChar(struct File *file) {
	char ch;
	int ret = readFile(file, &ch, 1);
	if (ret == ERROR || ret == 0) {
		return ret;
	}
	return ch;
}

// return the first character that is not a space
char skipSpace(struct File *file) {
	char ch;
	do {
		// keep reading the next char as long as we're reading spaces
		ch = getNextChar(file);
		if (ch == ERROR || ch == 0) {
			return ch;
		}
	} while (isspace(ch));

	return ch;
}

int compareFiles(struct File *firstFile, struct File *secondFile, int similar) {
	char firstCh, secondCh;
	while (TRUE) {
		// read the next character
		// if we're only checking similarity - ignore spaces and case
		firstCh = similar ? toupper(skipSpace(firstFile)) : getNextChar(firstFile);
		secondCh = similar ? toupper(skipSpace(secondFile)) : getNextChar(secondFile);
		if (firstCh == ERROR || secondCh == ERROR) {
			return ERROR;
		}
		// we reached the end of one of the files
		// or found two different characters
		if (firstCh == 0 || secondCh == 0 || firstCh != secondCh) {
			break;
		}
	}
	// would return true if we reached the end of both files
	return firstCh == secondCh;
}

enum ComparisonStatus getCmpStat(struct File *firstFile, struct File *secondFile) {
	// check if files are completely identical
	int identical = compareFiles(firstFile, secondFile, FALSE);
	if (identical == ERROR) {
		return ERROR;
	}
	if (identical == TRUE) {
		return FILES_IDENTICAL;
	}

	// reset the files (using lseek)
	if (resetFilePosition(firstFile) == ERROR || 
		resetFilePosition(secondFile) == ERROR) {
		return ERROR;
	}

	// check if files are similar
	int similar = compareFiles(firstFile, secondFile, TRUE);
	if (similar == ERROR) {
		return ERROR;
	}
	if (similar == TRUE) {
		return FILES_SIMILAR;
	}

	// files aren't identical or even similar
	return FILES_DIFFERENT;
}
