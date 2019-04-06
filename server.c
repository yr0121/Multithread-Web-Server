#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include "util.h"
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <limits.h>

#define MAX_THREADS 100
#define MAX_queue_len 100
#define MAX_CE 100
#define INVALID -1
#define BUFF_SIZE 1024

static int port;
static int num_dispatcher;
static int num_workers;
static int dynamic_flag = 0;
static int qlen;
static int cache_entries;

static char path[BUFF_SIZE];
static int error;

//variable to control FIFO operation
static int fstin = 0;
static int fstout = 0;

//number for counting in queue and cache
static int requestcount = 0;
static int cachecount = 0;

//vars for controling and counting in dynamic mood
static int to_decrease = 0;
static int workers = 0;
static int ar[BUFF_SIZE];
static int all = 0;

//condition variable
static pthread_cond_t norequest = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queuefull = PTHREAD_COND_INITIALIZER;

//locks for queue, cache and log
static pthread_mutex_t lockforqueue = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lockforcache = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lockforlog = PTHREAD_MUTEX_INITIALIZER;




/*
  THE CODE STRUCTURE GIVEN BELOW IS JUST A SUGESSTION. FEEL FREE TO MODIFY AS NEEDED
*/

// structs:
//every request in the queue has 2 attributes: fd and name
typedef struct request_queue {
   int fd;
   char filename[BUFF_SIZE];
} request_t;

//create an array for request queue
request_t q[MAX_queue_len];

//every cache entry has 3 attributes: the size of the content, the request newfilename
//the content of the file and the freqency of the file in cache
typedef struct cache_entry {
    int len;
    char *request;
    char *content;
    int timeforf;
} cache_entry_t;

//cache entry array
cache_entry_t c[MAX_CE];
//file stream for web_server_log file
FILE *out;

/* ******************************* Helper Function ********************************/
int decrease_worker(int dec){
  to_decrease = dec;
  while (to_decrease!=0 && workers>1){
    // printf("delete 1//////////%d\n",to_decrease);
  }
  printf("Deleted %d worker threads because server load decreased\n", dec);
  return 0;
}
/**********************************************************************************/




/* ************************************ Cache Code ********************************/

// Function to check whether the given request is present in cache
int getCacheIndex(char *request){
  //pthread_mutex_lock(&lockforcache);
  /// return the index if the request is present in the cache
  for (int i=0;i<cache_entries;i++) {
    //if the request name is null, return -1
    if(c[i].request!=NULL) {
      if (strcmp(c[i].request,request)==0) {
        return i;
      }
    }
  }
  //pthread_mutex_unlock(&lockforcache);
    return -1;

}

// Function to add the request and its file content into the cache
//LFU replacement policy
void addIntoCache(char *mybuf, char *memory , int memory_size){
  int insert;
  //set the min value to the max integer
  int min_value=INT_MAX;
  //go through the cache, if find empty slot, take the emplty slot index and break
  for (int i=0;i<cache_entries;i++) {
    if(c[i].request==NULL) {
      insert=i;
      break;
    }
    //if the frequency of a file is smaller than current min value, replace the
    //index with that position index
    if (c[i].timeforf<min_value) {
        insert=i;
    }
  }
  //free the reqeust and content to avoid memory leak
  if (c[insert].request!=NULL)
    free(c[insert].request);
  if (c[insert].content!=NULL)
    free(c[insert].content);
  //add all the information to the cache and increase frequency of that file
  c[insert].request = (char*)malloc(strlen(mybuf));
  c[insert].content = (char*)malloc(memory_size*sizeof(char)+1);
  strcpy(c[insert].request,mybuf);
  memcpy(c[insert].content,memory,memory_size);
  c[insert].timeforf++;
  c[insert].len =memory_size;
}
 //add chache dynamic version
