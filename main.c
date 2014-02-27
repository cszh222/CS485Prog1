#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() and read()*/
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/wait.h> 
#include <stdbool.h>
#include <time.h>

#define MAXBUFF 1000  /* Size of buffers */

void DieWithError(char *errorMessage);  /* Error handling function */
void handleClient(int clientfd); /*handleing connection with cliekt*/
int parseRequest(char *request, char *filename); /*parses the request line, returns the file name through file*/
void sendNotFound(int clientfd); /*sends 404 page*/
void sendFile(int clientfd, FILE *fileptr, char *filename);
void getMimeType(char *filename, char *mimeBuff);

char dirPath[MAXBUFF]; /*global for directory path, assuming 200 is enough*/

int main(int argc, char *argv[]) { 
    int portNo;

    if (argc != 3){   /* Test for correct number of arguments */
       fprintf(stderr, "Usage: <Port number> <Path to html root directory>\n");
       exit(1);
    }

    int index = 0;
    while(index != strlen(argv[1])) {//check that the port number passed does not contain non-numerics
       if(argv[1][index]<48 || argv[1][index]>57){
          fprintf(stderr, "Port number passed: %s is not valid\n", argv[1]);
          exit(1);
        }
       index+=1;
    }

    portNo = atoi(argv[1]);

    if(portNo<1024 || portNo>65535 ){//check port number within range
        fprintf(stderr, "Port number passed: %s is not between 1204-65535\n" ,argv[1]);
    }

    //get directory path
    strncpy(dirPath, argv[2], strlen(argv[2]));

    int servSock;                    /* Socket descriptor for server */
    int clntSock;                    /* Socket descriptor for client */
    struct sockaddr_in echoServAddr; /* Local address */
    struct sockaddr_in echoClntAddr; /* Client address */
    unsigned short echoServPort;     /* Server port */
    unsigned int clntLen;            /* Length of client address data structure */

    echoServPort = portNo ;         /* local port */

    /* Create socket for incoming connections */
    if ((servSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        DieWithError("socket() failed");
      
    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(echoServPort);      /* Local port */

    /* Bind to the local address */
    if (bind(servSock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");

    /* Mark the socket so it will listen for incoming connections */
    if (listen(servSock, 5) < 0)
        DieWithError("listen() failed");

    for (;;){ /* Run forever */
        /* Set the size of the in-out parameter */
        clntLen = sizeof(echoClntAddr);

        /* Wait for a client to connect */
        if ((clntSock = accept(servSock, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0)
            DieWithError("accept() failed");

        /* clntSock is connected to a client! */

        fprintf(stderr, "Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));
        handleClient(clntSock);

        pid_t pid = 1;
        while(pid > 0){
            pid = waitpid(-1, NULL, WNOHANG);
        }

    }
    exit(0);
}

void handleClient(int clientfd){

    if(fork() != 0){
        close(clientfd);
        return;
    }

    char readChar;
    char requestBuff[MAXBUFF];
    bzero(requestBuff, MAXBUFF);
    char requestFile[MAXBUFF];
    bzero(requestFile, MAXBUFF);
    char fullPath[MAXBUFF];
    bzero(fullPath, MAXBUFF);


    //timer for reading
    int readyToRead = 1;
    fd_set readFlag;
    FD_ZERO(&readFlag);
    FD_SET(clientfd, &readFlag);
    
    //time set to 10 sec so I can telnet initially	
    struct timeval waitTime;
    waitTime.tv_sec = 10;
    waitTime.tv_usec = 0;
    
    //read 1 char at a time until newline is found
    while(readyToRead > 0){
	if(recv(clientfd, &readChar, 1, MSG_DONTWAIT) == 1){
	    strncat(requestBuff, &readChar, 1);
        }
	if(readChar == '\n'){
	   break;
	}
        readyToRead = select(clientfd+1, &readFlag, NULL, NULL, &waitTime);
    }

    if(readyToRead <= 0){
        //timed out or input error
        fprintf(stderr,"Timeout or input error %d", readyToRead);
        close(clientfd);
	exit(1);
    }

    fprintf(stderr, "%s", requestBuff);

    if (parseRequest(requestBuff, requestFile) != 0){
        //request method is not GET, ignore the request
        fprintf(stderr, "bad request: %s\n", requestBuff);
	    close(clientfd);
        exit(1);
    }
    
    //set time to 2 sec so it times out faster when reading from browser
    waitTime.tv_sec = 2;    	
    //read the rest of request
    while(readyToRead > 0){
       recv(clientfd, &readChar, 1, MSG_DONTWAIT);
       fprintf(stderr, "%c", readChar);
       readyToRead = select(clientfd+1, &readFlag, NULL, NULL, &waitTime); 
    }

    /*valid request has been recieved*/
    /*build the full path of the file*/
    strncpy(fullPath, dirPath, strlen(dirPath));
    strncat(fullPath, requestFile, strlen(requestFile));

    FILE *fileptr;
    fileptr = fopen(fullPath, "rb");
    if(fileptr != NULL){
        //read from file and write to client;
        sendFile(clientfd, fileptr, requestFile);    
        fclose(fileptr);
    }
    else{
         //file dont exist, send 404
        sendNotFound(clientfd);        
    }  
    fprintf(stderr, "Closing Connection\n\n");
    close(clientfd);     
    exit(0);
}

int parseRequest(char *request, char *filename){
   /*request is null*/
    if(request== NULL){
        return -1;
    }
    char *method = strtok(request, " ");
    char *uri = strtok(NULL, " ");
    /*check none of the above is null*/
    if(method == NULL || uri == NULL){
        return -1;
    }
    /*check if method is GET*/
    if (strncmp(method, "GET", 3)!=0){
        return 1;
    }

    strncpy(filename, uri, strlen(uri));
    /*appedn index.html if the request is / */
    if(strcmp(filename, "/") == 0){
        strncat(filename, "index.html", 10);
    }

    return 0;
}

void sendNotFound(int clientfd){
    char responseBody1[] = "<html><head><title>404 - Not Found</title></head>";
    char responseBody2[] = "<body><h1>404 - Not found</h1>";
    char responseBody3[] = "<p>The server could not find the file you were requesting</p></body></html>";
    
    char statusHdr[] = "HTTP/1.0 404 NOT FOUND\r\n";
    char contentTypeHdr[] = "Content-Type: text/html\r\n";
    char contentLengthHdr[MAXBUFF];
    bzero(contentLengthHdr, MAXBUFF);


    sprintf(contentLengthHdr, "Content-Length: %d\r\n", 
        (int)(strlen(responseBody1)+strlen(responseBody2)+strlen(responseBody3)));

    /*write the headers*/
    send(clientfd, statusHdr, strlen(statusHdr), 0);
    send(clientfd, contentTypeHdr, strlen(contentTypeHdr), 0);
    send(clientfd, contentLengthHdr, strlen(contentLengthHdr), 0);
    send(clientfd, "\r\n", strlen("\r\n"), 0);

    /*write the body*/
    send(clientfd, responseBody1, strlen(responseBody1), 0);
    send(clientfd, responseBody2, strlen(responseBody2), 0);
    send(clientfd, responseBody3, strlen(responseBody3), 0);

    fprintf(stderr, "not found sent\n");
}

void sendFile(int clientfd, FILE *fileptr, char *filename){
    char mimeType[MAXBUFF];
    bzero(mimeType, MAXBUFF);
    char statusHdr[] = "HTTP/1.0 200 OK\r\n";
    char contentTypeHdr[MAXBUFF]; 
    bzero(contentTypeHdr, MAXBUFF);
    char contentLengthHdr[MAXBUFF];
    bzero(contentLengthHdr, MAXBUFF);      
    char *readBuff = (char *)malloc(sizeof(char)*MAXBUFF);
    bzero((void *)readBuff, MAXBUFF);
    int readSize;
    int fsize;

    getMimeType(filename, mimeType);   
    sprintf(contentTypeHdr, "Content-Type: %s\r\n", mimeType);

    fseek(fileptr, 0L, SEEK_END);
    fsize = ftell(fileptr);
    fseek(fileptr, 0L, SEEK_SET);

    sprintf(contentLengthHdr, "Content-Length: %d\r\n", fsize);

    /*send all headers*/
    send(clientfd, statusHdr, strlen(statusHdr), 0);
    send(clientfd, contentTypeHdr, strlen(contentTypeHdr), 0);
    send(clientfd, contentLengthHdr, strlen(contentLengthHdr), 0);
    send(clientfd, "\r\n", strlen("\r\n"), 0);

    while((readSize = fread((void *)readBuff, 1, MAXBUFF, fileptr)) > 0){
        send(clientfd, (void *)readBuff, readSize, MSG_WAITALL);
        //bzero((void *)readBuff, MAXBUFF);
    }

    free(readBuff);    
}

void getMimeType(char *filename, char *mimeBuff){
    /*get the string after the . */
    strtok(filename, ".");
    char *fileExt = strtok(NULL, ".");

    if(strcmp(fileExt, "html") ==0){
        strcpy(mimeBuff, "text/html");
    }
    else if(strcmp(fileExt, "txt") ==0){
        strcpy(mimeBuff, "text/plain");
    }
    else if(strcmp(fileExt, "jpg") == 0 || strcmp(fileExt, "jpeg") == 0 ){
        strcpy(mimeBuff, "image/jpeg");
    }
    else if(strcmp(fileExt, "gif") == 0){
        strcpy(mimeBuff, "image/gif");
    }
    else{
        //everything else sent as binary
        strcpy(mimeBuff, "application/octet-stream");
    }
}
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}
