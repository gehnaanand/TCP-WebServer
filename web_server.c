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
#define DEFAULT_PORT 8888
#define DOCUMENT_ROOT "./www"
#define TIMEOUT_SECONDS 10

typedef struct {
    int sockfd;
    int keep_alive;
    struct timeval last_activity;
} ClientSocket;
 
void handle_get_request(ClientSocket client_sockets[], int client_socket, const char *request_uri, char *http_ver, int keep_alive);

void send_response(int client_socket, int status_code, char *http_ver, const char *content_type, const char *file_path, const char *connection_type);

void send_error_response(int client_socket, int status_code, char *http_ver, const char *message, int keep_alive);

int is_start_of_request(const char *buffer) {
    const char *http_methods[] = {"GET ", "POST ", "PUT ", "DELETE "};

    for (int i = 0; i < sizeof(http_methods) / sizeof(http_methods[0]); ++i) {
        if (strncmp(buffer, http_methods[i], strlen(http_methods[i])) == 0) {
            return 1; 
        }
    }
    
    return 0; 
}

void convert_to_lower_case(char *string) {
    int i;
    for(i = 0; string[i]; i++) {
        string[i] = tolower(string[i]);
    }
}

int find_socket(ClientSocket client_sockets[], int sockfd) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (sockfd == client_sockets[i].sockfd)
            return i;
    }
    return -1;
}

void close_client(ClientSocket client_sockets[], int client_socket) {
    int index = find_socket(client_sockets, client_socket);
    close(client_socket);
    client_sockets[index].sockfd = 0;
}

void print_client_sockets(ClientSocket client_sockets[]) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        printf("Index %d , socket = %d\n", i, client_sockets[i].sockfd);
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
    
    ClientSocket client_sockets[MAX_CLIENTS] = {[0 ... MAX_CLIENTS-1] = { .sockfd = 0, .keep_alive = 1 }};

    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    printf("Server socket - %d\n", server_socket);
    // Set socket options to allow multiple connections
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
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

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        max_socket = server_socket;

        // Add child sockets to set
        for (i = 0; i < MAX_CLIENTS; i++) {
            client_socket = client_sockets[i].sockfd;
            if (client_socket > 0)
                FD_SET(client_socket, &readfds);
            if (client_socket > max_socket)
                max_socket = client_socket;
        }

        // Wait for activity on any socket
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        activity = select(max_socket + 1, &readfds, NULL, NULL, &tv);
        if (activity < 0) {
            perror("Select error");
            exit(EXIT_FAILURE);
        }

        // If activity on server socket, it's an incoming connection
        if (FD_ISSET(server_socket, &readfds)) {
            if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len)) < 0) {
                perror("Accept failed");
                exit(EXIT_FAILURE);
            }
            printf("New connection, socket fd is %d, ip is : %d, port : %d\n",
                   client_socket, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            // Add new socket to array of client sockets
            int existing_sockfd_index = find_socket(client_sockets, client_socket);
            if (existing_sockfd_index == -1) {
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i].sockfd == 0) {
                        client_sockets[i].sockfd = client_socket;
                        gettimeofday(&client_sockets[i].last_activity, NULL);
                        client_sockets[i].keep_alive = 1;
                        break;
                    }
                }
            } else {
                client_sockets[existing_sockfd_index].sockfd = client_socket;
                gettimeofday(&client_sockets[existing_sockfd_index].last_activity, NULL);
                client_sockets[existing_sockfd_index].keep_alive = 1;
            }
            print_client_sockets(client_sockets);
        }

        // If activity on client sockets, it's a client request
        for (i = 0; i < MAX_CLIENTS; i++) {
            client_socket = client_sockets[i].sockfd;
            if (client_socket > 0) {
                if (FD_ISSET(client_socket, &readfds)) {
                    // Handle client request
                    // Read HTTP request to get request URI
                    char method[10];
                    int bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
                    if (bytes_received < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            printf("Timeout occurred\n");
                        } else {
                            perror("Recv failed");
                        }
                        // Error
                        printf("Closing socket - %d\n", client_socket);
                        close(client_socket);
                        client_sockets[i].sockfd = 0;
                    } else if (bytes_received == 0) {
                        printf("Connection closed by peer\n");
                        printf("Closing socket - %d\n", client_socket);
                        close(client_socket);
                        client_sockets[i].sockfd = 0;
                    } else {
                        buffer[bytes_received] = '\0';
                        memset(method, '\0', sizeof(method));
                        // printf("Received from client %s\n", buffer);
                        if (is_start_of_request(buffer) == 1) {
                            sscanf(buffer, "%s %s HTTP/%s", method, request_uri, http_ver);
                            // convert_to_lower_case(method);
                            printf("Method - %s\n", method);
                            // printf("Request Uri - %s\n", request_uri);
                            // printf("Http Ver - %s\n", http_ver);
                            convert_to_lower_case(buffer);
                            if (strstr(buffer, "connection: keep-alive") == NULL) {
                                printf("Keep Alive not present \n");
                                client_sockets[i].keep_alive = 0;
                            } else {
                                printf("Keep Alive present \n");
                                client_sockets[i].keep_alive = 1;
                            }
                            if (strcmp(method, "GET") != 0) {
                                printf("Method Not Allowed\n");
                                send_error_response(client_socket, 405, http_ver, "Method Not Allowed", client_sockets[i].keep_alive);
                            } else if (strcmp(http_ver, "1.0") != 0 && strcmp(http_ver, "1.1") != 0) {
                                printf("Http version invalid\n");
                                send_error_response(client_socket, 505, http_ver, "HTTP Version Not Supported", client_sockets[i].keep_alive);
                            } else {
                                handle_get_request(client_sockets, client_socket, request_uri, http_ver, client_sockets[i].keep_alive);
                            }
                            gettimeofday(&client_sockets[i].last_activity, NULL);
                            printf("Updating time of %d - %ld\n", client_sockets[i].sockfd, client_sockets[i].last_activity);
                        }
                    } 
                } 

                // Check for timeouts
                struct timeval current_time;
                gettimeofday(&current_time, NULL);
                if (client_sockets[i].keep_alive == 1) {
                    long elapsed_time = current_time.tv_sec - client_sockets[i].last_activity.tv_sec;
                    if (elapsed_time >= TIMEOUT_SECONDS) {
                        printf("Elapsed time - %ld\n", elapsed_time);
                        printf("Closing inactive connection %d \n", client_sockets[i].sockfd);
                        close(client_sockets[i].sockfd);
                        client_sockets[i].sockfd = 0;
                    }
                } else {
                    printf("Closing socket %d since keep-alive is not set\n", client_socket);
                    close(client_sockets[i].sockfd);
                    client_sockets[i].sockfd = 0;
                }
            }
        }

        // Check for timeouts
        // struct timeval current_time;
        // gettimeofday(&current_time, NULL);
        // for (int i = 0; i < MAX_CLIENTS; i++) {
        //     if (client_sockets[i].sockfd > 0 && client_sockets[i].keep_alive == 1) {
        //         double elapsed_time = difftime(current_time.tv_sec, client_sockets[i].last_activity.tv_sec);
        //         if (elapsed_time >= TIMEOUT_SECONDS) {
        //             printf("Elapsed time - %d\n", elapsed_time);
        //             printf("Closing inactive connection %d \n", client_sockets[i].sockfd);
        //             close(client_sockets[i].sockfd);
        //             client_sockets[i].sockfd = 0;
        //         }
        //     }
        // }
    }

    return 0;
}

