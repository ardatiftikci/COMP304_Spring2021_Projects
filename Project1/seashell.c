#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>

const char * sysname = "seashell";

enum return_codes {
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t {
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3]; // in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t * command)
{
	int i=0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background?"yes":"no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete?"yes":"no");
	printf("\tRedirects:\n");
	for (i=0;i<3;i++)
		printf("\t\t%d: %s\n", i, command->redirects[i]?command->redirects[i]:"N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i=0;i<command->arg_count;++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}


}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i=0; i<command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i=0;i<3;++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next=NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
    gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters=" \t"; // split at whitespace
	int index, len;
	len=strlen(buf);
	while (len>0 && strchr(splitters, buf[0])!=NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len>0 && strchr(splitters, buf[len-1])!=NULL)
		buf[--len]=0; // trim right whitespace

	if (len>0 && buf[len-1]=='?') // auto-complete
		command->auto_complete=true;
	if (len>0 && buf[len-1]=='&') // background
		command->background=true;

	char *pch = strtok(buf, splitters);
	command->name=(char *)malloc(strlen(pch)+1);
	if (pch==NULL)
		command->name[0]=0;
	else
		strcpy(command->name, pch);

	command->args=(char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index=0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch) break;
		arg=temp_buf;
		strcpy(arg, pch);
		len=strlen(arg);

		if (len==0) continue; // empty arg, go for next
		while (len>0 && strchr(splitters, arg[0])!=NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len>0 && strchr(splitters, arg[len-1])!=NULL) arg[--len]=0; // trim right whitespace
		if (len==0) continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|")==0)
		{
			struct command_t *c=malloc(sizeof(struct command_t));
			int l=strlen(pch);
			pch[l]=splitters[0]; // restore strtok termination
			index=1;
			while (pch[index]==' ' || pch[index]=='\t') index++; // skip whitespaces

			parse_command(pch+index, c);
			pch[l]=0; // put back strtok termination
			command->next=c;
			continue;
		}

		// background process
		if (strcmp(arg, "&")==0)
			continue; // handled before

		// handle input redirection
		redirect_index=-1;
		if (arg[0]=='<')
			redirect_index=0;
		if (arg[0]=='>')
		{
			if (len>1 && arg[1]=='>')
			{
				redirect_index=2;
				arg++;
				len--;
			}
			else redirect_index=1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index]=malloc(len);
			strcpy(command->redirects[redirect_index], arg+1);
			continue;
		}

		// normal arguments
		if (len>2 && ((arg[0]=='"' && arg[len-1]=='"')
			|| (arg[0]=='\'' && arg[len-1]=='\''))) // quote wrapped arg
		{
			arg[--len]=0;
			arg++;
		}
		command->args=(char **)realloc(command->args, sizeof(char *)*(arg_index+1));
		command->args[arg_index]=(char *)malloc(len+1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count=arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8); // go back 1
	putchar(' '); // write empty over
	putchar(8); // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index=0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

    // tcgetattr gets the parameters of the current terminal
    // STDIN_FILENO will tell tcgetattr that it should write the settings
    // of stdin to oldt
    static struct termios backup_termios, new_termios;
    tcgetattr(STDIN_FILENO, &backup_termios);
    new_termios = backup_termios;
    // ICANON normally takes care that one line at a time will be processed
    // that means it will return if it sees a "\n" or an EOF or an EOL
    new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
    // Those new settings will be set to STDIN
    // TCSANOW tells tcsetattr to change attributes immediately.
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);


    //FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state=0;
	buf[0]=0;
  	while (1)
  	{
		c=getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c==9) // handle tab
		{
			buf[index++]='?'; // autocomplete
			break;
		}

		if (c==127) // handle backspace
		{
			if (index>0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c==27 && multicode_state==0) // handle multi-code keys
		{
			multicode_state=1;
			continue;
		}
		if (c==91 && multicode_state==1)
		{
			multicode_state=2;
			continue;
		}
		if (c==65 && multicode_state==2) // up arrow
		{
			int i;
			while (index>0)
			{
				prompt_backspace();
				index--;
			}
			for (i=0;oldbuf[i];++i)
			{
				putchar(oldbuf[i]);
				buf[i]=oldbuf[i];
			}
			index=i;
			continue;
		}
		else
			multicode_state=0;

		putchar(c); // echo the character
		buf[index++]=c;
		if (index>=sizeof(buf)-1) break;
		if (c=='\n') // enter key
			break;
		if (c==4) // Ctrl+D
			return EXIT;
  	}
  	if (index>0 && buf[index-1]=='\n') // trim newline from the end
  		index--;
  	buf[index++]=0; // null terminate string

  	strcpy(oldbuf, buf);

  	parse_command(buf, command);

  	// print_command(command); // DEBUG: uncomment for debugging

    // restore the old settings
    tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  	return SUCCESS;
}
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command=malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code==EXIT) break;

		code = process_command(command);
		if (code==EXIT) break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

