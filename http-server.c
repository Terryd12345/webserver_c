/*
** http-server.c
*/

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

void reset_game();
void reset_user(int sockfd);
void set_user(int sockfd);
void user_ready(int sockfd);
void print_details();
void check_win(int user, char *keyword);

// constants
static char const * const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_200_FORMAT_WITH_COOKIE = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Set-Cookie: id=%s\r\n\
Content-Length: %ld\r\n\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;

static int user1 = -1;
static char *user1_name = "";
static int user1_start = 0;
char user1_guesses[100][100];
int user1_guess_number = 0;

static int user2 = -1;
static char *user2_name = "";
static int user2_start = 0;
char user2_guesses[100][100];
int user2_guess_number = 0;
char *user1_current_guesses;
char *user2_current_guesses;

int gameover = 0;

static char *webpage;

// represents the types of method
typedef enum
{
    GET,
    POST,
    UNKNOWN
} METHOD;

static bool handle_http_request(int sockfd)
{
    // try to read the request
    char buff[2049];
    int n = read(sockfd, buff, 2049);
    if (n <= 0)
    {
        if (n < 0)
            perror("read");
        else
            printf("socket %d close the connection\n", sockfd);
        return false;
    }

    // terminate the string
    buff[n] = 0;

    char * curr = buff;

    // parse the method
    METHOD method = UNKNOWN;
    if (strncmp(curr, "GET ", 4) == 0)
    {
        curr += 4;
        method = GET;
    }
    else if (strncmp(curr, "POST ", 5) == 0)
    {
        curr += 5;
        method = POST;
    }
    else if (write(sockfd, HTTP_400, HTTP_400_LENGTH) < 0)
    {
        perror("write");
        return false;
    }

    // sanitise the URI
    while (*curr == '.' || *curr == '/'){
        ++curr;
    }
    
    if (strlen(curr) > 0) {
        
        if (method == GET)
        {
            
            if( strstr(buff, "start=Start") != NULL ){
                if( strcmp(webpage, "html/2_start.html") ){
                    user_ready(sockfd);
                }
            
                /* Handle resetting users when starting a new game */
                printf("gameover: %d\n", gameover);
                if(gameover == 0){
                    set_user(sockfd);
                } else {
                    printf("Restarting game, user1: %d, user2: %d\n", user1, user2);
                    gameover = 0;
                }                
                webpage = "html/3_first_turn.html";
            } else if( strstr(buff, "Cookie:") != NULL ) {
                webpage = "html/1_intro.html";
            } else {

                webpage = "html/1_intro.html";
            }
            
            
            
            // get the size of the file
            struct stat st;
            stat(webpage, &st);

            n = sprintf(buff, HTTP_200_FORMAT, st.st_size);
            // send the header first
            if (write(sockfd, buff, n) < 0)
            {
                perror("write");
                return false;
            }
            // send the file

            int filefd = open(webpage, O_RDONLY);
            do
            {
                n = sendfile(sockfd, filefd, NULL, 2048);
            }
            while (n > 0);
            if (n < 0)
            {
                perror("sendfile");
                close(filefd);
                return false;
            }
            close(filefd);
            
        }
        else if (method == POST)
        {
            char *username;
            int username_length;
            long added_length;
            long size;
            
            webpage = "html/2_start.html";   

            // get the size of the file
            struct stat st;

            if(strstr(buff, "quit=Quit") != NULL){
                webpage = "html/7_gameover.html";
                stat(webpage, &st);
                // Reset User
                reset_user(sockfd);

                n = sprintf(buff, HTTP_200_FORMAT, st.st_size);
                if (write(sockfd, buff, n) < 0)
                {
                    perror("write");
                    return false;
                }

                int filefd = open(webpage, O_RDONLY);
                n = read(filefd, buff, 2048);
                if (n < 0)
                {
                    perror("read");
                    close(filefd);
                    return false;
                }

                close(filefd);
                stat(webpage, &st);
                if (write(sockfd, buff, st.st_size) < 0)
                {
                    perror("write");
                    return false;
                }
            } else if(strstr(buff, "user=") != NULL){
                stat(webpage, &st);
                username = strstr(buff, "user=") + 5;
                
                //set_user(sockfd);

                username_length = strlen(username);
                added_length = username_length + 2;
                size = st.st_size + added_length;
                n = sprintf(buff, HTTP_200_FORMAT_WITH_COOKIE, username, size);
            }
            else if(strstr(buff, "guess=Guess") != NULL) {  
                
                char *keyword = strstr(buff, "keyword=")+8;
                int keyword_length = strlen(keyword);
                keyword[keyword_length-12] = '\0';

                if (sockfd == user1){            
                    /* if other player is ready then accept guesses */
                    if( user2_start == 1  ){
                        webpage = "html/4_accepted.html";
                        strcpy(user1_guesses[user1_guess_number], keyword);
                        user1_guess_number++;
                        check_win(1, keyword);            
                    } else if( gameover == 1 ){ 
                        webpage = "html/6_endgame.html"; 
                    } else {
                        webpage = "html/5_discarded.html";
                    }
                } else if(sockfd == user2){
                    if( user1_start == 1  ){
                        webpage = "html/4_accepted.html";
                        strcpy(user2_guesses[user2_guess_number], keyword);
                        user2_guess_number++;
                        check_win(2, keyword);
                    } else if( gameover == 1){    
                        webpage = "html/6_endgame.html"; 
                    } else {
                        webpage = "html/5_discarded.html";
                    }
                } else {
                    webpage = "html/6_endgame.html";
                }

                stat(webpage, &st);
                n = sprintf(buff, HTTP_200_FORMAT, st.st_size);

                if(sockfd == user1){
                    printf("I AM USER1");
                } else if( sockfd == user2 ){
                    printf("I AM USER2");
                }
                print_details();
                
            } else {
                printf("\n\n\nerror reading html...\n\n\n");
            }

            // send the header first
            if (write(sockfd, buff, n) < 0)
            {
                perror("write");
                return false;
            }
            // read the content of the HTML file
            int filefd = open(webpage, O_RDONLY);
            n = read(filefd, buff, 2048);
            
            if (n < 0)
            {
                perror("read");
                close(filefd);
                return false;
            }
            close(filefd);

            if( strcmp(webpage, "html/4_accepted.html") == 0 ){

                if(sockfd == user1){
                    for(int i=0; i<100; i++){
                        if( strlen(user1_guesses[i]) > 0 ){
                            //strncat(user1_current_guesses, user1_guesses[i], strlen(user1_guesses[i]));
                        }
                    }
                } else if(sockfd == user2){
                    for(int i=0; i<100; i++){
                        if( strlen(user2_guesses[i]) > 0 ){
                            //strncat(user2_current_guesses, user2_guesses[i], strlen(user2_guesses[i]));
                        }
                    }
                }
                
                printf("User1 guesses: %s, User2 guesses: %s\n", user1_current_guesses, user2_current_guesses);
                
            }

            if((strlen(username) > 0) && (strstr(buff, "user=") != NULL) ){
                // move the trailing part backward
                int p1, p2;
                for (p1 = size - 1, p2 = p1 - added_length; p1 >= size - 25; --p1, --p2)
                    buff[p1] = buff[p2];
                ++p2;
                // put the separator
                buff[p2++] = ' ';
                buff[p2++] = ' ';
                
                // copy the username
                strncpy(buff + p2, username, username_length);
                if (write(sockfd, buff, size) < 0)
                {
                    perror("write");
                    return false;
                }
            } else {
                if (write(sockfd, buff, st.st_size) < 0)
                {
                    perror("write");
                    return false;
                }
                
            }
        } 
        else {
            // never used, just for completeness
            fprintf(stderr, "no other methods supported");
        }
    // send 404
    } else if (write(sockfd, HTTP_404, HTTP_404_LENGTH) < 0)
    {
        perror("write");
        return false;
    }

    return true;
}

