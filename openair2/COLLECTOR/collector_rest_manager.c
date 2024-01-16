#include "collector_rest_manager.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdarg.h>


#define PORT 9000
#define BUFFER_SIZE 1024

void *nr_collector_rest_listener(void *arg) {
    LOG_D(NR_RRC, "COLLECTOR: REST LISTENER IS CREATED!\n");

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Create a socket for the server
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Allow the socket to be reused immediately after closing
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("Set socket options failed");
        exit(EXIT_FAILURE);
    }

    // Bind the socket to a port and listen for incoming connections
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Loop indefinitely and wait for incoming requests
    while (oai_exit == 0) {
        LOG_D(NR_RRC, "COLLECTOR REST: Waiting for incoming requests...\n");

        // Accept the incoming connection and read the request
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }
        if(read(new_socket, buffer, BUFFER_SIZE) < 0){
            perror("Read failed\n");
            exit(EXIT_FAILURE);
        }
        
        
        char response[BUFFER_SIZE] = {0};
        handle_request(buffer, response, arg);

        send(new_socket, response, strlen(response), 0);
        close(new_socket);
    }
    return NULL;
}

void handle_request(char* request, char* response, ...)
{
    if (strncmp(request, "GET", 3) == 0) {
        LOG_D(NR_RRC, "COLLECTOR: GET Request has came to the collector!\n");
        sprintf(response, "%s", handle_get_request(request));
    } else if (strncmp(request, "POST", 4) == 0) {
        LOG_D(NR_RRC, "COLLECTOR: POST Request has came to the collector!\n");
        va_list rest_args;
        va_start(rest_args, response);
        void* request_arguments = va_arg(rest_args, void*);
        sprintf(response, "%s" ,handle_post_request(request, request_arguments));   
        va_end(rest_args);     
    } else {
        LOG_D(NR_RRC, "COLLECTOR: Bad Request has came to the collector!\n");
        sprintf(response, "HTTP/1.1 400 Bad Request\n\n");
    }
}

char* handle_get_request(char* request)
{
    if (strncmp(request, "GET /is_gnb_alive", 17) == 0)
        //TODO: batuhan.duyuler: check some processes are running or not.
        return "HTTP/1.1 200 Gnb Is Alive\n\n";

    return "HTTP/1.1 400 Bad Request\n\n";
}

char* handle_post_request(char* request, void* args)
{
    char *param = strstr(request, "time_interval=");
    if (param != NULL) {
        char *value = param + strlen("time_interval=");
        int time_interval = atoi(value);

        collector_rest_listener_args_t* timeIntervalArgs = (collector_rest_listener_args_t*)args;
        // Set the time interval
        LOG_D(NR_RRC, "COLLECTOR: Received time interval: %d seconds\n", time_interval);
        *(timeIntervalArgs->timeInterval) = time_interval;
        *(timeIntervalArgs->firstTimeCopyFlag) = true;
        *(timeIntervalArgs->intervalChangedFlag) = true;
        return "HTTP/1.1 200 Time Interval Changed\n\n";
    }
    return "HTTP/1.1 400 Bad Request\n\n";
}