void shortdir(struct command_t *command){
	char* homedir = strcat(strdup(getenv("HOME")),"/");
	char* shortdirCommand = command->args[0];
	char* shortdirName = command->args[1];
	char* homedircpy1 = strdup(homedir);
	char* homedircpy2 = strdup(homedir);
	
	//take path of home directory and create the file which includes name and path of directory in home directory
	char* filedir = strdup(strcat(homedircpy1,"shortdirList.txt"));
	
	if(strcmp(shortdirCommand,"set")==0){
	
		//copy all line from previous file and check if the alias is used before
		char* filedir2 = malloc(100);
		filedir2 = strdup(strcat(homedircpy2,"shortdirList2.txt"));
		FILE *originalFile = fopen(filedir,"r");
		FILE *copyFile = fopen(filedir2, "w");
		char line[100];
		char* dirName;
		char* dirAddress;
		
		char cwd[100];
		getcwd(cwd, 100);
		int multipleAlias = 0; //flag
		if(originalFile!=NULL){
			while(fgets(line, 100, originalFile)){
				dirName = strtok(line, " ");
				dirAddress = strtok(NULL, " ");
				if(strcmp(dirName, shortdirName)!=0){
					//the alias is not equal to new alias so add them to newly created file
					fputs(dirName, copyFile);
					fputs(" ", copyFile);
					fputs(dirAddress, copyFile);
				}
				
				dirAddress[strcspn(dirAddress,"\n")]=0;
				if(strcmp(dirAddress,cwd)==0){
					//if there are multiple alias for one directory set the flag to warn the user
					multipleAlias = 1;
				}
			}
		}

		if(multipleAlias) printf("WARNING: You are setting multiple alias for one directory\n");
		
		//add new association to new file
		fputs(shortdirName, copyFile);
		fputs(" ", copyFile);
		fputs(cwd, copyFile);
		fputs("\n", copyFile);
		
		printf("%s is set as an alias for %s\n", shortdirName, cwd);
		
		remove(filedir);
		rename(filedir2, filedir);
		//remove old file and rename new file
		if(originalFile!=NULL) fclose(originalFile);
		fclose(copyFile);
	}
	
	else if(strcmp(shortdirCommand,"jump")==0){
		FILE *file = fopen(filedir,"r");
		char line[100];
		char* dirName;
		char* dirAddress;
		while(fgets(line, 100, file)){
			line[strcspn(line,"\n")]=0;
			//omit new line character
			dirName = strtok(line, " ");
			dirAddress = strtok(NULL, " ");
			if(strcmp(dirName, shortdirName)==0){
				//if shortdir jump dirName equals a name in the list jump to that directory and finish
				chdir(dirAddress);	
				printf("Jumped to %s %s\n", dirName, dirAddress);
				break;
			}
		}

		fclose(file);
	}
	
	else if(strcmp(shortdirCommand,"del")==0){
		//to delete a line copy lines which is not that line to another file and rename it into shortdirList 
		char* filedir2 = malloc(100);
		filedir2 = strcat(homedircpy2,"shortdirList2.txt");
		FILE *originalFile = fopen(filedir,"r");
		FILE *copyFile = fopen(filedir2, "w");
		char line[100];
		char* dirName;
		char* dirAddress;
		while(fgets(line, 100, originalFile)){
			dirName = strtok(line, " ");
			dirAddress = strtok(NULL, " ");
			if(strcmp(dirName, shortdirName)!=0){
				//if we do not want to delete this directory include it in new line
				fputs(dirName, copyFile);
				fputs(" ", copyFile);
				fputs(dirAddress, copyFile);
			}
		}
		remove(filedir);
		rename(filedir2, filedir);
		//remove old file and rename new file
		fclose(originalFile);
		fclose(copyFile);
		
		printf("%s is deleted\n", shortdirName);
	}
		
	else if(strcmp(shortdirCommand,"clear")==0){
		remove(filedir);
		printf("All shortdirs are removed\n");
		//remove shortdirList
	}	
		
	else if(strcmp(shortdirCommand,"list")==0){
		//print all lines of shortdirList
		FILE *originalFile = fopen(filedir,"r");
		if(originalFile==NULL) printf("No associations found\n");
		else{
			char line[100];
			char* dirName;
			char* dirAddress;
			printf("Shortdir List:\n");
			while(fgets(line, 100, originalFile)){
				printf("%s",line);
			}
			fclose(originalFile);
		}
	}
}