int main(int argc, char * argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s ip port\n", argv[0]);
        return 0;
    }

    // create TCP socket which only accept IPv4
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // reuse the socket if possible
    int const reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // create and initialise address we will listen on
    struct sockaddr_in serv_addr;
    bzero(&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    // if ip parameter is not specified
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // bind address to socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // listen on the socket
    listen(sockfd, 5);

    // initialise an active file descriptors set
    fd_set masterfds;
    FD_ZERO(&masterfds);
    FD_SET(sockfd, &masterfds);
    // record the maximum socket number
    int maxfd = sockfd;

    while (1)
    {
        // monitor file descriptors
        fd_set readfds = masterfds;
        if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0)
        {
            perror("select");
            exit(EXIT_FAILURE);
        }

        // loop all possible descriptor
        for (int i = 0; i <= maxfd; ++i) {
            // determine if the current file descriptor is active
            if (FD_ISSET(i, &readfds))
            {
                // create new socket if there is new incoming connection request
                if (i == sockfd)
                {
                    struct sockaddr_in cliaddr;
                    socklen_t clilen = sizeof(cliaddr);
                    int newsockfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
                    if (newsockfd < 0)
                        perror("accept");
                    else
                    {
                        // add the socket to the set
                        FD_SET(newsockfd, &masterfds);
                        // update the maximum tracker
                        if (newsockfd > maxfd)
                            maxfd = newsockfd;
                        // print out the IP and the socket number
                        char ip[INET_ADDRSTRLEN];
                        printf(
                            "new connection from %s on socket %d\n",
                            // convert to human readable string
                            inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, ip, INET_ADDRSTRLEN),
                            newsockfd
                        );
                    }
                }
                // a request is sent from the client
                else if (!handle_http_request(i))
                {
                    close(i);
                    FD_CLR(i, &masterfds);
                }
            }
        }
    }

    return 0;
}


