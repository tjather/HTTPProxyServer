/*
 * proxy.c - A simple proxy server
 * usage: ./server <port> <timeout>
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/md5.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFSIZE 1024
#define LISTENQ 10 /*maximum number of client connections */

pthread_mutex_t cache_lock;
int timeout;

/*
 * errorHandler given the appropriate status codes when the HTTP request results in an error
 */
void errorHandler(int client_sock, char version[100], int status_code)
{
    char header[BUFSIZE];

    switch (status_code) {
    case 400:
        printf("400 Bad Request\n");
        snprintf(header, sizeof(header), "%s 400 Bad Request\r\nContent-Length: 0\r\n\r\n", version);
        break;
    case 403:
        printf("403 Forbidden\n");
        snprintf(header, sizeof(header), "%s 403 Forbidden\r\nContent-Length: 0\r\n\r\n", version);
        break;
    case 404:
        printf("404 Not Found\n");
        snprintf(header, sizeof(header), "%s 404 Not Found\r\nContent-Length: 0\r\n\r\n", version);
        break;
    case 405:
        printf("405 Method Not Allowed\n");
        snprintf(header, sizeof(header), "%s 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n", version);
        break;
    case 505:
        printf("505 HTTP Version Not Supported\n");
        snprintf(header, sizeof(header), "%s 505 HTTP Version Not Supported\r\nContent-Length: 0\r\n\r\n", version);
        break;
    default:
        status_code = 200;
        printf("200 OK\n");
        snprintf(header, sizeof(header), "%s 200 OK\r\nContent-Length: 0\r\n\r\n", version);
    }

    write(client_sock, header, strlen(header));
}

/*
 * calcualte_md5 hash given an input
 * implmentation adapted from
 * https://stackoverflow.com/questions/7627723/how-to-create-a-md5-hash-of-a-string-in-c
 */
void calcualte_md5(char* str, char* digest)
{
    unsigned char md5sum[BUFSIZE];
    MD5_CTX context;
    MD5_Init(&context);
    MD5_Update(&context, str, strlen(str));
    MD5_Final(md5sum, &context);

    for (int i = 0; i < 16; ++i) {
        sprintf(digest + i * 2, "%02x", (unsigned int)md5sum[i]);
    }
}

/*
 * check_cache check if hash exists in the local cache
 * return 0 if the cache doesn't exist or is expired and 1 if it is a valid cache
 */
int check_cache(char* file_name)
{
  struct stat file_stat;
  DIR* directory = opendir("./cache");

  //sizeof("cache") includes null terminator, but it will be replace with '/'
  char buffer[BUFSIZE];
  memset(buffer, 0, sizeof(buffer));
  strcpy(buffer, "cache/");
  strcat(buffer, file_name);

  if(directory){
    closedir(directory);
    if(stat(buffer, &file_stat) != 0){
      printf("File doesn't exist in the cache\n");
      return 0;
    }

    if(timeout == 0){
      return 1;
    }

    time_t last_modification = file_stat.st_mtime;
    time_t current_time = time(NULL);
    double seconds_elapsed = difftime(current_time, last_modification);
    if(seconds_elapsed > timeout){
      printf("Page has expired\n");
      return 0;
    }
    printf("File %s is yet to expire with age of %d seconds\n", file_name, (int)seconds_elapsed);
    return 1;
  } else {
        printf("Couldn't open cache folder\n");
        return 0;
    }
}

/*
 * present_cached_page presents a page to the client from the cache
 */
void present_cached_page(int client_sock, char* file_name)
{
    FILE* file = fopen(file_name, "rb");
    if (file == NULL) {
        printf("Couldn't open file %s.\n", file_name);
        return;
    }

    fseek(file, 0L, SEEK_END);

    int file_size = ftell(file);
    rewind(file);

    char file_buffer[file_size];
    fread(file_buffer, 1, file_size, file);
    write(client_sock, file_buffer, file_size);
}

/*
 * on_blocklist checkes if a given IP is on the blocklist
 * returns 0 if the IP is not found on the blocklist and 1 if it is on the blocklist
 */
int on_blocklist(char* host, char* ip)
{
    FILE* file;
    char line[BUFSIZE];
    char* newline;

    if (access("blocklist", F_OK) == -1) {
        printf("Blocklist not found\n");
        return 0;
    }

    file = fopen("blocklist", "r");
    while (fgets(line, sizeof(line), file)) {
        newline = strchr(line, '\n');

        if (newline != NULL) {
            *newline = '\0';
        }

        if (strstr(line, host) || strstr(line, ip)) {
            printf("Host on blocklist\n");
            return 1;
        }
    }

    fclose(file);
    printf("Host not on blocklist\n");
    return 0;
}

/*
 * requestHandler processes the request for a single thread that represents one connection
 * it parses the request and transmits the file with all of its contents
 */