void goodMorning(struct command_t *command){
	char* alarmTime = malloc(10);
	alarmTime = command->args[1];
	char* musicAddress = malloc(100);
	musicAddress = command->args[2];
	//take arguments from command
	
	FILE* schedFile = fopen("sched_job.txt","w");
	char* hour = strtok(alarmTime,".");
	char* minute = strtok(NULL, ".");
	fputs(minute, schedFile);
	fputs(" ", schedFile);
	fputs(hour, schedFile);
	fputs(" * * * DISPLAY=:0.0 /usr/bin/rhythmbox-client --play-uri=", schedFile);
	fputs(musicAddress, schedFile);
	fputs("\n",schedFile);
	fclose(schedFile);
	
	//open a file named sched_job.txt and write the proper command to use in crontab
	
	char* argvs[3];
	argvs[0] = "crontab";
	argvs[1] = "sched_job.txt";
	argvs[2] = NULL;
	//assign arguments to use execvp
	execvp("crontab", argvs);
}

int alnum_cmp(char* wordToCheck, char* word){
	for(int i=0; i<strlen(word); i++){
		if(tolower(wordToCheck[i])!=tolower(word[i])) return 1;
	}
	for(int i=strlen(word); i<strlen(wordToCheck); i++){
		if(isalnum(wordToCheck[i])!=0) return 1;
	}
	return 0;

}

void highlight(struct command_t *command) {
	
	// ansi color codes
	#define RESETCOLOR "\033[0m"
	#define R "\x1B[31m"
	#define G "\x1B[32m"
	#define B "\x1B[34m"
	// studying the original grep code, with modificaitons to get coloring functionality
	char line[100]; //char array to store line
	char *word = command->args[1]; 
	char *color = command->args[2]; //storing word and color parameters
	char *found;
	char *wordcheck; //creating char pointers to check word equalaities
	FILE *file = fopen(command->args[3], "r");
	printf("\nHighlighting the word '%s':\n", word);
	
	for(int i=0; i<strlen(word);i++){
		*(word+i)=tolower(*(word+i)); // tolower function to achieve case unsensitive comparison requirement
	}
	while (fgets(line, 100, file) != NULL) { // read line by line until end of file
		char* linecpy = malloc(100);
		strcpy(linecpy, line);
		for(int i=0; i<strlen(linecpy); i++) *(linecpy+i)=tolower(*(linecpy+i)); //making a copy of the line fully lowercase
		found = strstr(linecpy, word); //strstr to check whether the line contains 
		int flag = 0; // flag to indicate the word exists in the line.
		if(found!=NULL){
			wordcheck = strtok(linecpy, " ");
			while (wordcheck != NULL) {
				if (alnum_cmp(wordcheck, word) == 0) { // our original comparison function
					flag=1; // word found in the current line
					break;
				}
				wordcheck = strtok(NULL, " ");
			}
		}
		
		if (flag == 1) { // if block for when the word is found
			wordcheck = strtok(line, " ");
			while (wordcheck != NULL) {
				if (alnum_cmp(wordcheck, word) == 0) { // if words match
					if (strcmp(color, "r") == 0) { 		// if color = red
						if(strrchr(wordcheck,'\n')==NULL) printf(R "%s " RESETCOLOR, wordcheck); // preserving correct spacing
						else printf(R "%s" RESETCOLOR, wordcheck);
					}
					else if (strcmp(color, "g") == 0) { // if color = green
						if(strrchr(wordcheck,'\n')==NULL) printf(G "%s " RESETCOLOR, wordcheck); // preserving correct spacing
						else printf(G "%s" RESETCOLOR, wordcheck);
					}
					else if (strcmp(color, "b") == 0) { // if color = blue
						if(strrchr(wordcheck,'\n')==NULL) printf(B "%s " RESETCOLOR, wordcheck); // preserving correct spacing
						else printf(B "%s" RESETCOLOR, wordcheck);
					}
				}
				else {
					if(strrchr(wordcheck,'\n')==NULL) printf("%s ", wordcheck); // no match
					else printf("%s", wordcheck);
				}
			wordcheck = strtok(NULL, " "); // tokenizer skips to the next word
			}
		}
	}

}