void reset_game(){
    gameover = 1;
    user1 = -1;
    user2 = -1;
    user1_guess_number = 0;
    user2_guess_number = 0;
    user1_start = 0;
    user2_start = 0;

    for(int i=0; i<100; i++){
        memset(user1_guesses[i], '\0', 100);
        memset(user2_guesses[i], '\0', 100);
    }
    printf("Game reset\n");
}

void reset_user(int sockfd){
    if(sockfd == user1){
        user1 = -1;
        user1_start = 0;
        printf("User1 reset\n");
    } else if(sockfd == user2){
        user2 = -1;
        user2_start = 0;
        printf("User2 reset\n");
    } else {
        printf("Sockfd not set for some reason..\n\n");
    }
}

void set_user(int sockfd){
    if( sockfd == user1 ){
        // do nothing
        printf("User1 already set\n");
    } else if( sockfd == user2 ){
        // do nothing
        printf("User2 already set\n");
    } else if( (user1 == -1) && (sockfd != user2) ){
        user1 = sockfd;
        printf("User1 set to socket: %d\n", sockfd);
    } else if( (user2 == -1) && (sockfd != user1) ){
        user2 = sockfd;
        printf("User2 set to socket: %d\n", sockfd);
    }
}

void user_ready(int sockfd){
    if( (sockfd == user1) && (user1_start == 0)  ){
        user1_start = 1;
        printf("User1 ready\n");
    } else if( (sockfd == user2) && (user2_start == 0) ){
        user2_start = 1;
        printf("User2 ready\n");
    }
}

void check_win(int user, char *keyword){
    if(user == 1){
        for(int i=0; i<user2_guess_number; i++){
            if(strcmp(user2_guesses[i], keyword) == 0){
                reset_game(); 
                webpage = "html/6_endgame.html";
            }
        }
    } else if( user == 2){
        for(int i=0; i<user1_guess_number; i++){
            if(strcmp(user1_guesses[i], keyword) == 0){
                reset_game(); 
                webpage = "html/6_endgame.html";
            }
        }
    }
}

void print_details(){
    printf("User1: %d, User2: %d, User1_start: %d, User2_start: %d\n\n", user1, user2, user1_start, user2_start);
}
