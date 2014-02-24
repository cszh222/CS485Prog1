#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() and read()*/
#include <fcntl.h>
#include <sys/types.h> 
#include <sys/wait.h> 


#define MAXBUFF 2048  /* Size of buffers */

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

    //setup sockets
    int listenfd = 0;
    struct sockaddr_in servAddr;

    if ((listenfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        DieWithError("socket() failed");
    }

    /* Construct the server address structure */
    memset(&servAddr, 0, sizeof(servAddr));     /* Zero out structure */
    servAddr.sin_family = AF_INET;             /* Internet address family */
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);   /* Server IP address */
    servAddr.sin_port = htons(portNo); /* Server port */

     /* Bind to the local address */
    if (bind(listenfd, (struct sockaddr *) &servAddr, sizeof(servAddr)) < 0){
        DieWithError("bind() failed");
    }

     /* Mark the socket so it will listen for incoming connections */
    if (listen(listenfd, 10) < 0){
        DieWithError("listen() failed");
    }

    struct sockaddr_in clientAddr;
    unsigned int clientLen;
    int clientfd = 0;
    while(1){
        clientLen = sizeof(clientAddr);
        /* Wait for a client to connect */
        if ((clientfd = accept(listenfd, (struct sockaddr *) &clientAddr, &clientLen)) < 0)
            DieWithError("accept() failed");

        /*fork a child process to handle request*/
        if(fork() != 0){
            close(clientfd);
        }else{
            handleClient(clientfd);
            exit(0);
        }
        /*if there a child process has ended, reap the child*/
        pid_t pid = 1;
        while(pid > 0){
            pid = waitpid(-1, NULL, WNOHANG);
        }
    }
    exit(0);
}

void handleClient(int clientfd){

    char readChar;
    char requestBuff[MAXBUFF];
    bzero(requestBuff, MAXBUFF);
    char requestFile[MAXBUFF];
    bzero(requestFile, MAXBUFF);

    //read 1 char at a time until newline is found
    recv(clientfd, &readChar, 1, 0);
    while(readChar != '\n'){
        strncat(requestBuff, &readChar, 1);
        recv(clientfd, &readChar, 1, 0);
    }

    if (parseRequest(requestBuff, requestFile) != 0){
        //request method is not GET, ignore the request
        close(clientfd);
        return;
    }

    /*valid request has been recieved*/
    /*build the full path of the file*/
    char fullPath[MAXBUFF];
    bzero(fullPath, MAXBUFF);
    strncpy(fullPath, dirPath, strlen(dirPath));
    strncat(fullPath, requestFile, strlen(requestFile));

    FILE *fileptr;
    if((fileptr = fopen(fullPath, "rb")) == NULL){
        //file dont exist, send 404
        sendNotFound(clientfd);
    }
    else{
        //read from file and write to client;
        sendFile(clientfd, fileptr, requestFile);
        fclose(fileptr);
    }  
    close(clientfd);  
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
        strlen(responseBody1)+strlen(responseBody2)+strlen(responseBody3));

    /*write the headers*/
    send(clientfd, statusHdr, strlen(statusHdr), 0);
    send(clientfd, contentTypeHdr, strlen(contentTypeHdr), 0);
    send(clientfd, contentLengthHdr, strlen(contentLengthHdr), 0);
    send(clientfd, "\r\n", 4, 0);

    /*write the body*/
    send(clientfd, responseBody1, strlen(responseBody1), 0);
    send(clientfd, responseBody2, strlen(responseBody2), 0);
    send(clientfd, responseBody3, strlen(responseBody3), 0);
}

void sendFile(int clientfd, FILE *fileptr, char *filename){
    /*get the content type*/
    char mimeType[MAXBUFF];
    getMimeType(filename, mimeType);

    /*get the content length*/
    fseek(fileptr, 0, SEEK_END);
    long fsize = ftell(fileptr);
    fprintf(stderr, "filesize %ld\n", fsize);
    fseek(fileptr, 0, SEEK_SET);

    char *fileBuf = (char*)malloc(fsize);
    fread(fileBuf, fsize, 1, fileptr);
    fprintf(stderr, "%s", fileBuf);
    
    /*form headers*/
    /*status*/
    char statusHdr[] = "HTTP/1.0 200 OK\r\n";
    /*content type*/
    char contentTypeHdr[MAXBUFF]; 
    bzero(contentTypeHdr, MAXBUFF);
    sprintf(contentTypeHdr, "Content-Type: %s\r\n", mimeType);
    /*content length*/
    char contentLengthHdr[MAXBUFF];
    bzero(contentLengthHdr, MAXBUFF);
    sprintf(contentLengthHdr, "Content-Length: %ld\r\n", fsize);

    /*send all headers*/
    send(clientfd, statusHdr, strlen(statusHdr), 0);
    send(clientfd, contentTypeHdr, strlen(contentTypeHdr), 0);
    send(clientfd, contentLengthHdr, strlen(contentLengthHdr), 0);
    send(clientfd, "\r\n", 4, 0);
    send(clientfd, fileBuf, fsize, 0);
    fflush(clientfd);
    free(fileBuf);
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
}
void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}