void handle_get_request(ClientSocket client_sockets[], int client_socket, const char *request_uri, char *http_ver, int keep_alive) {
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
        send_error_response(client_socket, 404, http_ver, "Not Found", keep_alive);
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
            strcat(response_header, "Connection: keep-alive\r\n");
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
        // if (keep_alive) {  
            // printf("Keep-alive is set\n");
            // struct timeval tv;
            // tv.tv_sec = TIMEOUT_SECONDS;
            // tv.tv_usec = 0;
            // if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
            //     perror("Error setting socket option\n");
            //     // Handle the error appropriately, possibly by closing the socket
            //     close_client(client_sockets, client_socket);
            //     return;
            // }
        // } else {
        //     // Close the socket if Keep-Alive is not requested
        //     printf("Closing socket %d since keep-alive is not set\n", client_socket);
        //     close_client(client_sockets, client_socket);
        // }
    } else {
        if (errno == EACCES) {
            // File access error
            send_error_response(client_socket, 403, http_ver, "Forbidden", keep_alive);
        } else {
            // File not found
            send_error_response(client_socket, 404, http_ver, "Not Found", keep_alive);
        }
    }
}

void send_error_response(int client_socket, int status_code, char *http_ver, const char* status_message, int keep_alive) {
    // char response[1024];
    // sprintf(response, "HTTP/%s %d %s\r\n", http_ver, status_code, status_message);
    // strcat(response, "Content-Type: text/html\r\n");
    // strcat(response, "Content-Length: %ld\r\n", strlen(status_message));
    // strcat(response, "\r\n");
    // strcat(response, "<html><body><h1>");
    // strcat(response, status_message);
    // strcat(response, "</h1></body></html>");
    
    // send(client_socket, response, strlen(response), 0);
    char connection_type[10];
    if (keep_alive) {
        memcpy(connection_type, "keep-alive", strlen("keep-alive"));
    } else {
        memcpy(connection_type, "close", strlen("close"));
    }
    send_response(client_socket, status_code, http_ver, "text/html", status_message, connection_type);
}


void send_response(int client_socket, int status_code, char *http_ver, const char *content_type, const char *message, const char *connection_type) {
    char response[200];
    sprintf(response, "HTTP/%s %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: %s\r\n\r\n%s",
    http_ver, status_code, message, content_type, strlen(message), connection_type, message);
    send(client_socket, response, strlen(response), 0);
}
