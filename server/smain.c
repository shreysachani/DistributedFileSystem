#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <dirent.h>


#define PORT 8080
#define BUFSIZE 102400
#define CMD_END_MARKER "END_CMD"
#define TAR_FILE_PATH "c_files.tar"

// Function prototypes
void prcclient(int client_sock);
void handle_ufile(int client_sock, char *command, char *file_data);
void handle_dfile(int client_sock, char *command);
void handle_rmfile(int client_sock, char *command);
void handle_dtar(int client_sock, char *command);
void handle_display(int client_sock, char *command);
int connect_to_spdf();
int connect_to_stext();
void send_file_to_server(int server_sock, int client_sock, char *command, char *filename, char *destination_path, char *file_data);
int receive_and_save_file(int sock, char *destination_path, char *f_name, char *file_data);
void remove_file_from_server(int sock, int client_sock, char *command, char *destination_path);
void send_file_to_client(int client_sock, const char *file_path, const char *file_name);
int delete_file(const char *file_path);
void send_download_request(int server_sock, int client_sock, char *command, char *file_path);
void get_file_names_from_server(int (*connect_func)(), const char *message, const char *error_prefix, char *response_buffer, size_t buffer_size);
void c_tar_file(int client_sock, const char *path);
void request_tar_file(int server_sock, int client_sock, char *path);

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    pid_t child_pid;

    // Create a socket for the server
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        // If socket creation fails, print an error and exit the program
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure the server address
    // Use the Internet address family
    server_addr.sin_family = AF_INET;
    // Set the port number, converting it to network byte order
    server_addr.sin_port = htons(PORT);
    // Allow connections from any IP address
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // Zero out the rest of the struct
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    // Bind the socket to the specified port and address so it can listen for incoming connections
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // If binding fails, print an error and close the socket
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Set the server to listen for incoming connections, with a maximum of 10 queued connections
    if (listen(server_sock, 10) < 0) {
        // If listening fails, print an error and close the socket
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("Smain server is listening on port %d\n", PORT);

    while (1) {
        // Accept a connection from a client, saving their address and port information
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);
        if (client_sock < 0) {
            // If accepting the connection fails, print an error and continue to the next iteration
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Fork a child process to handle the client
        child_pid = fork();
        if (child_pid == 0) {
            // If this is the child process, close the server socket and handle the client's requests
            close(server_sock);  // Close the server socket in the child
            prcclient(client_sock);  // Handle communication with the client
            close(client_sock);  // Close the client socket
            exit(0);  // Exit the child process
        } else if (child_pid > 0) {
            // In the parent process
            close(client_sock);  // Close the client socket in the parent
            waitpid(-1, NULL, WNOHANG);  // Wait for child processes to terminate
        } else {
            perror("Fork failed");
        }
    }

    close(server_sock);  // Close the server socket
    return 0;
}

// Function to handle communication with a connected client
void prcclient(int client_sock) {
    char buffer[BUFSIZE];
    int bytes_read;

    // Read messages from the client
    while ((bytes_read = recv(client_sock, buffer, BUFSIZE, 0)) > 0) {
        // Null-terminate the received string to prevent buffer overflow
        buffer[bytes_read] = '\0';


        // Check if the received message contains file data after the command
        char *file_data = strstr(buffer, "END_CMD");
        if (file_data) {
            // If found, separate the command part from the file data
            *file_data = '\0';
            // Move the pointer past the "END_CMD" marker to get to the file data
            file_data += strlen("END_CMD");
        }
        
        // Determine which command the client sent and call the appropriate function to handle it
        if (strncmp(buffer, "ufile", 5) == 0) {
            // Handle the 'ufile' command, which uploads a file
            printf("File Upload request\n");
            handle_ufile(client_sock, buffer, file_data);
        } else if (strncmp(buffer, "dfile", 5) == 0) {
            // Handle the 'dfile' command, which downloads a file
            printf("File download request\n");
            handle_dfile(client_sock, buffer);
        } else if (strncmp(buffer, "rmfile", 6) == 0) {
            // Handle the 'rmfile' command, which removes a file
            printf("File remove request\n");
            handle_rmfile(client_sock, buffer);
        } else if (strncmp(buffer, "dtar", 4) == 0) {
            // Handle the 'dtar' command, which download file of given extension to Tar
            printf("TarFile download request\n");
            handle_dtar(client_sock, buffer);
        } else if (strncmp(buffer, "display", 7) == 0) {
            // Handle the 'display' command, which shows files in a directory
            printf("Display Files request\n");
            handle_display(client_sock, buffer);
        }
    }
}