void kdiff(struct command_t *command){
	int mode = 0; //-a mode is 0 -b mode is 1, default is -a
	char* filename1 = malloc(100);
	char* filename2 = malloc(100);
	if(command->arg_count==5){
		//if -a -b option is provided
		if(strcmp(command->args[1],"-b")==0) mode=1;
		filename1 = command->args[2];
		filename2 = command->args[3];
	}else if(command->arg_count==4){
		//if -a -b option is not provided
		filename1 = command->args[1];
		filename2 = command->args[2];
	}else return;
	
	FILE* file1 = fopen(filename1, "r");
	FILE* file2 = fopen(filename2, "r");
	
	if(mode==0){
		//text comparing mode
		char* ext1 = strrchr(filename1, '.');
		char* ext2 = strrchr(filename2, '.');
		
		if(ext1==NULL || ext2==NULL || strcmp(ext1,".txt")!=0 || strcmp(ext2,".txt")!=0){
			//at least one of them is not a text file
			printf("At least one of the files are not text files!\n");
			return;
		}
		char* line1 = malloc(100);
		char* line2 = malloc(100);
		int lineNumber = 0;
		int diffCount = 0;
		int flag1=0;
		int flag2=0;
		while(1){
			//read both files until both finishes
			lineNumber++;
			if(fgets(line1, 100, file1)==NULL)flag1=1;
			if(fgets(line2, 100, file2)==NULL)flag2=1;
			//if it finishes set the flag to 1 and break if two files are finished
			if(flag1 && flag2) break;			
			if(strcmp(line1,line2)!=0){
				//compare lines and if one of them is null (file is finished) set the line to a message which eases printing differences
				diffCount++;
				if(flag1) line1 = "This line does not exist.\n";
				if(flag2) line2 = "This line does not exist.\n";
				printf("%s: Line %d: %s",filename1, lineNumber, line1);
				printf("%s: Line %d: %s",filename2, lineNumber, line2);
			}
		}
		
		if(diffCount==0)printf("The two text files are identical\n");
		else printf("%d different lines found\n", diffCount);
	}
	
	else{
		fseek(file1, 0, SEEK_END);
		fseek(file2, 0, SEEK_END);
		long filelen1 = ftell(file1);
		long filelen2 = ftell(file2);
		//go to the end of files to see file is how many bytes
		rewind(file1);
		rewind(file2);
		//go to the start
		char *buffer1 = (char*) malloc((filelen1));
		char *buffer2 = (char*) malloc((filelen2));
		//create 2 buffers and then find minimum length of 2 files
		int min = 0;
		if(filelen1<filelen2)min=filelen1;
		else min = filelen2;
		
		int diffCount = 0;
		for(int i=0;i<min;i++){
			//go through bytes until one of the files are finished
			fread(buffer1+i, 1,1, file1);
			fread(buffer2+i, 1,1, file2);
			if(buffer1[i]!=buffer2[i])diffCount++;
			
		}
		//add the difference of number of bytes
		//if short one is the first one this is equivalent to filelen1+filelen2-2*filelen1=filelen2-filelen1
		//if short one is the second one this is equivalent to filelen1+filelen2-2*filelen2=filelen1-filelen2
		//if both are equal in lengh this is absolutely 0
		diffCount+=filelen1+filelen2-2*min;
		if(diffCount==0)printf("The two files are identical!\n");
		else printf("The two files are different in %d bytes!\n", diffCount);
	}

}

int compare_fn(char* element1, char* element2, char* dataType);