void addIntoCache_dynamic(char *mybuf, char *memory , int memory_size){
  int insert;
  int min_value=INT_MAX;
  for (int i=0;i<cache_entries;i++) {
    if(c[i].request==NULL) {
      insert=i;
      break;
    }

    if (c[i].timeforf<min_value) {
        insert=i;
    }
  }
  if (c[insert].request!=NULL)
    free(c[insert].request);
  c[insert].request = (char*)malloc(strlen(mybuf));
  c[insert].content = (char*)malloc(memory_size*sizeof(char)+1);
  strcpy(c[insert].request,mybuf);
  memcpy(c[insert].content,memory,memory_size);
  c[insert].timeforf++;
  c[insert].len =memory_size;
}

// clear the memory allocated to the cache
void deleteCache(){
  // De-allocate/free the cache memory
}

// Function to initialize the cache
void initCache(){
  // Allocating memory and initializing the cache array
  for (int i=0;i<cache_entries;i++) {
    c[i].len=0;
    c[i].request=NULL;
    c[i].content=NULL;
    c[i].timeforf=0;
  }
}

// Function to open and read the file from the disk into the memory
// Add necessary arguments as needed
char* readFromDisk(char *filename) {
  // Open and read the contents of file given the request
  //find the size of the file
  int size;
  struct stat st;
  stat(filename,&st);
  size =st.st_size;
  FILE *fp;
  //if failed to open the file, return null
  if((fp=fopen(filename,"r"))==NULL) {
      return NULL;
    }
  //otherwise return the buffer that holds the content of the file
  char* returnbuf=(char*)malloc(sizeof(char)*size);
  fread(returnbuf,size,1,fp);
  fclose(fp);
  return returnbuf;

}

/**********************************************************************************/

/* ************************************ Utilities ********************************/
// Function to get the content type from the request
char* getContentType(char * mybuf) {
  // Should return the content type based on the file type in the request
    if (strstr(mybuf, ".htm") != NULL)
    {
        return "text/html";
    }
    if (strstr(mybuf, ".jpg") != NULL)
    {
        return "image/jpeg";
    }
    if (strstr(mybuf, ".gif") != NULL)
    {
        return "image/gif";
    }
    else
    {
        return "text/plain";
    }

}

// This function returns the current time in microseconds
long getCurrentTimeInMicro() {
  struct timeval curr_time;
  gettimeofday(&curr_time, NULL);
  return curr_time.tv_sec * 1000000 + curr_time.tv_usec;
}

/**********************************************************************************/

// Function to receive the request from the client and add to the queue
void * dispatch(void *arg) {

  while (1) {
    int fd;
    char filename[BUFF_SIZE];
    char newfilename[BUFF_SIZE];

    // Accept client connection
    fd = accept_connection();
    //if fail, print error information and continue
    if (get_request(fd,filename)<0)
    {
        continue;
        printf("this is a wrong mst, continue");
    }

    else
    {
      //lock
      pthread_mutex_lock(&lockforqueue);
      //conditiond to prevent adding request to request queue
      while(requestcount==MAX_queue_len)
      {
        pthread_cond_wait(&queuefull,&lockforqueue);
      }
      //if successfully get the request, add the number
      requestcount++;
      //take the fd and save it
      q[fstin].fd=fd;
      //take the filename and process the filename
      for(int i=0; i<sizeof(filename)-1;i++)
      {
        newfilename[i]=filename[i+1];
      }
      strcpy(q[fstin].filename,newfilename);
      fstin = (fstin+1)%qlen;
      //move to next add in position
      pthread_cond_broadcast(&norequest);
      //unclock
      pthread_mutex_unlock(&lockforqueue);

    }
   }
   return NULL;
}

/**********************************************************************************/

