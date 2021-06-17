#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <semaphore.h> 
#include <stdint.h>
#include <unistd.h>
#include <sys/time.h>
#include <string.h>

//command line variables
int N;
int q;
double p;
double t;
double b;

//threads, mutexes, condition variables
pthread_t moderator;
pthread_t* commentators;
pthread_t breaking_news_thread;
pthread_mutex_t breaking_news_mutex;
pthread_mutex_t mutex;
pthread_cond_t com;
pthread_cond_t* commentator_conds;
pthread_cond_t* nextQ;
pthread_cond_t mod;
pthread_cond_t break_cond;
pthread_cond_t break_cond2;

//flags
int gameover = 0;
int breaking_news = 0;
int turn = 0;
int comCount = 0;
int queueCount = 0;

//queue related variables
int* queue_array;
int back = -1;
int front = -1;

double startTime;

void comment();
void moderate();
void break_disc();

void enqueue(int id){
	if (front== - 1) front = 0;
	back++;
	queue_array[back] = id;
}

int dequeue(){
	if(front == - 1 || front > back) return -1;
	else{
		int id = queue_array[front];
		front++;
		return id;
	}
}

int pthread_sleepv2(double seconds){
    pthread_mutex_t mutex;
    if(pthread_mutex_init(&mutex,NULL)){
        return -1;
    }

    struct timeval tp;
    struct timespec timetoexpire;
    // When to expire is an absolute time, so get the current time and add
    // it to our delay time
    gettimeofday(&tp, NULL);
    long new_nsec = tp.tv_usec * 1000 + (seconds - (long)seconds) * 1e9;
    timetoexpire.tv_sec = tp.tv_sec + (long)seconds + (new_nsec / (long)1e9);
    timetoexpire.tv_nsec = new_nsec % (long)1e9;

    pthread_mutex_lock(&mutex);
    int res = pthread_cond_timedwait(&break_cond2, &mutex, &timetoexpire);//it sleeps until a breaking news occurs or time expires
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);

    //Upon successful completion, a value of zero shall be returned
    return res;
}

int pthread_sleep_breaking_news(){
	double seconds=5;
    pthread_mutex_t mutex;
    pthread_cond_t conditionvar;
    if(pthread_mutex_init(&mutex,NULL)){
        return -1;
    }
    if(pthread_cond_init(&conditionvar,NULL)){
        return -1;
    }

    struct timeval tp;
    struct timespec timetoexpire;
    // When to expire is an absolute time, so get the current time and add
    // it to our delay time
    gettimeofday(&tp, NULL);
    long new_nsec = tp.tv_usec * 1000 + (seconds - (long)seconds) * 1e9;
    timetoexpire.tv_sec = tp.tv_sec + (long)seconds + (new_nsec / (long)1e9);
    timetoexpire.tv_nsec = new_nsec % (long)1e9;

    pthread_mutex_lock(&mutex);
    int res = pthread_cond_timedwait(&conditionvar, &mutex, &timetoexpire);
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&conditionvar);

    //Upon successful completion, a value of zero shall be returned
    return res;
}

void get_inputs(int argc, char *argv[]){
	for(int i=1; i<argc; i+=2){
		if(strcmp(argv[i],"-n")==0) N = atoi(argv[i+1]);
		else if(strcmp(argv[i],"-q")==0) q = atoi(argv[i+1]);
		else if(strcmp(argv[i],"-p")==0) p = strtod(argv[i+1], NULL);
		else if(strcmp(argv[i],"-t")==0) t = strtod(argv[i+1], NULL);
		else if(strcmp(argv[i],"-b")==0) b = strtod(argv[i+1], NULL);
	}
}

void main(int argc, char *argv[]){
	get_inputs(argc, argv);
	
	srand(time(NULL));
	
	//initialization
	queue_array = malloc(sizeof(int)*(N*q+1));
	commentator_conds = malloc(sizeof(pthread_cond_t)*N);
	nextQ = malloc(sizeof(pthread_cond_t)*N);
	commentators = malloc(sizeof(pthread_t)*N);
	pthread_mutex_init(&mutex,NULL);
	pthread_mutex_init(&breaking_news_mutex,NULL);
	pthread_cond_init(&com,NULL);
	pthread_cond_init(&break_cond,NULL);
	pthread_cond_init(&break_cond2,NULL);
	pthread_cond_init(&mod,NULL);
	
	for(int i=0; i<N; i++){
		pthread_cond_init(&commentator_conds[i],NULL);
		pthread_cond_init(&nextQ[i],NULL);
	}
	
	pthread_create(&moderator, NULL, moderate, NULL);
	pthread_create(&breaking_news_thread, NULL, break_disc, NULL);
	for(int i=0; i<N; i++){
		pthread_create(&commentators[i], NULL, comment, (void*) i);
	}
	
	while(!gameover){
		if(!breaking_news){
			double prob = ((double)rand())/RAND_MAX;
			if(prob < b){
				breaking_news=1;
				pthread_cond_signal(&break_cond);		
			}	
		}
		pthread_sleepv2(1);//wait for 1 sec
	}
	
	pthread_join(moderator, NULL);
	for(int i=0; i<N; i++){
		pthread_cancel(commentators[i]);//we used cancel instead of join because it is possible that some threads may wait indefinitely
	}
	pthread_cancel(breaking_news_thread);
}



