/********************************************************************
* Author: Zach Marcus
* Last Modified: 2/6/2021
* OSU email address: marcusz@oregonstate.edu
* Course number/section: CS344 Section 400
* Assignment Number: 3
* Description: This program emulates a small shell. It provides a prompt
*              to the user for running commands such as exit, cd, status, etc.
*              It also handles blank/comment lines, provides variable expansion for $$, 
*              supports input/output redirection, supports background/foreground processes,
*              and has custom signal handlers for SIGNINT and SIGTSTP.
*********************************************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>


/*********************************************************************
* Container for shell variables.
*********************************************************************/
struct shellVars {
	bool run;
	bool background;
	char userInput[2048];
	char* userArguments[512];
	char* savePtr;
	int index;
	pid_t childPid;
	int childStatus;
	int inputFileDesc;
	int outputFileDesc;
	char* inputFileName;
	char* outputFileName;
	int result;
	struct sigaction interruptSignal;
};


/*********************************************************************
* Global variables, for use with foreground-only mode.
*********************************************************************/
bool foregroundMode = 0;
bool signalReceived = 0;


/*********************************************************************
* Function:    enableFgMode
* Description: This function is the signal handler for SIGTSTP. This function
*              either enables or disables fore-ground only mode, depending on which
*              mode is currently active.
* Parameters:  int
* Returns:     void
*********************************************************************/
void enableFgMode(int fgModeEnabled) {

	//indicate that a signal has been received
	signalReceived = 1;
	
	//check if program is currently in foreground-only mode 
	if (foregroundMode == 0) {
		//write is used here because of its reentrancy, writes line to standard out (1)
		write(1, "\nEntering foreground-only mode (& is now ignored)\n", 51);
		fflush(stdout);
		foregroundMode = 1;
	}
	else {
		//write is used here because of its reentrancy, writes line to standard out (1)
		write(1, "\nExiting foreground-only mode\n", 31);
		fflush(stdout);
		foregroundMode = 0;
	}
}


/*********************************************************************
* Function:    expandVariable
* Description: This function takes a character array and expands all the
*              instances of $$ within it to the process ID.
* Parameters:  char*
* Returns:     void
*********************************************************************/
void expandVariable(char* array) {

	//set up new array
	char* newArray = malloc(2048);
	bool converted = 0;

	//get the process id in string form
	int pid = getpid();
	char pidArray[50];
	sprintf(pidArray, "%d", pid);
	int index = 0;
	int newIndex = 0;
	
	while (index != strlen(array)) {

		//check if $$ is encountered in string
		if ((array[index] == '$') && (array[index + 1] == '$')) {
			
			//copy in pid to new array, increment where its last index is
			strcat(newArray, pidArray);
			newIndex += strlen(pidArray);

			//increment original index to get to the right spot
			index += 2;

			//indicate that a $$ variable has been encountered
			converted = 1;
		}
		else {
			//if $$ isn't encountered, then copy over normally
			newArray[newIndex] = array[index];
			newIndex++;
			index++;
		}
	}

	//if a $$ variable has been encountered, set original string to new string
	if (converted)
		strcpy(array, newArray);
}


/*********************************************************************
* Function:    getInput
* Description: This function gets the user input, parses it into tokens,
*              while also checking for certain characters. Returns 1 if
*              shell loop is meant to re-loop, otherwise it returns 2.
* Parameters:  struct shellVars*
* Returns:     int
*********************************************************************/
int getInput(struct shellVars *shell) {

	//get the user's input
	printf(": ");
	fflush(stdout);
	fgets(shell->userInput, 2048, stdin);

	//separate out each piece of user input
	char* argument = strtok_r(shell->userInput, " \n", &shell->savePtr);
	shell->index = 0;
	shell->background = 0;

	//check if nothing was entered
	if (argument == NULL)
		return 1;

	//store each argument into userArguments array
	while (argument != NULL) {

		//check if input symbol has been encountered
		if (!strcmp(argument, "<")) {
			argument = strtok_r(NULL, " \n", &shell->savePtr);
			shell->inputFileName = strdup(argument);
			argument = strtok_r(NULL, " \n", &shell->savePtr);
			continue;
		}

		//check if output symbol has been encountered
		if (!strcmp(argument, ">")) {
			argument = strtok_r(NULL, " \n", &shell->savePtr);
			shell->outputFileName = strdup(argument);
			argument = strtok_r(NULL, " \n", &shell->savePtr);
			continue;
		}

		shell->userArguments[shell->index] = strdup(argument);
		argument = strtok_r(NULL, " \n", &shell->savePtr);

		//check if final argument is a &, for background command
		if (argument == NULL) {
			if (!strcmp(shell->userArguments[shell->index], "&")) {
				shell->background = 1;
				break;
			}
		}
		shell->index++;
	}

	//set the last element to NULL, for use with exec functions
	shell->userArguments[shell->index] = NULL;

	//expand the $$ variable
	for (int i = 0; i < shell->index; i++)
		expandVariable(shell->userArguments[i]);

	return 2;
}


