#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>

#define PORT 8080
#define BUFSIZE 1024
#define MAX_TOKENS 10
// Marker to indicate the end of the command
#define CMD_END_MARKER "END_CMD" 

// Function defination
int is_valid_extension(const char *filename);
void send_file(int sock, char *filename, char *destination_path);
void process_command(int sock, char *input);
void handle_ufile(int sock, char *tokens[]);
void handle_dfile(int sock, char *tokens[]);
void handle_rmfile(int sock, char *tokens[]);
void handle_dtar(int sock, char *tokens[]);
void handle_display(int sock, char *tokens[]);

int main() {
    int client_sock;
    struct sockaddr_in server_addr;
    char buffer[BUFSIZE];

    // Create a socket for the client
    client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sock < 0) {
        // Check if the socket creation failed ,Exit if socket creation fails
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure the server address
    // Set address family to Internet (IPv4)
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    // Zero out the rest of the structure
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    // Connect the client socket to the server
    if (connect(client_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Close the socket if connection fails
        perror("Connect failed");
        close(client_sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to the server\n");

    // Infinite loop to keep the client running
    while (1) {
        printf("client$ ");
        // Read the user's input
        fgets(buffer, sizeof(buffer), stdin);
        // Process the user's input
        process_command(client_sock, buffer);
    }

    close(client_sock);
    return 0;
}

// Function to check if the file has a valid extension
int is_valid_extension(const char *filename) {
    // Find the last occurrence of '.' in the filename
    const char *ext = strrchr(filename, '.');
    // If there's no '.' in the filename, it's invalid
    if (ext == NULL) {
        return 0;
    }
    // Check if the file extension is one of the valid types: .c, .pdf, or .txt
    return strcmp(ext, ".c") == 0 || strcmp(ext, ".pdf") == 0 || strcmp(ext, ".txt") == 0;
}

// Function to process the user's input and determine the appropriate action
void process_command(int sock, char *input) {
    // Array to hold the tokens (words) of the command
    char *tokens[MAX_TOKENS];
    int token_count = 0;

    // Tokenize the input string by splitting it into words
    char *token = strtok(input, " \n");
    while (token != NULL && token_count < MAX_TOKENS) {
        // Store the token in the array
        tokens[token_count++] = token;
        token = strtok(NULL, " \n");
    }

    // If no tokens were found(Empty input), return early
    if (token_count == 0) {
        return;
    }

    // Determine the command from the first token and call the appropriate handler function
    if (strcmp(tokens[0], "ufile") == 0) {
        // check token count for ufile
        if(token_count != 3){
            printf("ERROR: Invalid Synopsis for %s.\n",tokens[0]);
            return;
        }
        handle_ufile(sock, tokens);
    } else if (strcmp(tokens[0], "dfile") == 0) {
        // check token count for dfile
        if(token_count != 2){
            printf("ERROR: Invalid Synopsis for %s.\n",tokens[0]);
            return;
        }
        handle_dfile(sock, tokens);
    } else if (strcmp(tokens[0], "rmfile") == 0) {
        // check token count for rmfile
        if(token_count != 2){
            printf("ERROR: Invalid Synopsis for %s.\n",tokens[0]);
            return;
        }
        handle_rmfile(sock, tokens);
    } else if (strcmp(tokens[0], "dtar") == 0) {
        // check token count for dtar
        if(token_count != 2){
            printf("ERROR: Invalid Synopsis for %s.\n",tokens[0]);
            return;
        }
        handle_dtar(sock, tokens);
    } else if (strcmp(tokens[0], "display") == 0) {
        // check token count for display
        if(token_count != 2){
            printf("ERROR: Invalid Synopsis for %s.\n",tokens[0]);
            return;
        }
        handle_display(sock, tokens);
    } else {
        // handle invalid command
        printf("ERROR: Invalid command\n");
    }
}

// Handle ufile command (upload file to server
void handle_ufile(int sock, char *tokens[]) { 
    // Buffer for receiving server responses
    char buffer[BUFSIZE];

    // Check if the filename and destination path are provided
    if (!tokens[1] || !tokens[2]) {
        printf("Error: Missing filename or destination path.\n");
        return;
    }

    // Extract the filename
    char *filename = tokens[1];
    // Extract the destination path
    char *destination_path = tokens[2];

    // Check if the file has a valid extension
    if (!is_valid_extension(filename)) {
        printf("Error: Invalid file extension of %s.\n",filename);
        return;
    }
    // Check if the file exists
    else if (access(filename, F_OK) == -1) {
        printf("Error: File does not exist.\n");
        return;
    }
    // Check if the destination path starts with "~/smain/"
    else if (strncmp(destination_path, "~/smain", 7) != 0) {
        printf("Error: Destination path must start with '~/smain'\n");
        return;
    }else{
        // send the file to the server
        send_file(sock, filename, destination_path);

        // Receive and display the confirmation message
        ssize_t bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            printf("Server: %s\n", buffer);
        } else if (bytes_received == 0) {
            printf("Connection closed by server.\n");
            exit(EXIT_SUCCESS);
        } else {
            perror("Error receiving data");
        }
    }
}

// Handle dfile command (download file from server)
void handle_dfile(int sock, char *tokens[]) {
    // Check if the filename is provided
    if (!tokens[1]) {
        printf("Error: Missing filename for dfile.\n");
        return;
    }
    // Extract file name
    char *file_path = tokens[1];
    // create a command to send to server
    char command[BUFSIZE];
    snprintf(command, sizeof(command), "dfile %s", file_path);

    // Send the command to the server
    if (send(sock, command, strlen(command), 0) < 0) {
        perror("Send failed");
        return;
    }

    // Receive the file name
    char buff_name[BUFSIZE];
    ssize_t bytes_received = recv(sock, buff_name, sizeof(buff_name) - 1, 0);
    if (bytes_received < 0) {
        // if file name is empty.
        perror("Error receiving file name");
        return;
    }
    buff_name[bytes_received] = '\0';

    // Check if the first response is an error message and print appropriate message
    const char *error_prefix = "ERROR:";
    if (strncmp(buff_name, error_prefix, strlen(error_prefix)) == 0) {
        printf("Server: %s\n", buff_name);
        return;
    }

    // Create a file to save the received content
    FILE *fp = fopen(buff_name, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return;
    }

    // Receive the file content
    char buffer_content[BUFSIZE];
    // take value of end marker to terminate
    size_t marker_len = strlen(CMD_END_MARKER);
    // flage to indicate the download status
    int download_successful = 0;

    while ((bytes_received = recv(sock, buffer_content, sizeof(buffer_content), 0)) > 0) {
        // Check for end marker
        if (bytes_received >= marker_len && 
            memcmp(buffer_content + bytes_received - marker_len, CMD_END_MARKER, marker_len) == 0) {
            // Write up to the end marker and break the loop
            fwrite(buffer_content, 1, bytes_received - marker_len, fp);
            // set flag
            download_successful = 1;
            break;
        }
        
        // write all the byte received from server
        size_t written = fwrite(buffer_content, 1, bytes_received, fp);
        if (written < (size_t)bytes_received) {
            perror("Error writing to file");
            break;
        }
    }

    // if received byte is 0 then print Error
    if (bytes_received < 0) {
        perror("Error receiving file");
    }

    // close file descripter
    fclose(fp);

    // print msg to client based on download status
    if (download_successful) {
        printf("  Your file has been downloaded.\n");
    } else {
        printf("  Failed: Download interupted.!\n");
    }

}

// Handle rmfile command
void handle_rmfile(int sock, char *tokens[]) {
    char recv_buffer[BUFSIZE];
    char buffer[BUFSIZE];

    // Check if the filepath is provided
    if (!tokens[1]) {
        printf("Error: Missing filepath for rmfile.\n");
        return;
    }

    // extract file path
    char *file_path = tokens[1];

    // extract and check path , and print if it is invalid.
    char *token_path = tokens[1];
    if (strncmp(token_path, "~/smain/", 8) != 0) {
        printf("Error: Path must start with '~/smain/'\n");
        return;
    }

    // Make a copy of file_path for tokenization
    char file_path_copy[BUFSIZE];
    strncpy(file_path_copy, file_path, BUFSIZE - 1);
    file_path_copy[BUFSIZE - 1] = '\0';

    // Tokenize the file path to get the file name
    char *last_token = NULL;
    char *token = strtok(file_path_copy, "/");
    while (token != NULL) {
        last_token = token;
        token = strtok(NULL, "/");
    }
    // check extension is valid or not
    if (last_token == NULL || !is_valid_extension(last_token)) {
        printf("Error: Invalid file extension.\n");
        return;
    }

    // construct command and Send the rmfile command to the server
    snprintf(buffer, sizeof(buffer), "rmfile %s", file_path);
    send(sock, buffer, strlen(buffer) + 1, 0);

    // Receive and display the confirmation message based on received message
    ssize_t bytes_received = recv(sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
    if (bytes_received > 0) {
        recv_buffer[bytes_received] = '\0';
        printf("Server: %s\n", recv_buffer);
    } else if (bytes_received == 0) {
        printf("Connection closed by server.\n");
        exit(EXIT_SUCCESS);
    } else {
        perror("Error receiving response from server\n");
    }
}

// Handle dtar command
void handle_dtar(int sock, char *tokens[]) {
    // buffer to send to Smain
    char command[BUFSIZE];

    // Check if the file extension is provided
    if (!tokens[1]) {
        printf("Error: Missing extenion for dtar.\n");
        return;
    }
    
    // Store the extension provided by the user in a pointer variable
    char *ext = tokens[1];

    // Check if the provided extension is valid
    if(!is_valid_extension(ext)){
        printf("Error: Invalid file extension.\n");
        return;
    }

    // Form the dtar command using the provided file extension
    snprintf(command, sizeof(command), "dtar %s", tokens[1]);

    // Send the command to the server
    if (send(sock, command, strlen(command), 0) < 0) {
        perror("Failed to send command to server");
        return;
    }
    
    // Buffer to receive the tar file name from the server
    char buff_name[BUFSIZE];
    ssize_t bytes_received = recv(sock, buff_name, sizeof(buff_name) - 1, 0);
    if (bytes_received < 0) {
        // Check if the tar file name was received successfully
        perror("Error receiving file name");
        return;
    }
    buff_name[bytes_received] = '\0';
 
    // Check if the first response is an error message and print appropriate message
    const char *error_prefix = "ERROR:";
    if (strncmp(buff_name, error_prefix, strlen(error_prefix)) == 0) {
        printf("Server: %s\n", buff_name);
        return;
    }

    // Create a file to save the received content
    FILE *fp = fopen(buff_name, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return;
    }

    // Buffer to receive data
    char buffer_data[BUFSIZE];
    
    // Receive the data and write it to the file
    while ((bytes_received = recv(sock, buffer_data, sizeof(buffer_data), 0)) > 0) {
        // Write received data to file
        fwrite(buffer_data, 1, bytes_received, fp);

        // Check if the received data is the end marker indicating the end of transmission
        if (bytes_received < BUFSIZE) {
            // If the received data is less than the buffer size, check for the end marker
            buffer_data[bytes_received] = '\0'; // Null-terminate the buffer_data
            if (strstr(buffer_data, "END_CMD")) {
                break;
            }
        }
    }
    // Check if there was an error while receiving data from the server
    if (bytes_received < 0) {
        perror("Failed to receive data from server");
    }else{
        printf("File received and saved as %s\n", buff_name);
    }

    // Close the file
    fclose(fp);
}

// Handle display command
void handle_display(int sock, char *tokens[]) {  
    char command[BUFSIZE];
    char buffer[BUFSIZE];

    // Check if the pathname is provided and is valid or not
    if (tokens[1] == NULL) {
        printf("Invalid command: Pathname not provided.\n");
        return;
    } else if (strncmp(tokens[1], "~/smain", 7) != 0) {
        printf("Error: Destination path must start with '~/smain'\n");
        return;
    }

    // Form the display command with the given pathname
    snprintf(command, sizeof(command), "display %s", tokens[1]);

    // Send the command to the server
    if (send(sock, command, strlen(command), 0) < 0) {
        perror("Failed to send command to server");
        return;
    }

    // Receive the server's response containing the list of file names
    ssize_t bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received < 0) {
        perror("Error receiving data from server");
        return;
    }
    buffer[bytes_received] = '\0';  // Null-terminate the received data


    // Check if the response is an error message or not and print accordingly
    const char *error_prefix = "ERROR:";
    if (strncmp(buffer, error_prefix, strlen(error_prefix)) == 0) {
        printf("Server: %s\n", buffer);
        return;
    }else{
        // Print the list of file names received from the server
        printf("Server:\n%s\n", buffer);
    }
}


// Function to send a file to the server along with the command
void send_file(int sock, char *filename, char *destination_path) {
    // Buffer to hold file content during transmission
    char buffer[BUFSIZE];
    int file_fd;
    // Variable to store the number of bytes read from the file
    ssize_t bytes_read;
    // Variable to store the total size of the file
    size_t total_size;

    // Open the file
    file_fd = open(filename, O_RDONLY);
    if (file_fd < 0) {
        // Check if file opening failed
        perror("File open failed");
        return;
    }

    // Determine the size of the file by moving the file pointer to the end
    total_size = lseek(file_fd, 0, SEEK_END);
    // Reset the file pointer to the beginning
    lseek(file_fd, 0, SEEK_SET);

    // Allocate memory for the entire message
    char *message = malloc(total_size + BUFSIZE + strlen(CMD_END_MARKER) + 1);
    if (message == NULL) {
        // Check if memory allocation failed
        perror("Memory allocation failed");
        close(file_fd);
        return;
    }

    // Build the command string
    snprintf(message, total_size + BUFSIZE + strlen(CMD_END_MARKER) + 1, "ufile %s %s %s", filename, destination_path, CMD_END_MARKER);

    // Append the file content to the command string
    ssize_t message_len = strlen(message);
    while ((bytes_read = read(file_fd, buffer, BUFSIZE)) > 0) {
        // Copy the chunk into the message
        memcpy(message + message_len, buffer, bytes_read);
         // Update the total length of the message
        message_len += bytes_read;
    }

    // Send the entire message (command + file content) to the server
    send(sock, message, message_len, 0);

    // Clean up: free the allocated memory and close the file
    free(message);
    close(file_fd);
}