void bsort(struct command_t *command){
	char* dataType = command->args[1]; //can be -s (string) -i (integer) -f (float)
	char* mode = command->args[2]; // can be -a (ascending) -d (descending)
	char* filename = malloc(100);
	filename = command->args[3];
	char* sorted_filename = malloc(strlen(filename)+7);
	char* ext = strrchr(filename, '.');
	
	if(ext==NULL || strcmp(ext,".txt")!=0){
		printf("Input is not a text file!\n");
	}
	else{
		//we copied the filename to manipulate it without losing original name
		char* filenamecpy = malloc(strlen(filename));
		strcpy(filenamecpy,filename);
		char* name = strtok(filenamecpy,".");
		sorted_filename = strcat(strcat(name,"_sorted"),".txt");
		//filename_sorted.txt is the name of output file
	}
	FILE* file = fopen(filename, "r");
	char line[100];
	int elementNumber = 0;
	while(fgets(line, 100, file)){
		elementNumber++;
	}
	
	//first count the number of elements (rows) then take that rows as strings
	rewind(file);
	char fileArray[elementNumber][100];
	int curEl=0;
	while(fgets(fileArray[curEl], 100, file)){
		fileArray[curEl][strcspn(fileArray[curEl],"\n")]=0;
		curEl++;
	}
	
	char temp[100];
	
	//Classical implementation of bubble sort
	for(int i=0;i<elementNumber-1;i++){
		for(int j=0;j<elementNumber-i-1;j++){
			//good thing about bubble sort, we can treat all of the data types like strings 
			//by just differing compare method which we did in compare_fn
			if(strcmp(mode,"-d")==0){
				//descending order
				if(compare_fn(fileArray[j],fileArray[j+1], dataType)<0){
				strcpy(temp, fileArray[j]);
				strcpy(fileArray[j],fileArray[j+1]);
				strcpy(fileArray[j+1], temp);
				}
			}
			
			else{
				//ascending order
				if(compare_fn(fileArray[j],fileArray[j+1],dataType)>0){
				strcpy(temp, fileArray[j]);
				strcpy(fileArray[j],fileArray[j+1]);
				strcpy(fileArray[j+1], temp);
				}
			}

		}
	}
	
	//print output to file
	FILE* file2 = fopen(sorted_filename, "w");
	for(int i=0;i<elementNumber;i++){
		fputs(fileArray[i],file2);
		fputs("\n", file2);
	}
	fclose(file);
	fclose(file2);
}

int compare_fn(char* element1, char* element2, char* dataType){
	//it returns positive value if first element is greater than second, negative value when second is greater than first, 0 when they are equal
	//atoi and atof are used to convert strings to int and floats
	if(strcmp(dataType,"-s")==0){
		return strcmp(element1,element2);
	}else if(strcmp(dataType,"-i")==0){
		return atoi(element1)-atoi(element2);
	}else if(strcmp(dataType,"-f")==0){
		return atof(element1)-atof(element2);
	}
}
int process_command(struct command_t *command)
{

	int r;
	if (strcmp(command->name, "")==0) return SUCCESS;

	if (strcmp(command->name, "exit")==0)
		return EXIT;

	if (strcmp(command->name, "cd")==0)
	{
		if (command->arg_count > 0)
		{
			r=chdir(command->args[0]);
			if (r==-1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}
	
	//PART 2:
	//shortdir needs to change the directory of shell, so we kept it here instead of child process
	if(strcmp(command->name,"shortdir")==0){
		shortdir(command);
		return SUCCESS;
	}
			

	pid_t pid=fork();
	if (pid==0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
	    // extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		
		command->args=(char **)realloc(
			command->args, sizeof(char *)*(command->arg_count+=2));

		// shift everything forward by 1
		for (int i=command->arg_count-2;i>0;--i)
			command->args[i]=command->args[i-1];


		// set args[0] as a copy of name
		command->args[0]=strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count-1]=NULL;

		//execvp(command->name, command->args); // exec+args+path
		/// TODO: do your own exec with path resolving using execv()
		
		//PART 4:
		if(strcmp(command->name,"goodMorning")==0){
			goodMorning(command);
		}
		//PART 3:
		else if (strcmp(command->name, "highlight") == 0) {
			highlight(command);
		}
		
		//PART 5:
		else if(strcmp(command->name, "kdiff")==0){
			kdiff(command);
		}
		//PART 6:
		else if(strcmp(command->name, "bsort")==0){
			bsort(command);
		}
		
		else{
			//PART 1
			int exists;
			//getting environment variable "PATH" variable and delimiting it to paths
			char* pathVariable = getenv("PATH");
			char* pathTok = strtok(pathVariable, ":");
			char* pathToCheck;
			while(pathTok!=NULL){
				pathToCheck = strcat(strcat(strdup(pathTok), "/"), command->name);//look at path/command->name
				struct stat statStruct;
				exists = stat(pathToCheck,&statStruct);
				//if it exists break the loop
				if(exists == 0) break;
				pathTok = strtok(NULL, ":");
			}
			//using path we found and execv, do the system call
			execv(pathToCheck,command->args);
			//if it does not exist print the message
			if(exists!=0) printf("-%s: %s: command not found\n", sysname, command->name);
		}
		exit(0);
	}
	
	else
	{
		if (!command->background)
			wait(0); // wait for child process to finish
		return SUCCESS;
	}
	// TODO: your implementation here
	
	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}
