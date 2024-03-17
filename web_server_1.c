#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/time.h>
#include <ctype.h>
#include <errno.h>

#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024
#define DEFAULT_PORT 8889
#define DOCUMENT_ROOT "./www"
#define TIMEOUT_SECONDS 10

void convert_to_lower_case(char *string) {
    int i;
    for(i = 0; string[i]; i++) {
        string[i] = tolower(string[i]);
    }
}

void send_response(int client_socket, int status_code, char *http_ver, const char *content_type, const char *message) {
    char response[200];
    sprintf(response, "HTTP/%s %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n%s", http_ver, status_code, message, content_type, strlen(message), message);
    send(client_socket, response, strlen(response), 0);
}

void send_error_response(int client_socket, int status_code, char *http_ver, const char* status_message) {
    send_response(client_socket, status_code, http_ver, "text/html", status_message);
}

void handle_get_request(int client_socket, const char *request_uri, char *http_ver, int keep_alive) {
    char file_path[100];
    char *content_type;
    char buffer[BUFFER_SIZE];

    if ((strcmp(request_uri, "/") == 0) || (strcmp(request_uri, "/inside/") == 0)) {
        sprintf(file_path, "%s%s", DOCUMENT_ROOT, "/index.html");
    } else {
        sprintf(file_path, "%s%s", DOCUMENT_ROOT, request_uri);
    }
    printf("File Path - %s - on socket %d\n", file_path, client_socket);

    // Determine content type based on file extension
    if (strstr(file_path, ".html") || strstr(file_path, ".htm"))
        content_type = "text/html";
    else if (strstr(file_path, ".txt"))
        content_type = "text/plain";
    else if (strstr(file_path, ".png"))
        content_type = "image/png";
    else if (strstr(file_path, ".gif"))
        content_type = "image/gif";
    else if (strstr(file_path, ".jpg") || strstr(file_path, ".jpeg"))
        content_type = "image/jpeg";
    else if (strstr(file_path, ".ico"))
        content_type = "image/x-icon";
    else if (strstr(file_path, ".css"))
        content_type = "text/css";
    else if (strstr(file_path, ".js"))
        content_type = "application/javascript";
    else {
        // Unsupported file type
        send_error_response(client_socket, 400, http_ver, "Bad Request");
        return;
    }

    // Open file and send response
    FILE *file = fopen(file_path, "rb");
    if (file) {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char response_header[200];
        sprintf(response_header, "HTTP/%s 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n", http_ver, content_type, file_size);
        // If Keep-Alive is requested, set Connection header accordingly
        if (keep_alive) {
            strcat(response_header, "Connection: Keep-alive\r\n");
        } else {
            strcat(response_header, "Connection: close\r\n");
        }
        strcat(response_header, "\r\n");
        send(client_socket, response_header, strlen(response_header), 0);

        // Read and send file contents
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
            send(client_socket, buffer, bytes_read, 0);
        }

        fclose(file);

        // Reset the timeout if Keep-Alive is requested
        if (keep_alive) {  
            printf("Keep-alive is set\n");
            // struct timeval tv;
            // tv.tv_sec = TIMEOUT_SECONDS;
            // tv.tv_usec = 0;
            // if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
            //     perror("Error setting socket option\n");
            //     // Handle the error appropriately, possibly by closing the socket
            //     close(client_socket);
            //     return;
            // }
        } else {
            // Close the socket if Keep-Alive is not requested
            printf("Closing socket %d since keep-alive is not set\n", client_socket);
            close(client_socket);
        }
    } else {
        if (errno == EACCES) {
            // File access error
            send_error_response(client_socket, 403, http_ver, "Forbidden");
        } else {
            // File not found
            send_error_response(client_socket, 404, http_ver, "Not Found");
        }
    }
}

int main(int argc, char *argv[]) {
    int server_socket, client_socket, max_socket, activity, i;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    fd_set readfds;
    int opt = 1;
    char buffer[BUFFER_SIZE];
    char request_uri[BUFFER_SIZE]; 
    char http_ver[16];
    struct timeval timeout;

    pid_t childpid;
    int n;
    
    // Parse command line arguments
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    // Create server socket
    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to allow multiple connections
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Bind the server socket
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_socket, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", port);

    while(1) {
        //accept a connection
        client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_addr_len);

        printf ("%s\n","Child created for dealing with client requests");

        if ((childpid = fork()) == 0) {
            //if it’s 0, it’s child process

            //close listening socket
            close(server_socket);

            // Child process handling client requests
            char method[10];
            int keep_alive = 0;
            n = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("Timeout occurred\n");
                } else {
                    perror("Recv failed");
                }
                // Error or connection closed by client
                printf("Closing socket - %d\n", client_socket);
                close(client_socket);
  
                // perror("Read error");
                // exit(EXIT_FAILURE);
            }
            buffer[n] = '\0';
            if (n > 0)  {
                // printf("%s \n", buffer);
                
                sscanf(buffer, "%s %s HTTP/%s", method, request_uri, http_ver);
                convert_to_lower_case(method);
                if (strcmp(method, "get") != 0) {
                    printf("Method Not Allowed\n");
                    send_error_response(client_socket, 405, http_ver, "Method Not Allowed");
                    continue;
                }
                if (strcmp(http_ver, "1.0") != 0 && strcmp(http_ver, "1.1") != 0) {
                    printf("Http version invalid\n");
                    send_error_response(client_socket, 505, http_ver, "HTTP Version Not Supported");
                    continue;
                }
                convert_to_lower_case(buffer);
                if (strstr(buffer, "connection: keep-alive") == NULL) {
                    printf("Keep Alive not present \n");
                    keep_alive = 0;
                } else {
                    printf("Keep Alive present \n");
                    keep_alive = 1;
                }

                handle_get_request(client_socket, request_uri, http_ver, keep_alive);

                if (keep_alive) {
                    struct timeval tv;
                    tv.tv_sec = TIMEOUT_SECONDS;
                    tv.tv_usec = 0;
                    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
                        perror("Error setting socket option\n");
                        // Handle the error appropriately, possibly by closing the socket
                        close(client_socket);
                    }
                } 
            }

            if (!keep_alive) {
                close(client_socket);
                exit(EXIT_SUCCESS); // Child process exits after handling the request
            }
        } else if (childpid < 0) {
            perror("Fork failed");
            exit(EXIT_FAILURE);
        }

        // Parent process closes client socket and continues to accept new connections
        close(client_socket);
    }

    return 0;
}
