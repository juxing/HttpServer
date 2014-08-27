#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <dlfcn.h>
#include <getopt.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <error.h>
#include <syslog.h>
#include <ctype.h>

#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/tcp.h>

#define QLEN 200
#define ISspace(x) isspace((int)(x))
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)

// Http server parameters
char ROOTDIR[255];
char MODDIR[255];
int PORT = 1089;
char LOGFILE[255];
char CONFIG[255];

// Http server log file
int fdlog;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Read one line from remote socket
int getLine(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while((i < size-1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        if(n > 0) {
            if(c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);
                if((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        }
        else 
            c = '\n';
    }
    buf[i] = '\0';

    return i;
}

int serveFile(int, const char*, int*, int);

// Get formated time string for headers
void getCurrentTimeHeader(char *timestr, int size) {
    
    time_t t;
    struct tm *nowtime;

    time(&t);
    nowtime = localtime(&t);
    //strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", nowtime);
    strftime(timestr, size, "%a, %e %b %Y %H:%M:%S", nowtime);
    
}

// If resource is not founded, return 404 not found to users
void notFound(int client) {
        char htmlbuf[1024];
        char resbuf[1024];
        char sendbuf[2048];

        sprintf(htmlbuf, "<html>\n<body>\n<center>\n<h2>404 Not Found<h2>\n</center>\n</body>\n</html>\n");
        int contLen = strlen(htmlbuf);
        char timestr[255];
        getCurrentTimeHeader(timestr, sizeof(timestr));
        //printf("%s\n", getCurrentTimeHeader);
        sprintf(resbuf, "HTTP/1.0 404 Not Found\r\nDate: %s\r\nContent-Length: %d\r\nContent-Type: text/html\r\n\r\n", timestr, contLen);
        strncpy(sendbuf, resbuf, strlen(resbuf)+1);
        strncat(sendbuf, htmlbuf, strlen(htmlbuf));

        if(send(client, sendbuf, strlen(sendbuf), 0) < 0)
            printf("notFound() send failed.\n");
        sleep(1);
}

// To serve mod request
int serveMod(int client, char *modPath, char *modArg, int *filesize, int nocontent) {

        void *handle;
        char*(*func)(char*);
        char *error;
        char modPathWh[1024];
        char *res = NULL;

        strncpy(modPathWh, MODDIR, strlen(MODDIR)+1);
        strncat(modPathWh, modPath, strlen(modPath));
        //printf("modpathwh: %s\n", modPathWh);

        handle = dlopen(modPathWh, RTLD_LAZY);
        if(!handle) {
            fputs(dlerror(), stderr);
            //exit(1);
            //printf("\nserveMod: dlopen failed.\n");
            notFound(client);
            //printf("serveMode: after notFound\n");
            //close(client);
            return 404;
        }

        func = dlsym(handle, "def_mod");
        if((error = dlerror()) != NULL) {
            fputs(error, stderr);
            //exit(1);
            notFound(client);
            dlclose(handle);
            //close(client);
            return 404;
        }

        res = func(modArg);
        
        char htmlbuf[1024];
        char resbuf[1024];
        char sendbuf[2048];

        sprintf(htmlbuf, "<html>\n<body>\n<center>\n<h2>%s<h2>\n</center>\n</body>\n</html>\n", res);
        int contLen = strlen(htmlbuf);
        char timestr[255];
        getCurrentTimeHeader(timestr, sizeof(timestr));
        //printf("%s\n", getCurrentTimeHeader);
        sprintf(resbuf, "HTTP/1.0 200 OK\r\nDate: %s\r\nContent-Length: %d\r\nContent-Type: text/html\r\n\r\n", timestr, contLen);
        strncpy(sendbuf, resbuf, strlen(resbuf)+1);
        if(nocontent == 0)
            strncat(sendbuf, htmlbuf, strlen(htmlbuf));

        int sendnum;
        if((sendnum = send(client, sendbuf, strlen(sendbuf), 0)) < 0) {
            printf("serveMod() send failed.\n");
            if(res != NULL)
                free(res);
            dlclose(handle);
            //close(client);
            return 404;
        }        
        sleep(1);
        //char test[255];
        //sprintf(test, "%d\n", sendnum);
        //write(fdlog, test, strlen(test));
        //writeLog(client, method, url, 200, 1234);

        *filesize = contLen;

        free(res);
        dlclose(handle);
        //close(client);

        return 200;
}


// After finished user request, write log int to log file
void writeLog(int client, const char *method, const char *url, int status, int bytes) {
    char remoteIP[128];
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);

    getpeername(client, (struct sockaddr*)&sa, &len);
    inet_ntop(AF_INET, &(sa.sin_addr), remoteIP, 128);
    
    time_t t;
    struct tm *nowtime;
    char timestr[255];

    time(&t);
    nowtime = localtime(&t);
    strftime(timestr, sizeof(timestr), "[%e/%b/%Y:%H:%M:%S]", nowtime);

    char logbuf[1024];
    sprintf(logbuf, "%s %s \"%s %s\" %d %d\n", remoteIP, timestr, method, url, status, bytes);
    pthread_mutex_lock(&mutex);
    if(write(fdlog, logbuf, strlen(logbuf)) < 0)
        printf("writeLog() write failed.\n");
    pthread_mutex_unlock(&mutex);
}

//  Process user request
void acceptRequest(int client) {

    char buf[1024];
    char method[255];
    char url[255];
    int numchars;
    size_t i, j;
    struct stat st;
    //char *queryString = NULL;

    if((numchars = getLine(client, buf, sizeof(buf))) <= 0) {
        close(client);
        return;
    }

    //printf("%s", buf);

    i = j = 0;
    while(!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    i = 0;
    while(ISspace(buf[j]) && (j < sizeof(buf)))
        j++;
    while(!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
        url[i] = buf[j];
        i++;
        j++;
    }
    url[i] = '\0';

    if(strlen(url) == 0)
        strncpy(url, "/", 2);

    if(strncmp(method, "GET", 3) && strncmp(method, "HEAD", 4)) {
        notFound(client);
        writeLog(client, method, url, 404, 0);
        close(client);
        return;
    }

    int nocontent = 0;
    if(!strncmp(method, "HEAD", 4))
        nocontent = 1;

    char *ptrBegin = NULL;
    char *ptrQues = NULL;
    char modArg[255];
    char modPath[255];

    if((ptrBegin = strstr(url, "/mod")) == url) {    // If user request for mod service
        if((ptrQues = strstr(url, "?")) > ptrBegin+4) {

            if(ptrQues-ptrBegin-4 > 251) {
                notFound(client);
                writeLog(client, method, url, 404, 0);
                close(client);
                return;
            }
            
            strncpy(modPath, url+4, ptrQues-ptrBegin-4);
            modPath[ptrQues-ptrBegin-4] = '\0';
            strncat(modPath, ".so", 3);
            //printf("searching mod: modpath: %s\n", modPath);
            if(*(ptrQues+1) != '\0') {
                strncpy(modArg, ptrQues+1, strlen(ptrQues+1)+1);
            }
            else 
                memset(modArg, 0, sizeof(modArg));
        }
        else {
            strncpy(modPath, url+4, strlen(url+4)+1);
            strcat(modPath, ".so");
            memset(modArg, 0, sizeof(modArg));
        }

        int status;
        int bytes;

        status = serveMod(client, modPath, modArg, &bytes, nocontent);
        writeLog(client, method, url, status, bytes);
        close(client);
    }

    else {    // If user request for file service
        char path[1024];    
        sprintf(path, "%s%s", ROOTDIR, url); //boundry?
        //printf("url: %s\n", path);
        if(path[strlen(path)-1] == '/')
            strcat(path, "index.html");
        //printf("path: %s\n", path);
        if(stat(path, &st) == -1) {
            /*while((numchars > 0) && strcmp("\n", buf))
                numchars = getLine(client, buf, sizeof(buf));*/
            notFound(client);
            writeLog(client, method, url, 404, 0);
            close(client);
            return;
        }
        else {
            if((st.st_mode & S_IFMT) == S_IFDIR)
                strcat(path, "/index.html");
            //printf("path: %s\n", path);

            int status;
            int bytes;
            status = serveFile(client, path, &bytes, nocontent);
            writeLog(client, method, url, status, bytes);
            close(client);
        }
    }

    //printf("log: %s\n", LOGFILE);
    
    /*char *time = getCurrentTime();
    write(fdlog, time, strlen(time));
    write(fdlog, buf, strlen(buf));*/

    //close(client);
    return;
}

// In file service, return headers to user based on file types
int headersFile(int client, const char *filename, int filesize, time_t modtime) {
    char buf[2048];
    
    char status[255];
    strcpy(status, "HTTP/1.0 200 OK\r\n");
    
    //char server[255];
    //strcpy(server, SERVER_STRING);

    int k = strlen(filename) - 1;
    while(filename[k] != '.')
        k--;

    char type[1024];
    if(strncmp(filename+k+1, "txt", 3) == 0)
        sprintf(type, "Content-Type: text/plain\r\n");
    else if(strncmp(filename+k+1, "htm", 3) == 0)
        sprintf(type, "Content-Type: text/html\r\n");
    else if(strncmp(filename+k+1, "html", 4) == 0)
        sprintf(type, "Content-Type: text/html\r\n");
    else if(strncmp(filename+k+1, "jpg", 3) == 0)
        sprintf(type, "Content-Type: image/jpeg\r\n");
    else if(strncmp(filename+k+1, "jpeg", 4) == 0)
        sprintf(type, "Content-Type: image/jpeg\r\n");
    else if(strncmp(filename+k+1, "gif", 3) == 0)
        sprintf(type, "Content-Type: image/gif\r\n");
    else if(strncmp(filename+k+1, "png", 3) == 0)
        sprintf(type, "Content-Type: image/png\r\n");
    else {
        return 1;
    }
    
    char timestr[255];
    getCurrentTimeHeader(timestr, sizeof(timestr));
    //printf("current: %s\n", timestr);

    char contLen[255];
    sprintf(contLen, "Content-Length: %d\r\n", filesize);

    char lastModTime[255];
    struct tm *nowtime;
    nowtime = localtime(&modtime);
    strftime(lastModTime, sizeof(lastModTime), "Last-Modified: %a, %e %b %Y %H:%M:%S\r\n", nowtime);
    //printf("%s\n", lastModTime);

    sprintf(buf, "%sDate: %s\r\n%s%s%s\r\n", status, timestr, lastModTime, contLen, type);

    if(send(client, buf, strlen(buf), 0) < 0)
        return 1;

    return 0;
}

// File service
int serveFile(int client, const char *filename, int *filesize, int nocontent) {
    /*int numchars = 1;
    char buf[1024];
    buf[0] = 'A';
    buf[1] = '\0';
    while((numchars > 0) && strcmp("\n", buf)) {
        numchars = getLine(client, buf, sizeof(buf));
    }*/

    int fp; 
    if((fp = open(filename, O_RDONLY)) < 0) {
        notFound(client);
        //close(client);
        return 404;
    }
    //printf("am i here\n");

    off_t offset = 0;
    struct stat filestat;
    stat(filename, &filestat);
    
    if(headersFile(client, filename, filestat.st_size, filestat.st_mtime) == 1) {
        notFound(client);
        //close(client);
        close(fp);
        return 404;
    }

    int sendsize;
    if(nocontent == 0)
        if((sendsize = sendfile(client, fp, &offset, filestat.st_size)) < 0) {
             notFound(client);
             close(fp);
             //close(client);
             return 404;
        }

    sleep(1);
    *filesize = filestat.st_size;
    close(fp);

    return 200;
}

void *threadTask(void *sock) {
    int conn = (int)sock;

    acceptRequest(conn);
    pthread_exit(NULL);
}

// Deamonize httpd service
void daemonize(const char *progname) {
    
    int i, fd0, fd1, fd2;
    pid_t pid;
    struct rlimit rl;
    //char buf[16];

    // Clear file creation mask.
    umask(0);

    // Get maximum number of file descriptors.
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
        fprintf(stderr, "%s: can't get file limit", progname);
        exit(1);
    }

    if ((pid = fork()) < 0) {
        fprintf(stderr, "%s: can't fork", progname);
        exit(1);
    } else if (pid != 0) { // parent must quit
        exit(0);
    }

    // child continues from here
    // become a session leader
    setsid();

    /*
     * Change the current working directory to the root so
     * we won't prevent file systems from being unmounted.
     */
    /*if (chdir("/") < 0) {
        fprintf(stderr, "%s: can't change directory to /");
        exit(1); 
    }*/

    // Close all open file descriptors.
    if (rl.rlim_max == RLIM_INFINITY) {
        rl.rlim_max = 1024;
    }

    for (i = 0; i < rl.rlim_max; i++) {
        close(i);
    }

    /*
     * Attach file descriptors 0, 1, and 2 to /dev/null.
     * All terminal output to user should be printed before this.
     */
    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);

    /*
     * Initialize the log file.
     */
    openlog(progname, LOG_CONS, LOG_DAEMON);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) {
        syslog(LOG_ERR, "unexpected file descriptors %d %d %d", fd0, fd1, fd2);
        exit(1);
    }

    fdlog = open(LOGFILE, O_RDWR|O_CREAT|O_APPEND, LOCKMODE);
    if (fdlog < 0) {
        syslog(LOG_ERR, "%s: can't open %s: %s", progname, LOGFILE, strerror(errno));
        exit(1);
    }

    if (lockf(fdlog,F_TLOCK,0) < 0) { // try to lock without getting blocked
        if (errno == EACCES || errno == EAGAIN) {
            syslog(LOG_ERR, "%s: can't lock %s: %s", progname, LOGFILE, strerror(errno));
            close(fdlog);
            exit(1);
        }
        syslog(LOG_ERR, "%s: can't lock %s: %s", progname, LOGFILE, strerror(errno));
        close(fdlog);
        exit(1);
    }
}