// helper Function to check if the path is valid
int is_valid_path(const char *path) {
    // Check if the path starts with "smain"
    if (strncmp(path, "~/smain",7) != 0) {
        return 0; // Invalid path
    }
    return 1; // Valid path
}

// Function to handle 'ufile' command
void handle_ufile(int client_sock, char *command, char *file_data) {
    char filename[256], destination_path[256];
    int server_sock;
    char *f_name;

    // Extract filename and destination path from the command
    if (sscanf(command, "ufile %s %s", filename, destination_path) != 2) {
        // Notify the client that the file upload failed
        printf("Command parsing failed\n");
        send(client_sock, "File upload failed", 18, 0);
        return;
    }
    // extract file name if subdirectory is also given
    if(strstr(filename,"/") != NULL){
        // Extract the file name
        f_name = strrchr(filename, '/') + 1;
    }else{
        f_name = filename;
    }

    // Check if the file is a PDF
    if (strstr(filename, ".pdf") != NULL) {
        // Forward to Spdf server
        server_sock = connect_to_spdf();
        if (server_sock < 0) {
            printf("Failed to connect to Spdf server\n");
            // Notify the client that the file upload failed
            send(client_sock, "File upload failed", 18, 0);
            return;
        }
        // Send the file to the Spdf server
        send_file_to_server(server_sock, client_sock, "ufile", f_name, destination_path, file_data);
        close(server_sock);

    // Check if the file is a text file
    } else if (strstr(filename, ".txt") != NULL) {
        // Connect to the Stext server
        server_sock = connect_to_stext();
        if (server_sock < 0) {
            // Notify the client that the file upload failed
            printf("Failed to connect to Stext server\n");
            send(client_sock, "File upload failed", 18, 0);
            return;
        }
        // Send the file to the Stext server
        send_file_to_server(server_sock, client_sock, "ufile", f_name, destination_path, file_data);
        close(server_sock);

    // Check if the file is a C file
    } else if (strstr(filename, ".c") != NULL) {
        // upload by Smain
        if (receive_and_save_file(client_sock, destination_path, f_name, file_data) == 0) {
            // Notify the client that the file upload was successful
            const char *success_message = "File Uploaded successfully.";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        } else {
            // Notify the client that the file upload failed
            const char *failed_message = "File uploading failed!";
            printf("%s\n",failed_message);
            send(client_sock, failed_message, strlen(failed_message), 0);
        }
    } else {
        // If the file type is unsupported, notify the client
        printf("Unsupported file type: %s\n", filename);
        send(client_sock, "Unsupported file type", 21, 0);
    }
}