double getRelativeTime(){
	struct timeval time;
    gettimeofday(&time, NULL);
	double currentTime = (double)(time.tv_usec) /1000000 + (double)(time.tv_sec);
	return currentTime - startTime;
}

void printTime() {
	double timeElapsed = getRelativeTime();
	printf("[%02d:", (int) timeElapsed/60);
	printf("%02d.", (int) timeElapsed%60);
	double miliseconds = (timeElapsed - (int)timeElapsed)*1000;
	printf("%03d] ", (int) miliseconds);
}

void comment(void* id){
	while(gameover!=1) {
		pthread_mutex_lock(&mutex);
		if (turn==0) {
			//moderator's turn, wait for commentator turn
			pthread_cond_wait(&com, &mutex);
		}
		double prob = ((double)rand())/RAND_MAX;
		int idd = (int *) id; //rename for casting

		comCount++;
		if(prob<p){
			//generating answer
			printTime();
			enqueue(idd);
			printf("Commenentator #%d generates answer, position in queue: %d\n", idd, queueCount);
			queueCount++;
		}
		if(comCount==N){
			pthread_cond_signal(&mod);	
		}

		if(prob <p){
			//answering the question
			double timeToSpeak = 1 + (t-1)*((double)rand())/RAND_MAX;
			pthread_cond_wait(&commentator_conds[idd],&mutex);
			printTime();
			printf("Commentator #%d’s turn to speak for %f seconds\n", idd, timeToSpeak);
			pthread_sleepv2(timeToSpeak);
			printTime();
			if(breaking_news) printf("Commentator #%d is cut short due to a breaking news\n", idd);
			else printf("Commentator #%d finished speaking\n", idd);
			queueCount--;
			pthread_cond_signal(&mod);		
		}else{
			pthread_cond_wait(&nextQ[idd],&mutex);
			pthread_cond_signal(&mod);		
 		}
 		
		turn = 0;
		comCount--;

		if(comCount==0){
			//signal moderator when all commentators are done
			pthread_cond_signal(&mod);		
		}
		while(breaking_news){
			//busy wait
		}
		pthread_mutex_unlock(&mutex);
	}

}

void moderate(){
	//record the starting time
	struct timeval start;
    gettimeofday(&start, NULL);
	startTime = (double)(start.tv_usec) /1000000 + (double)(start.tv_sec);
	
	for(int i=1;i<=q;i++){
		pthread_mutex_lock(&mutex);
		if (turn==1) {
			//commentator's turn, wait for moderator turn
			pthread_cond_wait(&mod, &mutex);
		}
		while(breaking_news){
			//busy wait
		}
		printTime();
		printf("Moderator asked Question %d\n", i);
		turn = 1;
		pthread_cond_broadcast(&com); //wake all commentator threads up
		pthread_cond_wait(&mod,&mutex);
		while(breaking_news){
			//busy wait
		}
		int idToWake;
		int flag[N];
		
		for(int j=0; j<N; j++) flag[j]=1;
		
		while((idToWake=dequeue())!=-1){
			//give turn to commentators in queue one by one until the queue becomes empty
			flag[idToWake]=0;
			pthread_cond_signal(&commentator_conds[idToWake]);
			pthread_cond_wait(&mod,&mutex);
		}
		
		for(int j=0; j<N; j++){
			if(flag[j]){
				pthread_cond_signal(&nextQ[j]);//wake commentators that did not answer
				pthread_cond_wait(&mod,&mutex);

			}
		}
		pthread_mutex_unlock(&mutex);
	}

	gameover=1; //declare the program is ended
}

void break_disc(){
	while(!gameover){
		pthread_cond_wait(&break_cond, &breaking_news_mutex);
		pthread_cond_signal(&break_cond2);
		printTime();
		printf("Breaking news!\n");
		pthread_sleep_breaking_news();
		printTime();
		printf("Breaking news ends!\n");
		breaking_news=0;
		pthread_mutex_unlock(&mutex);
		pthread_mutex_unlock(&breaking_news_mutex);
	}
}