// Function to retrieve the request from the queue, process it and then return a result to the client
void * worker(void *arg) {
  //related variables
  char* returnbuf;
  int totalrequest=0;
  char * type;
  int thread_id=*((int *)arg);
  struct stat st;
  int flag;
  long stop_time;
  long time;

  request_t retrievenode;

  cache_entry_t cachenode;

   while (1) {


     pthread_mutex_lock(&lockforqueue);
     //if no request, wait for new request, do not pick up request
     while(requestcount==0) {
       pthread_cond_wait(&norequest,&lockforqueue);
     }
     //start record time
     long start_time=getCurrentTimeInMicro();
     //decrease the request number in queue
     requestcount--;
     //increase the request number which the worker deals
     totalrequest++;
     //store the retrived node to a variable
     retrievenode=q[fstout];
     //move to next available slot for retriving
     fstout=(fstout+1)%qlen;
     pthread_cond_broadcast(&queuefull);
     pthread_mutex_unlock(&lockforqueue);
     //lock the cache related operation
     pthread_mutex_lock(&lockforcache);
     //get cache index,if the file is found in request
     //return result and log
     int cache_index=getCacheIndex(retrievenode.filename);
     if (cache_index!=-1) {
       stop_time=getCurrentTimeInMicro();
       time=stop_time-start_time;
       return_result(retrievenode.fd,getContentType(retrievenode.filename),c[cache_index].content,c[cache_index].len);
       pthread_mutex_lock(&lockforlog);
       fprintf(stderr, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,c[cache_index].len,time,"HIT");
       fprintf(out, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,c[cache_index].len,time,"HIT");
       fflush(out);
       pthread_mutex_unlock(&lockforlog);
       //otherwise find it in the disk,return result,log and save the file
       //to cache
     }else {
       returnbuf=readFromDisk(retrievenode.filename);
       if(returnbuf!=NULL) {
         stat(retrievenode.filename,&st);
         int size =st.st_size;
         stop_time=getCurrentTimeInMicro();
         time=(stop_time-start_time);
         pthread_mutex_lock(&lockforlog);
         type=getContentType(retrievenode.filename);
         return_result(retrievenode.fd,type,returnbuf,size);
         fprintf(stderr, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,size,time,"MISS");
         fprintf(out, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,size,time,"MISS");
         fflush(out);
         addIntoCache(retrievenode.filename,returnbuf,size);
         free(returnbuf);
         //if the file is null, return error and record error message
       }else {
          stop_time=getCurrentTimeInMicro();
          time=(stop_time-start_time);
          flag=return_error(retrievenode.fd,"Hello");
          fprintf(stderr, "[%d][%d][%d][%s][%s][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,"Requested file not found.",time,"MISS");
          fprintf(out, "[%d][%d][%d][%s][%s][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,"Requested file not found.",time,"MISS");
          fflush(out);
        }
        pthread_mutex_unlock(&lockforlog);
      }

  if ((to_decrease>0)&&(workers>1)){
    to_decrease-=1;
    workers-=1;
    pthread_mutex_unlock(&lockforcache);
    pthread_exit(NULL);
  }
  pthread_mutex_unlock(&lockforcache);
}//end while
  return NULL;
}

//worker dynamic version
void * worker_dynamic(void *arg) {
  char* returnbuf;
  int totalrequest=0;
  char * type;
  int thread_id=*((int *)arg);
  struct stat st;
  int flag;
  long stop_time;
  long time;

  request_t retrievenode;

  cache_entry_t cachenode;

   while (1) {


     pthread_mutex_lock(&lockforqueue);
     while(requestcount==0) {
       pthread_cond_wait(&norequest,&lockforqueue);
     }
     long start_time=getCurrentTimeInMicro();
     requestcount--;
     totalrequest++;

     retrievenode=q[fstout];
     fstout=(fstout+1)%qlen;
     pthread_cond_broadcast(&queuefull);
     pthread_mutex_unlock(&lockforqueue);

     pthread_mutex_lock(&lockforcache);
     int cache_index=getCacheIndex(retrievenode.filename);
     if (cache_index!=-1) {
       stop_time=getCurrentTimeInMicro();
       time=stop_time-start_time;

       return_result(retrievenode.fd,getContentType(retrievenode.filename),c[cache_index].content,c[cache_index].len);
       pthread_mutex_lock(&lockforlog);
       fprintf(stderr, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,c[cache_index].len,time,"HIT");
       fprintf(out, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,c[cache_index].len,time,"HIT");
       fflush(out);
       pthread_mutex_unlock(&lockforlog);
     }else {
       returnbuf=readFromDisk(retrievenode.filename);
       if(returnbuf!=NULL) {
         stat(retrievenode.filename,&st);
         int size =st.st_size;
         stop_time=getCurrentTimeInMicro();
         time=(stop_time-start_time);
         pthread_mutex_lock(&lockforlog);
         type=getContentType(retrievenode.filename);
         // strcpy(type,getContentType(retrievenode.filename));
         return_result(retrievenode.fd,type,returnbuf,size);
         fprintf(stderr, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,size,time,"MISS");
         fprintf(out, "[%d][%d][%d][%s][%d][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,size,time,"MISS");
         fflush(out);
         addIntoCache_dynamic(retrievenode.filename,returnbuf,size);
         //free(returnbuf);
       }else {
          stop_time=getCurrentTimeInMicro();
          time=(stop_time-start_time);
          flag=return_error(retrievenode.fd,"Hello");
          fprintf(stderr, "[%d][%d][%d][%s][%s][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,"Requested file not found.",time,"MISS");
          fprintf(out, "[%d][%d][%d][%s][%s][%ld us][%s]\n", thread_id,totalrequest,retrievenode.fd,retrievenode.filename,"Requested file not found.",time,"MISS");
          fflush(out);
        }
        pthread_mutex_unlock(&lockforlog);
      }

  if ((to_decrease>0)&&(workers>1)){
    to_decrease-=1;
    workers-=1;
    pthread_mutex_unlock(&lockforcache);
    pthread_exit(NULL);
  }
  pthread_mutex_unlock(&lockforcache);
  // pthread_mutex_unlock(&lockforqueue);
}//end while
  return NULL;
}

/**********************************************************************************/
/* ************************ Dynamic Pool Code ***********************************/
// Extra Credit: This function implements the policy to change the worker thread pool dynamically
// depending on the number of requests
void * dynamic_pool_size_update() {

  pthread_t tidd[num_dispatcher];
  pthread_t tidw;

  workers = 1;
  int undo;
  int temp_w; // current workers number



  // create the first worker
  ar[0]=0;
  if (error = pthread_create(&tidw, NULL, &worker_dynamic , &ar[0])) {
    fprintf(stderr, "Failed to create worker:%s\n", strerror(error));
    exit(-1);
  }
  all +=1;

  // create dispatchers
  for(int i = 0; i < num_dispatcher; i++) {
    int a = i;
    if (error = pthread_create(&tidd[i], NULL, &dispatch, &a)) {
      fprintf(stderr, "Failed to create dispatcher %d:%s\n",i, strerror(error));
      exit(-1);
    } // end if
  } // end for

  // Increase / decrease dynamically based on your policy
  // dynamically create or kill worker threads based onthe server load
  printf("Starting server on port %d: %d disp, 1 work\n", port, num_dispatcher);
  while(1) {
    // printf("in while!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n" );
    undo = requestcount;
    temp_w = workers;
    if (undo > temp_w){ // to increase
      // printf("inc undo is %d\n", undo);
      // printf("inc workers is %d\n", temp_w);
      int inc = undo-temp_w;
      int realinc = 0;
      pthread_t tid[inc];
      for(int i = 0; (i < inc)&&(temp_w < MAX_THREADS-1); i++)
      {
        ar[all] = i + all;
        // printf("new idx is %d\n",ar[all] );
        if (pthread_create(&tid[i], NULL, &worker_dynamic, &ar[all])) {
          fprintf(stderr, "Failed to create worker");
        } // end if
        else {
          workers +=1;
          temp_w +=1;
          realinc +=1;
          all+=1;
        }
      } // end for
      printf("Created %d worker threads because server load increased\n", realinc);
    } // end increase
    else if (undo < temp_w && temp_w>1){ // to decrease
      // printf("dec undo is %d\n", undo);
      // printf("dec workers is %d\n", temp_w);
      int dec;
      if (undo == 0){
        dec = temp_w-1;
      } else if (undo>0){
        dec = workers - undo;
      }
      if(decrease_worker(dec)) {
        fprintf(stderr, "Failed to kill worker ");
      } // end if
    } // end decrease
    usleep(88);
  } // end while

  return NULL;
} // end func
/**********************************************************************************/

//main function
int main(int argc, char **argv) {

  // Error check on number of arguments
  if(argc != 8){
    printf("usage: %s port path num_dispatcher num_workers dynamic_flag queue_length cache_size\n", argv[0]);
    return -1;
  }

  // Perform error checks on the input arguments

  port = atoi(argv[1]);
  if (port<1025||port>65535) {
    perror("Invalid port number");
    exit(-1);
  }

  if(strlen(argv[2])>=BUFF_SIZE) {
    perror("Invalid path");
    exit(-1);
  }
  strcpy(path,argv[2]);

  num_dispatcher = atoi(argv[3]);
  if(num_dispatcher>MAX_THREADS||num_dispatcher<=0) {
    perror("dispatcher number exceed limit");
    exit(-1);
  }

  num_workers = atoi(argv[4]);
  if(num_workers>MAX_THREADS||num_workers<=0) {
    perror("worker number exceed limit");
    exit(-1);
  }

  dynamic_flag = atoi(argv[5]);
  if ((dynamic_flag!=0)&&(dynamic_flag!=1)) {
    perror("Invalid dynamic flag");
    exit(-1);
  }

  qlen=atoi(argv[6]);
  if(qlen>MAX_queue_len||qlen<=0) {
    perror("queue length exceed limit");
    exit(-1);
  }

  cache_entries=atoi(argv[7]);
  if(cache_entries>MAX_CE||cache_entries<=0) {
    perror("cache length exceed limit");
    exit(-1);
  }

  int w[MAX_THREADS];

  pthread_t tidd[num_dispatcher];

  pthread_t tidw[num_workers];

  init(port);

  // Change the current working directory to server root directory
  chdir(path);

  // Start the server and initialize cache
  initCache();

  out=fopen("web_server_log","w+");
  if (out==NULL) {
    perror("Failed to open file");
    exit(-1);
  }


  if (dynamic_flag == 1) { // enter dynamic mood
    dynamic_pool_size_update();
  }
  else { // enter static mood

    // Create dispatcher and worker threads
    for(int i=0; i< num_workers;i++) {
      w[i]=i;
      if (error = pthread_create(&tidw[i], NULL, &worker , &w[i])) {
         fprintf(stderr, "Failed to create worker %d:%s\n",i, strerror(error));
         exit(-1);
      }
    } // end for

    // Clean up
    for(int i=0; i< num_dispatcher;i++) {
      int a = i;
      if (error = pthread_create(&tidd[i], NULL, &dispatch, &a)) {
        fprintf(stderr, "Failed to create dispatcher %d:%s\n",i, strerror(error));
        exit(-1);
      }
    } // end for

    for(int i=0; i< num_dispatcher;i++) {
      if (error = pthread_join(tidd[i], NULL)) {
        fprintf(stderr, "Failed dispatcher %d join:%s\n", i, strerror(error));
        exit(-1);
      }
    }

    for(int i=0; i< num_workers;i++) {
      if (error =pthread_join(tidw[i], NULL)) {
        fprintf(stderr, "Failed workers %d join:%s\n", i, strerror(error));
        exit(-1);
      }
    } // end for
  } // end static

  return 0;
}