/*********************************************************************
* Function:    createNewProcess
* Description: This function creates forks a new process and checks 
*              whether foreground or background mode is currently enabled.
* Parameters:  struct shellVars*
* Returns:     void
*********************************************************************/
void createNewProcess(struct shellVars* shell) {

	//create a new process
	shell->childPid = fork();

	//check what kind of process is running
	switch (shell->childPid) {
		//error in creating process
		case -1:
			shell->childStatus = 1;
			break;
		//child process 
		case 0:
			//if the process is a background command
			if (shell->background) {
				//redirect standard in to /dev/null
				shell->inputFileDesc = open("/dev/null", O_RDONLY);
				if (shell->inputFileDesc == -1) {
					printf("cannot open\n");
					fflush(stdout);
					exit(1);
				}

				//change standard in (0) to inputFileName
				shell->result = dup2(shell->inputFileDesc, 0);
				if (shell->result == -1) {
					exit(1);
				}

				//redirect standard out to /dev/null
				shell->outputFileDesc = open("/dev/null", O_RDWR | O_CREAT | O_TRUNC, 0777);
				if (shell->outputFileDesc == -1) {
					printf("cannot open\n");
					fflush(stdout);
					exit(1);
				}

				//change standard out (1) to outputFileName
				shell->result = dup2(shell->outputFileDesc, 1);
				if (shell->result == -1) {
					exit(1);
				}
			}

			//input redirection
			if (shell->inputFileName != NULL) {

				//open the file for reading only
				shell->inputFileDesc = open(shell->inputFileName, O_RDONLY);
				if (shell->inputFileDesc == -1) {
					printf("cannot open %s for input\n", shell->inputFileName);
					fflush(stdout);
					exit(1);
				}

				//change standard in (0) to inputFileName
				shell->result = dup2(shell->inputFileDesc, 0);
				if (shell->result == -1) {
					exit(1);
				}
				close(shell->inputFileDesc);
			}

			//output redirection
			if (shell->outputFileName != NULL) {

				//open the file for reading/writing, if it's not there create it, or truncate onto it
				shell->outputFileDesc = open(shell->outputFileName, O_RDWR | O_CREAT | O_TRUNC, 0777);
				if (shell->outputFileDesc == -1) {
					printf("cannot open %s for output\n", shell->outputFileName);
					fflush(stdout);
					exit(1);
				}

				//change standard out (1) to outputFileName
				shell->result = dup2(shell->outputFileDesc, 1);
				if (shell->result == -1) {
					exit(1);
				}
				close(shell->outputFileDesc);
			}

			//if the process is a foreground process
			if (shell->background == 0) {

				//foreground child process terminates with SIGINT
				shell->interruptSignal.sa_handler = SIG_DFL;
				shell->interruptSignal.sa_flags = 0;
				sigaction(SIGINT, &shell->interruptSignal, NULL);
			}
			//if foreground mode is enabled
			else if (foregroundMode) {

				//if in foreground-only mode, background process terminates with SIGINT
				shell->interruptSignal.sa_handler = SIG_DFL;
				shell->interruptSignal.sa_flags = 0;
				sigaction(SIGINT, &shell->interruptSignal, NULL);
			}

			//execute the command
			execvp(shell->userArguments[0], shell->userArguments);
			printf("%s: no such file or directory\n", shell->userArguments[0]);
			exit(2);
			break;
		//parent process
		default:
			//if foregroundMode is active, parent must wait till all processes complete
			if (foregroundMode) {
				shell->childPid = waitpid(shell->childPid, &shell->childStatus, 0);

				//check if a SIGINT was sent
				if (WIFSIGNALED(shell->childStatus)) {
					printf("terminated by signal %d\n", WTERMSIG(shell->childStatus));
					fflush(stdout);
				}
			}
			//if background process, command line access immediately returned
			else if (shell->background) {
				printf("background pid is %d\n", shell->childPid);
				fflush(stdout);
			}
			//foreground process, parent must wait till it completes
			else {

				shell->childPid = waitpid(shell->childPid, &shell->childStatus, 0);

				//check if a SIGINT was sent
				if (WIFSIGNALED(shell->childStatus)) {
					printf("terminated by signal %d\n", WTERMSIG(shell->childStatus));
					fflush(stdout);
				}
			}
			break;
		}
}



