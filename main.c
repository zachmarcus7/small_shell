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
* Global variables, for use with foreground-only mode
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
* Function:    main
* Description: This is the main function for the shell. It's separated
*              into three main parts: getting the user input, checking the user
*              input, and checking if any background processes have terminated.
* Parameters:  int, char*
* Returns:     EXIT_SUCCESS
*********************************************************************/
int main(int argc, char* argv[]) {
	//initialize variables for running the shell
	bool shell = 1;
	bool background;
	char userInput[2048];
	char* userArguments[512];
	char* savePtr;
	int index;
	pid_t childPid;
	int childStatus = 0;
	int inputFileDesc = 0;
	int outputFileDesc = 0;
	char* inputFileName = NULL;
	char* outputFileName = NULL;
	int result;

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


	//run the shell
	while (shell) {


		/* get user input, store it inside userArguments array */

		printf(": ");
		fflush(stdout);
		fgets(userInput, 2048, stdin);
		
		//separate out each piece of user input
		char* argument = strtok_r(userInput, " \n", &savePtr);
		index = 0;
		background = 0;

		//check if nothing was entered
		if (argument == NULL)
			continue;

		//store each argument into userArguments array
		while (argument != NULL) {

			//check if input symbol has been encountered
			if (!strcmp(argument, "<")) {
				argument = strtok_r(NULL, " \n", &savePtr);
				inputFileName = strdup(argument);
				argument = strtok_r(NULL, " \n", &savePtr);
				continue;
			}

			//check if output symbol has been encountered
			if (!strcmp(argument, ">")) {
				argument = strtok_r(NULL, " \n", &savePtr);
				outputFileName = strdup(argument);
				argument = strtok_r(NULL, " \n", &savePtr);
				continue;
			}

			userArguments[index] = strdup(argument);
			argument = strtok_r(NULL, " \n", &savePtr);

			//check if final argument is a &, for background command
			if (argument == NULL) {
				if (!strcmp(userArguments[index], "&")) {
					background = 1;
					break;
				}
			}
			index++;
		}

		//set the last element to NULL, for use with exec functions
		userArguments[index] = NULL;

		//expand the $$ variable
		for (int i = 0; i < index; i++) 
			expandVariable(userArguments[i]);
		

		/* test user input */

		//if user entered exit
		if (!strcmp(userArguments[0], "exit")) {
			shell = 0;
			exit(0);
		}
		//if user entered cd 
		else if (!strcmp(userArguments[0], "cd")) {
			//check if there were arguments passed with cd
			if (userArguments[1] == NULL)
				chdir(getenv("HOME"));
			else {
				if (chdir(userArguments[1]) == -1) {
					printf("No such directory\n");
					fflush(stdout);
				}
			}
		}
		//if user entered status
		else if (!strcmp(userArguments[0], "status")) {
			//if child process terminated normally
			if (WIFEXITED(childStatus)) {
				printf("exit value %d\n", WEXITSTATUS(childStatus));
				fflush(stdout);
			} 
			else {
				printf("terminated by signal %d\n", WTERMSIG(childStatus));
				fflush(stdout);
			}
		}
		//if user entered a comment
		else if (*userArguments[0] == '#') {
			continue;
		}
		//if foreground-only signal was received
		else if (signalReceived) {
			signalReceived = 0;
			continue;
		}
		//if user entered a different command
		else {
			//create a new process
			childPid = fork();

			//check what kind of process is running
			switch (childPid) {
				//error in creating process
				case -1:
					childStatus = 1;
					break;
				//child process 
				case 0: 
					//if the process is a background command
					if (background) {
						//redirect standard in to /dev/null
						inputFileDesc = open("/dev/null", O_RDONLY);
						if (inputFileDesc == -1) {
							printf("cannot open\n");
							fflush(stdout);
							exit(1);
						}

						//change standard in (0) to inputFileName
						result = dup2(inputFileDesc, 0);
						if (result == -1) {
							exit(1);
						}

						//redirect standard out to /dev/null
						outputFileDesc = open("/dev/null", O_RDWR | O_CREAT | O_TRUNC, 0777);
						if (outputFileDesc == -1) {
							printf("cannot open\n");
							fflush(stdout);
							exit(1);
						}

						//change standard out (1) to outputFileName
						result = dup2(outputFileDesc, 1);
						if (result == -1) {
							exit(1);
						}
					}

					//input redirection
					if (inputFileName != NULL) {

						//open the file for reading only
						inputFileDesc = open(inputFileName, O_RDONLY);
						if (inputFileDesc == -1) {
							printf("cannot open %s for input\n", inputFileName);
							fflush(stdout);
							exit(1);
						}

						//change standard in (0) to inputFileName
						result = dup2(inputFileDesc, 0);
						if (result == -1) {
							exit(1);
						}
						close(inputFileDesc);
					}

					//output redirection
					if (outputFileName != NULL) {

						//open the file for reading/writing, if it's not there create it, or truncate onto it
						outputFileDesc = open(outputFileName, O_RDWR | O_CREAT | O_TRUNC, 0777);
						if (outputFileDesc == -1) {
							printf("cannot open %s for output\n", outputFileName);
							fflush(stdout);
							exit(1);
						}

						//change standard out (1) to outputFileName
						result = dup2(outputFileDesc, 1);
						if (result == -1) {
							exit(1);
						}
						close(outputFileDesc);
					}

					//if the process is a foreground process
					if (background == 0) {

						//foreground child process terminates with SIGINT
						interruptSignal.sa_handler = SIG_DFL;
						interruptSignal.sa_flags = 0;
						sigaction(SIGINT, &interruptSignal, NULL);
					} 
					//if foreground mode is enabled
					else if (foregroundMode) {

						//if in foreground-only mode, background process terminates with SIGINT
						interruptSignal.sa_handler = SIG_DFL;
						interruptSignal.sa_flags = 0;
						sigaction(SIGINT, &interruptSignal, NULL);
					}

					//execute the command
					execvp(userArguments[0], userArguments);
					printf("%s: no such file or directory\n", userArguments[0]);
					exit(2);
					break;
				//parent process
				default:
					//if foregroundMode is active, parent must wait till all processes complete
					if (foregroundMode) {
						childPid = waitpid(childPid, &childStatus, 0);

						//check if a SIGINT was sent
						if (WIFSIGNALED(childStatus)) {
							printf("terminated by signal %d\n", WTERMSIG(childStatus));
							fflush(stdout);
						}
					}
					//if background process, command line access immediately returned
					else if (background) {
						printf("background pid is %d\n", childPid);
						fflush(stdout);
					}
					//foreground process, parent must wait till it completes
					else {

						childPid = waitpid(childPid, &childStatus, 0);
						
						//check if a SIGINT was sent
						if (WIFSIGNALED(childStatus)) {
							printf("terminated by signal %d\n", WTERMSIG(childStatus));
							fflush(stdout);
						}
					}
					break;
			}	
		}


		/* check if any background processes have terminated */

		//have waitpid wait for any child process, WNOHANG returns immediately if no child process has terminated
		childPid = waitpid(-1, &childStatus, WNOHANG);

		//enter a loop, clearing out remaining  child processes waiting to be terminated
		while (childPid > 0) { 

			if (WIFEXITED(childStatus)) {
				printf("background pid %d is done: exit value %d \n", childPid, WEXITSTATUS(childStatus));
				fflush(stdout);
			}
			else {
				printf("background pid %d is done: terminated by signal %d\n", childPid, WTERMSIG(childStatus));
				fflush(stdout);
			}

			//get the process id for the next background process waiting to be terminated
			childPid = waitpid(-1, &childStatus, WNOHANG);
		}

		//reset character pointers for input/output files
		inputFileName = NULL;
		outputFileName = NULL;

		//reset userArgument pointers
		for (int i = 0; i < index; i++) {
			free(userArguments[i]);
		}
	}
	return EXIT_SUCCESS;
}