int main(int argc, char **argv) {

   getcwd(ROOTDIR, sizeof(ROOTDIR));
   strncat(ROOTDIR, "/www", 4);
   //printf("rootdir: %s\n", ROOTDIR);
   strncpy(MODDIR, ROOTDIR, strlen(ROOTDIR));
   strncat(MODDIR, "/mod", 4);
   //MODDIR[strlen(MODDIR)] = '\0';
   //printf("MODDIR: %s\n", MODDIR);
   getcwd(LOGFILE, sizeof(LOGFILE));
   strncat(LOGFILE, "/ihttpd.log", 11);
   //printf("LOGFILE: %s\n", LOGFILE); 

   char c;
   int optInx = 0;
   char *help = "Usage:\t./ihttpd [-r|--rootdir] rootdir [-m|--moddir] moddir\n\t[-p|--port] port [-l|--logfile] logfile\n\t[-c|--config] config [-h|--help]\n";
   int rootflag = 0;
   int modflag = 0;
   int portflag = 0;
   int logflag = 0;
   int confflag = 0;

   struct option longOpts[] = {
       {"rootdir", required_argument, NULL, 'r'},
       {"moddir", required_argument, NULL, 'm'},
       {"port", required_argument, NULL, 'p'},
       {"logfile", required_argument, NULL, 'l'},
       {"config", required_argument, NULL, 'c'},
       {"help", no_argument, NULL, 'h'},
       {0, 0, 0, 0}
   };

   while((c = getopt_long(argc, argv, "r:m:p:l:c:h", longOpts, &optInx)) != -1) {
       switch(c) {
           case 'r':
               if(strlen(optarg) > 254) {
                   printf("rootdir name is too long.\n");
                   exit(1);
               }
               strncpy(ROOTDIR, optarg, strlen(optarg)+1);
               if(ROOTDIR[strlen(ROOTDIR)-1] == '/')
                   ROOTDIR[strlen(ROOTDIR)-1] = '\0';
               rootflag = 1;
               break;
           case 'm':
               if(strlen(optarg) > 254) {
                   printf("moddir name is too long.\n");
                   exit(1);
               }
               strncpy(MODDIR, optarg, strlen(optarg)+1);
               if(MODDIR[strlen(MODDIR)-1] == '/')
                   MODDIR[strlen(MODDIR)-1] = '\0';
               modflag = 1;
               break;
           case 'p':
               PORT = atoi(optarg);
               if(PORT <= 0) {
                   printf("Invalid port.\n");
                   exit(1);
               }
               portflag = 1;
               break;
           case 'l':
               if(strlen(optarg) > 254) {
                   printf("logfile name is too long.\n");
                   exit(1);
               }
               strncpy(LOGFILE, optarg, strlen(optarg)+1);
               logflag = 1;
               break;
           case 'c':
               if(strlen(optarg) > 254) {
                   printf("config name is too long.\n");
                   exit(1);
               }
               strncpy(CONFIG, optarg, strlen(optarg)+1);
               confflag = 1;
               break;
           case 'h':
               printf("%s", help);
               return 0;
           default:
               printf("Invalid opts.\n");
               exit(1);
       }
   }

   if(strlen(CONFIG) > 0) {
       FILE *conffile;
       if(!(conffile = fopen(CONFIG, "r"))) {
           printf("Failed to open config file.\n");
           exit(1);
       }
       else {

           char confbuf[1024];

           while(fgets(confbuf, sizeof(confbuf), conffile)) {
               if(confbuf[0] == '#') {
                   continue;
               }

               if((strncmp(confbuf, "rootdir", 7) == 0) && (rootflag == 0)) {
                   char *tokremain = confbuf;
                   char *tok = strsep(&tokremain, "\042");
                   tok = strsep(&tokremain, "\042");
                   strncpy(ROOTDIR, tok, strlen(tok)+1);
                   if(ROOTDIR[strlen(ROOTDIR)-1] == '/')
                       ROOTDIR[strlen(ROOTDIR)-1] = '\0';
               }
               else if((strncmp(confbuf, "moddir", 6) == 0) && (modflag == 0)) {
                   char *tokremain = confbuf;
                   char *tok = strsep(&tokremain, "\042");
                   tok = strsep(&tokremain, "\042");
                   strncpy(MODDIR, tok, strlen(tok)+1);
                   if(MODDIR[strlen(MODDIR)-1] == '/')
                       MODDIR[strlen(MODDIR)-1] = '\0';
               }
               else if((strncmp(confbuf, "logfile", 7) == 0) && (logflag == 0)) {
                   char *tokremain = confbuf;
                   char *tok = strsep(&tokremain, "\042");
                   tok = strsep(&tokremain, "\042");
                   strncpy(LOGFILE, tok, strlen(tok)+1);
               }
               else if((strncmp(confbuf, "port", 4) == 0) && (portflag == 0)) {
                   char *tokremain = confbuf;
                   char *tok = strsep(&tokremain, "\042");
                   tok = strsep(&tokremain, "\042");
                   PORT = atoi(tok);
               }
           }
       }
   }

   printf("ROOTDIR: %s %d\n", ROOTDIR, rootflag);
   printf("MODDIR: %s %d\n", MODDIR, modflag);
   printf("LOGFILE: %s %d\n", LOGFILE, logflag);
   printf("CONFIG: %s %d\n", CONFIG, confflag);
   printf("PORT: %d %d\n", PORT, portflag);

   daemonize("ihttpd");

   //Lock the log file
   /*fdlog = open(LOGFILE, O_RDWR|O_CREAT|O_APPEND, LOCKMODE);
   if(fdlog < 0) {
       printf("Cannot open logfile.\n");
       exit(1);
   }   

   if(lockf(fdlog, F_TLOCK, 0) < 0) {
       if(errno == EACCES || errno == EAGAIN) {
           printf("Failed to lock file.\n");
           close(fdlog);
           exit(1);
       }
       printf("Failed to lock file for unknown reason.\n");
       close(fdlog);
       exit(1);
   }*/

   //Statr to http socket to accept requests
   //int n;
   int sock, conn;
   int val;
   struct sockaddr_in sin;
   //struct hostent *hen;
   //char buf[1024];

   struct sockaddr_in remote_addr;
   socklen_t addr_len;
   addr_len = sizeof(remote_addr);

   // create socket
   sock = socket ( AF_INET, SOCK_STREAM, 0 ) ;
   if ( sock < 0 ) {
      fprintf ( stderr, "Can't create socket: %s\n", strerror(errno) ) ;
      exit(1) ;
   }

   memset ( &sin, 0, sizeof(sin) ) ;
   sin.sin_family = AF_INET ;
   sin.sin_addr.s_addr = INADDR_ANY ;
   sin.sin_port = htons ( (short) PORT ) ;

   // try to bind to PORT
   val = bind ( sock, (struct sockaddr *)&sin, sizeof(sin) ) ;
   if ( val < 0 ) {
      fprintf ( stderr, "Can't bind socket: %s\n", strerror(errno) ) ;
      exit(1) ;
   }

   // we have to listen for TCP sockets 
   val = listen ( sock, QLEN ) ;
   if ( val < 0 ) {
      fprintf ( stderr, "Can't listen on socket: %s\n", strerror(errno) ) ;
      exit(1) ;
   }

   for ( ; ; ) {
      conn = accept ( sock, (struct sockaddr *)&remote_addr, &addr_len ) ;

      if (conn == -1) { // accept error
         if (errno == EINTR) { // caused by an interrupt
            continue; // try again
         } else {
            fprintf(stderr, "%s", strerror(errno));
            continue;
         }
      }

      /*char remotetest[20];
      inet_ntop(AF_INET, &(remote_addr.sin_addr), remotetest, 20);
      printf("%s\n", remotetest);
      struct sockaddr_in fortest;
      int size = sizeof(struct sockaddr_in);
      int wakaka = getpeername(conn, (struct sockaddr*)&fortest, &size);
      printf("conn: %d, getpeername: %d\n", conn, wakaka);*/

      char opt = 1;
      setsockopt(conn, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(char));

      pthread_t tid;
      int retval;
      
      retval = pthread_create(&tid, NULL, threadTask, (void*)conn);
      if(retval) {
          printf("Error from pthread_creat(), return value is %d.\n", retval);
          close(conn);
          continue;
      }
   }
}
