#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <direct.h>

#define SERVER_IP "192.168.2.2"
#define PORT 8080
#define BUFFER_SIZE 4096
#define TIMEOUT_SECONDS 5

void handle_error(const char*);

int32_t receive_with_timeout(SOCKET, char*, int32_t, int32_t);

void download_file(SOCKET, const char*);

void send_file(SOCKET, const char*);

int32_t main(void) {
    WSADATA wsaData;
    SOCKET client_socket;
    struct sockaddr_in server_addr;
    char buffer[BUFFER_SIZE];
	
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        handle_error("[-] WSAStartup failed");
        return 1;
    }

    if ((client_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        handle_error("[-] Socket creation failed");
        WSACleanup();
        return 1;
    }
	
	#ifndef SERVER_IP
		char SERVER_IP[BUFFER_SIZE];
		printf("Give server IPV4 address: ");
		scanf("%s", SERVER_IP);
		getchar();
	#endif
	
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(PORT);

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        handle_error("[-] Connection failed");
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    printf("[+] Connected to the server\n");

    while (true) {
        printf("Enter command (or 'exit' to quit): ");
        fgets(buffer, sizeof(buffer), stdin);

        if (strncmp(buffer, "download", 8) == 0) {
            char filename[BUFFER_SIZE];
            sscanf(buffer, "download %[^\n]", filename);
            download_file(client_socket, filename);
			continue;
		} else if (strncmp(buffer, "send", 4) == 0) {
            char filename[BUFFER_SIZE];
            sscanf(buffer, "send %[^\n]", filename);
            send_file(client_socket, filename);
			continue;	
		} else {
            send(client_socket, buffer, strlen(buffer), 0);
        }

        if (strcmp(buffer, "exit\n") == 0) {
            break;
        }

        int32_t message_length;

        if (recv(client_socket, (char*)&message_length, sizeof(message_length), 0) <= 0) {
            printf("[-] Error receiving message length from server.\n");
            continue;
        }

        // Convert message_length from network byte order to host byte order
        message_length = ntohl(message_length);

        // Allocate memory for the received message
        char* received_buffer = (char*)malloc(message_length + 1);
        if (received_buffer == NULL) {
            perror("[-] Memory allocation failed");
            exit(EXIT_FAILURE);
        }

        if (receive_with_timeout(client_socket, received_buffer, message_length, TIMEOUT_SECONDS) <= 0) {
            printf("[-] Timeout reached while receiving message.\n");
            free(received_buffer);
            continue;
        }

        received_buffer[message_length] = '\0';  
        printf("%s", received_buffer);

        free(received_buffer);

    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}

void handle_error(const char* error_message) {
    fprintf(stderr, "[-] %s\n", error_message);
}

int32_t receive_with_timeout(SOCKET socket, char* buffer, int32_t buffer_size, int32_t timeout_seconds) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int32_t result = select(0, &read_fds, NULL, NULL, &timeout);
    if (result == SOCKET_ERROR) {
        return SOCKET_ERROR;
    } else if (result == 0) {
        return 0; // Timeout reached
    }

    int32_t received_bytes = recv(socket, buffer, buffer_size, 0);
    return received_bytes;
}

void download_file(SOCKET client_socket, const char* filename) {
    char buffer[BUFFER_SIZE];
    uint32_t file_length;

    sprintf(buffer, "download %s", filename);
    send(client_socket, buffer, strlen(buffer), 0);

    if (receive_with_timeout(client_socket, (char*)&file_length, sizeof(file_length), TIMEOUT_SECONDS) <= 0) {
        printf("[-] Timeout reached. No response from the server.\n");
        return;
    }

    file_length = ntohl(file_length);
    FILE* file = fopen(filename, "wb");
    if (!file) {
        handle_error("[-] Error opening file");
        return;
    }

    uint32_t total_received_bytes = 0;
    int32_t received_bytes = 0;

    // Receive and display progress in percentage
    while (total_received_bytes < file_length) {
        received_bytes = receive_with_timeout(client_socket, buffer, sizeof(buffer), TIMEOUT_SECONDS);
        if (received_bytes <= 0) {
            printf("[-] Timeout reached. No response from the server.\n");
            fclose(file);
            remove(filename); // Remove the incomplete file
            return;
        }

        fwrite(buffer, 1, received_bytes, file);
        total_received_bytes += received_bytes;

        // Calculate and display progress percentage
        int32_t percentage = (int32_t)((double)total_received_bytes / file_length * 100);
        printf("Downloading... %d%%\r", percentage);
        fflush(stdout);
    }

    printf("[+] File '%s' downloaded successfully!\n", filename);

    fclose(file);
}

void send_file(SOCKET client_socket, const char* filename) {
    char buffer[BUFFER_SIZE];
    uint32_t bytes_read, totalBytesSent = 0;
	
    sprintf(buffer, "send %s", filename);
    send(client_socket, buffer, strlen(buffer), 0);
	
    FILE* file = fopen(filename, "rb");
    if (!file) {
        send(client_socket, "File not found.\n", 17, 0);
        return;
    }
    fseek(file, 0, SEEK_END);
    int64_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Send the length of the file to the client
    int32_t file_length = htonl(file_size);
    send(client_socket, (char*)&file_length, sizeof(file_length), 0);

    // Read and send the file in chunks
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(client_socket, buffer, bytes_read, 0) == -1) {
            perror("Error sending file data");
            fclose(file);
            return;
        }
        totalBytesSent += bytes_read;

        // Calculate and print the percentage
        int32_t percentage = (int32_t)((double)totalBytesSent / file_size * 100);
        printf("Sending... %d%%\r", percentage);
        fflush(stdout);
    }

    fclose(file);
    printf("[+] File '%s' sent successfully!\n", filename);
}