// Function to handle 'dfile' command
void handle_dfile(int client_sock, char *command) {
    int server_sock;
    char file_path[256];

    // Extract the file path from the command
    sscanf(command, "dfile %s", file_path);

    // check if requested doenload file path is valid or not
    if(!is_valid_path(file_path)){
        printf("ERROR: Invalid path!\n");
        // Send error message if the path is invalid
        const char *error_message = "ERROR: Invalid path!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }
    
    // Extract the file name
    char *file_name = strrchr(file_path, '/') + 1;
    if (!file_name) {
        // Handle case where the file name extraction fails
        send(client_sock, "Invalid file path", 17, 0);
        return;
    }

    // Determine the file type and process accordingly
    if(strstr(file_name,".c") != NULL){
        // Handle .c file - Send file directly to the client
        send_file_to_client(client_sock, file_path, file_name);
    }else if(strstr(file_name,".txt") != NULL){
        // Handle .txt file - Forward request to Stext server
        server_sock = connect_to_stext();
        if (server_sock < 0) {
            printf("Failed to connect to Stext server\n");
            return;
        }
        send_download_request(server_sock, client_sock, "dfile", file_path);
        close(server_sock);

    }else if(strstr(file_name,".pdf") != NULL){
        // Handle .pdf file - Forward request to Spdf server
        server_sock = connect_to_spdf();
        if (server_sock < 0) {
            printf("Failed to connect to Stext server\n");
            return;
        }
        send_download_request(server_sock, client_sock, "dfile", file_path);
        close(server_sock);

    }else{
        printf("Invalid file type\n");
        // Send an error message to the client with a specific prefix
        const char *success_message = "ERROR: Invalid file type!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }
}

// Function to handle 'rmfile' command
void handle_rmfile(int client_sock, char *command) {
    // variable to store the file path
    char file_path[256];
    int server_sock;

    // Extract the file path from the command
    sscanf(command, "rmfile %s", file_path);
    
    // Create a copy of the file path to use for Tokenization
    char file_path_copy[BUFSIZE];
    strncpy(file_path_copy, file_path, BUFSIZE - 1);
    file_path_copy[BUFSIZE - 1] = '\0';

    // Tokenize the file path to get the file name
    char *file_name = NULL;
    char *token = strtok(file_path_copy, "/");
    while (token != NULL) {
        file_name = token;
        token = strtok(NULL, "/");
    }

    // Check if the file has a .pdf extension
    if (strstr(file_name, ".pdf") != NULL) {
        // Connect to the server responsible for handling PDF files
        server_sock = connect_to_spdf();
        // If the connection fails, inform the client and exit the function
        if (server_sock < 0) {
            printf("Failed to connect to Spdf server\n");
            send(client_sock, "File remove failed", 18, 0);
            return;
        }
        // Remove the file from the server
        remove_file_from_server(server_sock, client_sock, "rmfile", file_path);
        // Close the server connection after the operation
        close(server_sock);

    // Check if the file has a .txt extension
    } else if (strstr(file_name, ".txt") != NULL) {
        // Connect to the server responsible for handling text files
        server_sock = connect_to_stext();
        // If the connection fails, inform the client and exit the function
        if (server_sock < 0) {
            printf("Failed to connect to Stext server\n");
            send(client_sock, "File remove failed", 18, 0);
            return;
        }
        // Remove the file from the server
        remove_file_from_server(server_sock, client_sock, "rmfile", file_path);
        // Close the server connection after the operation
        close(server_sock);

    // Check if the file has a .c extension
    } else if (strstr(file_name, ".c") != NULL) {
        // Delete the .c file by Smain
        int result = delete_file(file_path);
        if (result == 0) {
            // Send confirmation to the client
            const char *success_message = "File has been removed!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        }else if (result == 2){
            // Send rejction to the client
            const char *success_message = "File not found!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        }else{
            // Send rejction to the client
            const char *success_message = "File remove Failed!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        }

    // Handle unsupported file types
    } else {
        printf("Unsupported file type: %s\n", file_name);
    }
}

// Function to handle 'dtar' command from client
void handle_dtar(int client_sock, char *command) {
    // variable to store the file extension
    char ext[10];
    // store the server socket connection
    int server_sock;
    // Extract the file extension from the command 
    sscanf(command, "dtar %s", ext);

    // Define the path to be searched
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        // Print an error message if the home directory couldn't be found
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    // Define the full path to the directory that will be searched 
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/smain", home_dir);


    // Check if the file has a .pdf extension
    if (strcmp(ext, ".pdf") == 0) {
        // Connect to the server responsible for handling PDF files
        server_sock = connect_to_spdf();
        // If the connection fails, inform the client and exit the function
        if (server_sock < 0) {
            printf("Failed to connect to Spdf server\n");
            return;
        }
        // Send Request to the server to create a tarball and send it back and forward to client
        request_tar_file(server_sock,client_sock,full_path);
        close(server_sock);

    // Check if the file has a .txt extension
    }else if (strcmp(ext, ".txt") == 0) {
        // Connect to the server responsible for handling PDF files
        server_sock = connect_to_stext();
        // If the connection fails, inform the client and exit the function
        if (server_sock < 0) {
            printf("Failed to connect to Stext server\n");
            return;
        }
        // Send Request to the server to create a tarball and send it back and forward to client
        request_tar_file(server_sock,client_sock,full_path);
        close(server_sock);

    // Check if the file has a .c extension
    }else if (strcmp(ext, ".c") == 0) {
        // Check if the full_path exists and is a directory
        struct stat path_stat;
        if (stat(full_path, &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
            // Print an error message if the directory doesn't exist
            printf("ERROR: Server directory does not exist, expected : %s\n", full_path);
            // Send an error message to the client
            const char *error_message = "ERROR: Server directory does not exist!";
            send(client_sock, error_message, strlen(error_message), 0);
            return;
        }
        // Create a tarball of the ".c" files and send it to the client
        c_tar_file(client_sock, full_path);

    } else {
        // Print a message indicating that the file extension is not supported
        const char *success_message = "ERROR: Invalid Extention Format!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }
}

// Function to handle 'display' command
void handle_display(int client_sock, char *command) {
    // variables to store the pathname and full path
    char pathname[256];
    char full_path[BUFSIZE];
    // variables for server socket connections and file stats
    int server_sock;
    struct stat path_stat;
    int server_sock_pdf,server_sock_txt;

    // Extract the pathname from the command
    sscanf(command, "display %s", pathname);

    // Initialize file lists
    char c_files[BUFSIZE] = "";  // List of .c files
    char pdf_files[BUFSIZE] = ""; // List of .pdf files from server
    char txt_files[BUFSIZE] = ""; // List of .txt files from server

    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    // Construct the full path for the directory
    if (pathname[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, pathname + 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", pathname);
    }

    // Initialize a flag to check if the path exists
    int path_exists = 0;
    if (stat(full_path, &path_stat) == 0) {
        if (S_ISDIR(path_stat.st_mode)) {
            path_exists = 1;
            // Step 1: Retrieve the list of .c files from the local directory
            DIR *dir = opendir(full_path);
            struct dirent *entry;
            // If the directory is opened successfully, read its contents
            if (dir != NULL) {
                while ((entry = readdir(dir)) != NULL) {
                    // Check if the file has a .c extension and add it to the list
                    if (strstr(entry->d_name, ".c") != NULL) {
                        strcat(c_files, entry->d_name);
                        strcat(c_files, "\n");
                    }
                }
                // Close the directory after reading its contents
                closedir(dir);
            }
        } else {
            // If the path exists but is not a directory, print an error
            printf("ERROR: Not a directory in Smain!\n");
        }
    } else {
        // If stat fails, it might be due to an invalid path
        printf("ERROR: Invalid path or not a directory in Smain!\n");
    }

    // Prefix for error messages
    const char *error_prefix = "ERROR:";

    // Construct the message to send
    char message[BUFSIZE];
    snprintf(message, sizeof(message), "display %s", full_path);

    // Step 2: Retrieve .pdf files from Spdf server
    get_file_names_from_server(connect_to_spdf, message, error_prefix, pdf_files, sizeof(pdf_files));

    // Step 3: Retrieve .txt files from Stext server
    get_file_names_from_server(connect_to_stext, message, error_prefix, txt_files, sizeof(txt_files));

    // Step 4: Combine the lists
    char combined_list[3 * BUFSIZE] = "";
    // If the path exists for c files, add .c files to the combined list
    if (path_exists) {
        strcat(combined_list, c_files);
    }
    // Add .pdf and .txt files from the servers to the combined list
    strcat(combined_list, pdf_files);
    strcat(combined_list, txt_files);

    // If no files were found, send an error message to the client
    if(strlen(combined_list) == 0){
        const char *error_message = "ERROR: No files found or given path doesnot exist!";
        printf("%s\n",error_message);
        send(client_sock, error_message, strlen(error_message), 0);
    }else{
        // print and send the list of files to the client
        printf("List of files has been sent to Client\n");
        send(client_sock,combined_list,strlen(combined_list),0);
    }
    
}

// Function to connect to the Spdf server
int connect_to_spdf() {
    // variable to store the socket descriptor
    int server_sock;
    // structure to store the server's address information
    struct sockaddr_in server_addr;

    // Create a socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    // Check if the socket creation was successful
    if (server_sock < 0) {
        perror("Spdf socket creation failed");
        return -1;
    }

    // Set up the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8081);  // Port for Spdf
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the Spdf server
    if (connect(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Print an error message if the connection fails
        perror("Connect to Spdf failed");
        close(server_sock);
        return -1;
    }

    return server_sock;
}

// Function to connect to the Stext server
int connect_to_stext() {
    // variable to store the socket descriptor
    int server_sock;
    // structure to store the server's address information
    struct sockaddr_in server_addr;

    // Create a socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    // Check if the socket creation was successful
    if (server_sock < 0) {
        perror("Stext socket creation failed");
        return -1;
    }

    // Set up the server address
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8082);  // Port for Stext
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the Stext server
    if (connect(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        // Print an error message if the connection fails
        perror("Connect to Stext failed");
        close(server_sock);
        return -1;
    }

    return server_sock;
}


// helper Function to send a file to a specified server for uploading file
void send_file_to_server(int server_sock, int client_sock, char *command, char *filename, char *destination_path, char *file_data) {
    // buffer to hold data
    char buffer[BUFSIZE];
    // store the number of bytes sent
    ssize_t bytes_sent;
    // buffer to hold the response from the server
    char recv_buffer[BUFSIZE];

    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    // Check if the HOME environment variable is available
    if (home_dir == NULL) {
        // Print an error message if the HOME variable is not found
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    
    // Construct the full path for the file (FilePath + file name)
    char full_path[BUFSIZE];
    if (destination_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s/%s/%s", home_dir, destination_path + 1, filename);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", destination_path, filename);
    }
    
    // Construct the message with the command and the full path
    char message[BUFSIZE];
    snprintf(message, sizeof(message), "%s %s\n", command, full_path);

    // Calculate the total length of the message including file data
    size_t total_length = strlen(message) + strlen(file_data) + 1; // +1 for null terminator
    
    // Allocate memory to hold the entire message (command + file path + file data)
    char *complete_message = malloc(total_length);
    if (complete_message == NULL) {
        // Print an error message if memory allocation fails
        perror("Memory allocation failed");
        return;
    }

    // Copy the message and file data into the complete_message buffer
    strcpy(complete_message, message);
    // Append the file data to the complete_message buffer
    strcat(complete_message, file_data);

    // Send the complete message (command, full path, and file data in one go)
    printf("Sending request to server...\n");
    bytes_sent = send(server_sock, complete_message, total_length - 1, 0); // -1 to exclude the null terminator
    if (bytes_sent < 0) {
        // Print an error message if sending fails
        perror("Send failed");
    }

    // Receive and display the confirmation message
    ssize_t bytes_received = recv(server_sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
    // Check if data was received from the server
    if (bytes_received > 0) {
        // Null-terminate the received data to make it a valid string
        recv_buffer[bytes_received] = '\0';
        printf("Server Responce: %s\nforwarding responce to client\n",recv_buffer);
        // Forward the server response to the client
        if (send(client_sock, recv_buffer, bytes_received, 0) < 0) {
            // Print an error message if forwarding to the client fails
            perror("Send to client failed");
        }
    } else if (bytes_received == 0) {
        // Print a message if the connection was closed by the server
        printf("Connection closed by server.\n");
        exit(EXIT_SUCCESS);
    } else {
        // Print an error message if there was an issue receiving data
        perror("Error receiving data");
    }

    // Free allocated memory
    free(complete_message);
}


// Function to receive a file from a client and save it to the specified destination for uploading file
int receive_and_save_file(int sock, char *destination_path, char *f_name, char *file_data) {
    // a buffer to hold data temporarily
    char buffer[BUFSIZE];
    int file_fd;
    // To store the number of bytes received
    ssize_t bytes_received;
    // buffer to hold the directory path
    char dir_path[256];
 
    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return -1;
    }
 
    // Create the full path for the file
    char full_path[BUFSIZE];
    if (destination_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, destination_path + 1);
    } else {
        strncpy(full_path, destination_path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }
 
    // Copy the destination path to dir_path
    strncpy(dir_path, full_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
 
    // Ensure the destination directory exists by creating it if necessary
    char command[BUFSIZE];
    snprintf(command, sizeof(command), "mkdir -p %s", dir_path);
    system(command);
 
    // Construct the full path for the file (path + file name)
    char final_path[512];
    snprintf(final_path, sizeof(final_path), "%s/%s", full_path, f_name);
 
    // Create the file at the specified path with read/write permissions
    file_fd = open(final_path, O_CREAT | O_RDWR | O_TRUNC, 0777);
    if (file_fd < 0) {
        // Print an error message if file creation fails
        perror("File creation failed");
        return -1;
    }
 
    // Write the received file data into the newly created file
    if (file_data) {
        write(file_fd, file_data, strlen(file_data));
    }
    
    // Close the file after writing is complete
    close(file_fd);
    return 0;
}


// Function to remove requested file by client from servers
void remove_file_from_server(int sock, int client_sock, char *command, char *destination_path){
    // Declare a buffer to hold the server's response
    char recv_buffer[BUFSIZE];

    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    
    // Construct the full path for the file (FilePath + file name)
    char full_path[BUFSIZE];
    if (destination_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, destination_path+1);
    }

    // Construct the message to send to the server, including the command and full file path
    char message[BUFSIZE];
    snprintf(message, sizeof(message), "%s %s", command, full_path);
    
    // Send the message to the server
    printf("Sending request to server...\n");
    if (send(sock, message, strlen(message), 0) == -1) {
        // Print an error message if sending fails
        perror("send");
        return;
    }

    // Receive and display the confirmation message from the server
    ssize_t bytes_received = recv(sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
    if (bytes_received > 0) {
        //  Null-terminate the received data to make it a valid string
        recv_buffer[bytes_received] = '\0';
        printf("Server Responce: %s\nforwarding responce to client\n",recv_buffer);
        // Forward the server's response to the client
        if (send(client_sock, recv_buffer, bytes_received, 0) < 0) {
            // Print an error message if forwarding to the client fails
            perror("Send to client failed");
        }
    } else if (bytes_received == 0) {
        // Print a message if the server closed the connection
        printf("Connection closed by server.\n");
        exit(EXIT_SUCCESS);
    } else {
        // Print an error message if there was an issue receiving data
        perror("Error receiving data");
    }
}

// Function to delete a file and handle errors
int delete_file(const char *file_path) {
    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return -1;
    }

    // new file path creation to replace ~
    char full_path[BUFSIZE];
    if (file_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, file_path + 1);
    } else {
        strncpy(full_path, file_path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    }

    // check if file exist or not
    if (access(full_path, F_OK) == -1) {
        return 2;
    }

    // delete the file at the specified path
    if (unlink(full_path) == 0) {
        return 0;
    } else {
        // Handle different errors that could occur during file deletion
        switch (errno) {
            case EACCES:
                fprintf(stderr, "Permission denied: %s\n", full_path);
                break;
            case ENOENT:
                fprintf(stderr, "File does not exist: %s\n", full_path);
                break;
            case EISDIR:
                fprintf(stderr, "Path is a directory, not a file: %s\n", full_path);
                break;
            default:
                fprintf(stderr, "Failed to delete file %s: %s\n", full_path, strerror(errno));
        }
        return -1;
    }
}

// Function to send a file to the client for downloading
void send_file_to_client(int client_sock, const char *file_path, const char *file_name) {
    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    // Construct the full path for the file
    char full_path[BUFSIZE];
    if (file_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, file_path + 1);
    } else {
        snprintf(full_path, sizeof(full_path), "%s", file_path);
    }

    // Open the file for reading
    int file_fd = open(full_path, O_RDONLY);
    if (file_fd < 0) {
        perror("File open failed");
        // Send rejction to the client
        const char *success_message = "ERROR: File not found!";
        printf("%s\n",success_message);
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Send the file name to the client
    send(client_sock, file_name, strlen(file_name), 0);

    // Read the file and send its contents to the client
    char buffer_content[BUFSIZE];
    ssize_t bytes_read,bytes_sent;
    while ((bytes_read = read(file_fd, buffer_content, sizeof(buffer_content))) > 0) {
        bytes_sent = send(client_sock, buffer_content, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Error sending file");
            break;
        }
    }
    if (bytes_read < 0) {
        perror("Error reading file");
    }
    close(file_fd);

    // Send the end marker to indicate the end of the file transfer
    if (send(client_sock, CMD_END_MARKER, strlen(CMD_END_MARKER), 0) == -1) {
        perror("Failed to send end marker");
    }

}


// Function to send a download request to the server and handle the file transfer
void send_download_request(int server_sock, int client_sock, char *command, char *file_path){
    // Replace ~ with the value of the HOME environment variable
    const char *home_dir = getenv("HOME");
    if (home_dir == NULL) {
        fprintf(stderr, "Failed to get HOME environment variable\n");
        return;
    }
    
    // Construct the full path for the file (FilePath + file name)
    char full_path[BUFSIZE];
    if (file_path[0] == '~') {
        snprintf(full_path, sizeof(full_path), "%s%s", home_dir, file_path+1);
    }

    // Construct the message to send to the server, including the command and full file path
    char message[BUFSIZE];
    snprintf(message, sizeof(message), "%s %s", command, full_path);
    
    // Send the message to the server
    printf("Sending download request to server..\n");
    if (send(server_sock, message, strlen(message), 0) == -1) {
        // Print an error message if sending fails
        perror("send");
        return;
    }

    // Receive the file name from the server
    char file_name[256];
    ssize_t bytes_received = recv(server_sock, file_name, sizeof(file_name) - 1, 0);
    if (bytes_received <= 0) {
        // Print an error message if receiving the file name fails
        perror("Error receiving file name");
        return;
    }
    // Null-terminate the file name
    file_name[bytes_received] = '\0'; 

    //send the file name to the client
    if (send(client_sock, file_name, strlen(file_name), 0) == -1) {
        // Print an error message if sending the file name to the client fails
        perror("send");
    }


    // Receive the file content from the server and forward it to the client
    char buffer[BUFSIZE];
    ssize_t content_received;
    // Keep receiving content until there's no more left to receive
    while ((content_received = recv(server_sock, buffer, sizeof(buffer), 0)) > 0) {
        // Forward the received content to the client
        ssize_t bytes_sent = send(client_sock, buffer, content_received, 0);
        if (bytes_sent < 0) {
            // Print an error message if forwarding fails
            perror("send");
            break;
        }

        // Check for the end marker in the buffer to detect the end of the file content
        if (content_received < BUFSIZE) {
            if (memcmp(buffer + content_received - strlen(CMD_END_MARKER), CMD_END_MARKER, strlen(CMD_END_MARKER)) == 0) {
                // Break the loop if the end of the content is reached
                break;
            }
        }
    }
    if (content_received < 0) {
        // Print an error message if there was an issue receiving the file content
        perror("Error receiving file content");
    }
}


// Helper function to request server for file name for given path
void get_file_names_from_server(int (*connect_func)(), const char *message, const char *error_prefix, char *response_buffer, size_t buffer_size) {
    // Establish a connection to the server using the provided connect function
    int server_sock = connect_func();
    if (server_sock < 0) {
        // Print an error message if the connection failed
        printf("Failed to connect to server\n");
        response_buffer[0] = '\0'; // Clear the buffer on connection failure
        return;
    }

    // Send the message to the server
    send(server_sock, message, strlen(message), 0);

    // Clear the response buffer to ensure it's empty before receiving data
    memset(response_buffer, 0, buffer_size);

    // Receive the server's response into the response buffer
    // Read response, leaving space for null terminator
    recv(server_sock, response_buffer, buffer_size - 1, 0); 

    // Check if the response starts with the error prefix
    if (strncmp(response_buffer, error_prefix, strlen(error_prefix)) != 0) {
        // If the response doesn't start with the error prefix, close the connection
        close(server_sock);
    } else {
        // If the response starts with the error prefix, clear the buffer to indicate an error
        response_buffer[0] = '\0';
        close(server_sock);
    }
}


// Helper Function to create a tarball of .c files and send it to the client
void c_tar_file(int client_sock, const char *path) {
    // variables to hold the command for creating the tarball and the target path
    char tar_cmd[BUFSIZE];
    char target_path[BUFSIZE];

    // Create the full path for the tarball file, which will be stored in the given path
    snprintf(target_path,sizeof(target_path), "%s/%s",path,TAR_FILE_PATH);

    // Check for the presence of .c files first
    snprintf(tar_cmd, sizeof(tar_cmd), "find %s -name '*.c' -print -quit", path);
    // Run the command to check for .c files and store the result
    FILE *check = popen(tar_cmd, "r");
    // If the check command fails, inform the client and exit the function
    if (check == NULL) {
        printf("ERROR: Failed to check for .c files.\n");
        const char *error_message = "ERROR: Failed to check for .c files!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }

    // If no .c files are found, inform the client and exit the function
    if (fgetc(check) == EOF) {
        printf("No .c files found.\n");
        const char *error_message = "ERROR: No .c files found!";
        send(client_sock, error_message, strlen(error_message), 0);
        pclose(check);
        return;
    }
    // Close the check command as .c files were found
    pclose(check);

    // If .c files are found, create the tarball using the find command and tar command
    snprintf(tar_cmd, sizeof(tar_cmd), "find %s -name '*.c' -print0 | tar -cf %s --null -T - 2>/dev/null", path, target_path);
    // Run the command to create the tarball
    int result = system(tar_cmd);
    // If the tarball creation fails, inform the client and exit the function
    if (result != 0) {
        printf("ERROR: Failed to create tarball for .c files.\n");
        const char *error_message = "ERROR: Tar file creation failed!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }

    // send tar filename to client
    send(client_sock, TAR_FILE_PATH, strlen(TAR_FILE_PATH), 0);

    // Check if the tarball file was successfully created
    if (access(target_path, F_OK) != 0) {
        // Send rejction to the client
        const char *success_message = "ERROR: Tar file creation failed!";
        printf("%s\n",success_message);
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Open the tarball file to read its contents
    FILE *tarball = fopen(target_path, "rb");
    if (tarball == NULL) {
        // If the tarball file cannot be opened
        printf("ERROR: Failed to open tarball file.\n");
        // Send rejction to the client
        const char *success_message = "ERROR: Tar file creation failed!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Declare a buffer to hold the file content as it is read
    char file_buffer[1024];
    size_t bytes_read;
    // Read the tarball file and send its contents to the client
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), tarball)) > 0) {
        // Send the read data to the client
        ssize_t bytes_sent = send(client_sock, file_buffer, bytes_read, 0);
        // If sending the data fails, inform the client and exit the function
        if (bytes_sent < 0) {
            perror("Failed to send tarball data");
            // Send rejction to the client
            const char *success_message = "ERROR: Tar file creation failed!";
            send(client_sock, success_message, strlen(success_message), 0);
            fclose(tarball);
            return;
        }
    }

    // Send an end-of-file marker to signal the end of the file content
    const char *end_marker = "END_CMD";
    send(client_sock, end_marker, strlen(end_marker), 0);
    // Close the tarball file after sending its contents
    fclose(tarball);
    printf("Tarball sent to client.\n");
}

// Function to request a tarball file from a server and forward it to the client
void request_tar_file(int server_sock, int client_sock, char *path){
    // Construct the message to send to the server, including the command and server path
    char message[BUFSIZE];
    snprintf(message, sizeof(message), "dtar %s", path);

    // Send the message to the server
    printf("Sending tar file download request to server\n");
    if (send(server_sock, message, strlen(message), 0) == -1) {
        // Print an error message if sending fails
        perror("send");
        return;
    }

    // Receive the file name from the server
    char file_name[256];
    ssize_t bytes_received = recv(server_sock, file_name, sizeof(file_name) - 1, 0);
    // If receiving the file name fails, print an error message and exit
    if (bytes_received <= 0) {
        // Print an error message if receiving the file name fails
        perror("Error receiving file name");
        return;
    }
    // Null-terminate the received file name string
    file_name[bytes_received] = '\0'; 

    //send the file name to the client
    if (send(client_sock, file_name, strlen(file_name), 0) == -1) {
        // Print an error message if sending the file name to the client fails
        perror("send");
    }

    // buffer to hold the file content received from the server
    char buffer_data[BUFSIZE];
    ssize_t content_received;
    // Keep receiving file content from the server and forward it to the client
    while ((content_received = recv(server_sock, buffer_data, sizeof(buffer_data), 0)) > 0) {
        // Send the received content to the client
        ssize_t bytes_sent = send(client_sock, buffer_data, content_received, 0);
        // If sending the content fails, print an error message and exit
        if (bytes_sent < 0) {
            // Print an error message if forwarding fails
            perror("send");
            break;
        }

        // Check for the end marker in the buffer_data to detect the end of the file content
        if (content_received < BUFSIZE) {
            if (memcmp(buffer_data + content_received - strlen(CMD_END_MARKER), CMD_END_MARKER, strlen(CMD_END_MARKER)) == 0) {
                // Break the loop if the end of the content is reached
                break;
            }
        }
    }
    // If receiving the content fails, print an error message
    if (content_received < 0) {
        // Print an error message if there was an issue receiving the file content
        perror("Error receiving file content");
    }else{
        // Print a message indicating that the file was successfully received and forwarded to the client
        printf("'%s' received and send to client.\n",file_name);
    }
}