void* requestHandler(void* socket_desc)
{
    int client_sock = *(int*)socket_desc;
    int sock_desc;
    int n; /* message byte size */
    char buffer[BUFSIZE];
    char* method = NULL;
    char* uri = NULL;
    char* version = NULL;
    char path[BUFSIZE];
    struct sockaddr_in serveraddr; /* server's addr */
    struct hostent* server;

    free(socket_desc);

    n = read(client_sock, buffer, BUFSIZE);

    // printf("%s", buffer);

    method = strtok(buffer, " "); // GET or POST
    uri = strtok(NULL, " ");
    version = strtok(NULL, "\r"); // HTTP/1.1 or HTTP/1.0

    if (uri == NULL || strlen(uri) == 0 || version == NULL) {
        errorHandler(client_sock, version, 400);
        close(client_sock);
        return NULL;
    }

    // printf("Request type: %s\nHostname: %s\nVersion: %s\n", method, uri, version);

    if (strcmp(method, "GET") != 0) {
        errorHandler(client_sock, version, 400);
        close(client_sock);
        return NULL;
    }

    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0) {
        errorHandler(client_sock, version, 505);
        close(client_sock);
        return NULL;
    }

    char* doubleSlash = strstr(uri, "//");
    if (doubleSlash != NULL) {
        strcpy(uri, doubleSlash + 2); // Remove the url scheme for gethostbyname
    }

    char* slash = strchr(uri, '/');
    if (slash == NULL || *(slash + 1) == '\0') {
        printf("Default page requested\n");
        strcpy(path, "index.html");
    } else {
        strcpy(path, slash + 1);
    }

    // printf("Host: %s\nFile: %s\n", uri, path);

    if (slash != NULL) {
        *slash = '\0'; // Remove subdirectory
    }

    char md5_input[BUFSIZE];
    char md5_output[BUFSIZE];

    strcpy(md5_input, uri);
    strcat(md5_input, "/");
    strcat(md5_input, path);
    memset(md5_output, 0, sizeof(md5_output));

    printf("Hashing %s\n", md5_input);
    calcualte_md5(md5_input, md5_output);

    char cache_buffer[BUFSIZE];
    strcpy(cache_buffer, "cache/");
    strcat(cache_buffer, md5_output);

    if (check_cache(md5_output)) {
        present_cached_page(client_sock, cache_buffer);
        close(client_sock);
        return NULL;
    }

    sock_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_desc < 0)
        perror("ERROR opening socket");

    /*
     * build the server's Internet address
     */
    bzero((char*)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(80); // PORT 80 to send a request over HTTP

    struct in_addr cache_address;
    // printf("Host: %s", uri);

    /*
     * gethostbyname: get the server's DNS entry
     */
    server = gethostbyname(uri);
    if (server == NULL) {
        fprintf(stderr, "ERROR, host %s doesn't exist\n", uri);
        errorHandler(client_sock, version, 404);
        close(client_sock);
        return NULL;
    }

    bcopy((char*)server->h_addr, (char*)&serveraddr.sin_addr.s_addr, server->h_length);

    char ip_buffer[BUFSIZE];
    if (inet_ntop(AF_INET, (char*)&serveraddr.sin_addr.s_addr, ip_buffer, (socklen_t)20) == NULL) {
        printf("ERROR in address conversion\n");
        close(client_sock);
        return NULL;
    }

    printf("Host: %s, IP: %s\n", uri, ip_buffer);
    if (on_blocklist(uri, ip_buffer)) {
        errorHandler(client_sock, version, 403);
        close(client_sock);
        return NULL;
    }

    int serverlen = sizeof(serveraddr);

    n = connect(sock_desc, (struct sockaddr*)&serveraddr, serverlen);
    if (n < 0) {
        printf("ERROR in connect\n");
        close(client_sock);
        return NULL;
    }

    memset(buffer, 0, BUFSIZE);
    sprintf(buffer, "GET /%s %s\r\nHost: %s\r\n\r\n", path, version, uri);

    n = write(sock_desc, buffer, sizeof(buffer));
    if (n < 0) {
        printf("ERROR in write\n");
        close(client_sock);
        return NULL;
    }

    int total_size = 0;
    memset(buffer, 0, sizeof(buffer));

    // printf("GET /%s %s\r\nHost: %s\r\n\r\n", path, version, uri);
    // printf("FILE : %s", cache_buffer);

    FILE* file;
    file = fopen(cache_buffer, "wb");

    pthread_mutex_lock(&cache_lock);
    while ((n = read(sock_desc, buffer, sizeof(buffer))) > 0) {
        if (n < 0) {
            printf("ERROR in read\n");
            close(client_sock);
            return NULL;
        }

        total_size += n;

        write(client_sock, buffer, n);
        fwrite(buffer, 1, n, file);
        memset(buffer, 0, sizeof(buffer));
    }
    pthread_mutex_unlock(&cache_lock);

    fclose(file);

    if (n == -1) {
        printf("Error in read:%d\n", errno);
        close(client_sock);
        return NULL;
    }

    printf("Received a %d bytes\n", total_size);
    close(client_sock);
    return NULL;
}

int main(int argc, char** argv)
{
    int socket_desc; /* socket */
    int portno; /* port to listen on */
    int client_sock; /* connected socket */
    int client_len; /* client size */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */

    /*
     * check command line arguments
     */
    if (argc == 2) {
        timeout = 0;
    } else if (argc == 3) {
        timeout = atoi(argv[2]);
    } else {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    portno = atoi(argv[1]);
    // memset(cacheDNS, 0, sizeof(cacheDNS));

    /*
     * socket: create the parent socket
     */
    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc < 0)
        perror("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char*)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(socket_desc, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) < 0)
        perror("ERROR on binding");

    listen(socket_desc, LISTENQ);

    printf("Server running...waiting for connections.\n");

    if (pthread_mutex_init(&cache_lock, NULL) != 0) {
        perror("ERROR on pthread_mutex_init");
        return -1;
    }

    client_len = sizeof(struct sockaddr_in);

    while ((client_sock = accept(socket_desc, (struct sockaddr*)&clientaddr, (socklen_t*)&client_len))) {
        if (client_sock < 0) {
            perror("ERROR in accept");
            continue;
        }

        pthread_t thread_id;
        int* new_sock = malloc(sizeof(int));
        if (new_sock == NULL) {
            perror("Failed to allocate memory for socket");
            continue;
        }
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, requestHandler, (void*)new_sock) < 0) {
            perror("ERROR in pthread_create");
            continue;
        }
        pthread_detach(thread_id);
    }
    return 0;
}