/*********************************************************************
* Function:    testInput
* Description: This function tests the user input. Returns 1 if
*              shell loop is meant to re-loop, otherwise it returns 2.
* Parameters:  struct shellVars*
* Returns:     int
*********************************************************************/
int testInput(struct shellVars* shell) {

	//if user entered exit
	if (!strcmp(shell->userArguments[0], "exit")) {
		shell->run = 0;
		exit(0);
	}
	//if user entered cd 
	else if (!strcmp(shell->userArguments[0], "cd")) {
		//check if there were arguments passed with cd
		if (shell->userArguments[1] == NULL)
			chdir(getenv("HOME"));
		else {
			if (chdir(shell->userArguments[1]) == -1) {
				printf("No such directory\n");
				fflush(stdout);
			}
		}
	}
	//if user entered status
	else if (!strcmp(shell->userArguments[0], "status")) {
		//if child process terminated normally
		if (WIFEXITED(shell->childStatus)) {
			printf("exit value %d\n", WEXITSTATUS(shell->childStatus));
			fflush(stdout);
		}
		else {
			printf("terminated by signal %d\n", WTERMSIG(shell->childStatus));
			fflush(stdout);
		}
	}
	//if user entered a comment
	else if (*shell->userArguments[0] == '#') {
		return 1;
	}
	//if foreground-only signal was received
	else if (signalReceived) {
		signalReceived = 0;
		return 1;
	}
	//if user entered a different command
	else {
		createNewProcess(shell);
	}
	return 2;
}


/*********************************************************************
* Function:    checkBackground
* Description: This function checks if any background processes have 
*              terminated.
* Parameters:  struct shellVars*
* Returns:     void
*********************************************************************/
void checkBackground(struct shellVars* shell) {

	//have waitpid wait for any child process, WNOHANG returns immediately if no child process has terminated
	shell->childPid = waitpid(-1, &shell->childStatus, WNOHANG);

	//enter a loop, clearing out remaining  child processes waiting to be terminated
	while (shell->childPid > 0) {

		if (WIFEXITED(shell->childStatus)) {
			printf("background pid %d is done: exit value %d \n", shell->childPid, WEXITSTATUS(shell->childStatus));
			fflush(stdout);
		}
		else {
			printf("background pid %d is done: terminated by signal %d\n", shell->childPid, WTERMSIG(shell->childStatus));
			fflush(stdout);
		}

		//get the process id for the next background process waiting to be terminated
		shell->childPid = waitpid(-1, &shell->childStatus, WNOHANG);
	}

}


/*********************************************************************
* Function:    resetPtrs
* Description: This function takes a pointer to a shell struct and resets
*              the pointers inside of it to NULL, so that they're ready
*              for use with the next input.
* Parameters:  struct shellVars*
* Returns:     void
*********************************************************************/
void resetPtrs(struct shellVars* shell) {

	//reset character pointers for input/output files
	shell->inputFileName = NULL;
	shell->outputFileName = NULL;

	//reset userArgument pointers
	for (int i = 0; i < shell->index; i++) 
		free(shell->userArguments[i]);

}


/*********************************************************************
* Function:    main
* Description: This is the main function for the shell. It's separated
*              into three main parts: getting the user input, checking the user
*              input, and checking if any background processes have terminated.
* Parameters:  int, char*
* Returns:     EXIT_SUCCESS
*********************************************************************/
int main(int argc, char* argv[]) {

	//create signal handler for SIGINT
	struct sigaction interruptSignal;
	interruptSignal.sa_handler = SIG_IGN;
	interruptSignal.sa_flags = 0;
	sigaction(SIGINT, &interruptSignal, NULL);

	//create signal handler for SIGTSTP
	struct sigaction stopSignal;
	stopSignal.sa_handler = enableFgMode;
	sigfillset(&stopSignal.sa_mask);
	stopSignal.sa_flags = 0;
	sigaction(SIGTSTP, &stopSignal, NULL);

	//initialize variables for running the shell
	struct shellVars shell = { .run = 1, .inputFileName = NULL, .outputFileName = NULL, .interruptSignal = interruptSignal };
	struct shellVars* shellPtr = &shell;


	while (shell.run) {

		//get user input, then test it
		if (getInput(shellPtr) == 1)
			continue;

		if (testInput(shellPtr) == 1)
			continue;

		checkBackground(shellPtr);
		resetPtrs(shellPtr);
	}

	return EXIT_SUCCESS;
}
