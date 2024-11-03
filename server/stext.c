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
#include <dirent.h>
#include <sys/wait.h>

// Define constants for the port number and buffer size
#define PORT 8082
#define BUFSIZE 102400
#define CMD_END_MARKER "END_CMD"
#define TAR_FILE_PATH "text_files.tar"

// Function prototypes
void handle_client(int client_sock);
char* create_txt_path(const char *destination_path);
int delete_file(const char *file_path);
void handle_ufile(int client_sock, char *command, char *file_data);
void handle_dfile(int client_sock, char *command);
void handle_rmfile(int client_sock, char *command);
void handle_dtar(int client_sock, char *command);
void handle_display(int client_sock, char *command);
void send_file_back_to_smain(int smain_sock, const char *file_path, const char *file_name);
void txt_tar_file(int client_sock, const char *path);

// This function handles communication with a connected client (Smain)
void handle_client(int client_sock) {
    // Buffer to store data received from the client
    char buffer[BUFSIZE];
    // Variable to hold the number of bytes received
    ssize_t bytes_received;
    // Pointer to store the command part of the message
    char *command;
    // Pointer to store the file data part of the message
    char *file_data;

    // Receive the combined message (command and possibly file data) from the client(Smain)
    bytes_received = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received data

        // Determine which command was sent by the client and handle it accordingly
        if (strncmp(buffer, "ufile", 5) == 0) {
            // Locate the newline character that separates the command from the file data
            char *delimiter = strstr(buffer, "\n");
            if (delimiter == NULL) {
                printf("Invalid message format\n");
                return;
            }
            // Null-terminate the command
            *delimiter = '\0';
            // Extract the file data
            file_data = delimiter + 1;
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
        } else {
            // If the command is unknown, print an error message
            printf("Unknown command: %s\n", buffer);
        }
    } else {
        // Handle the case where no data is received or an error occurred
        if (bytes_received == 0) {
            printf("Connection closed by peer\n");
        } else {
            perror("Receive command failed");
        }
    }

    // Close the connection with the client after handling the command
    close(client_sock);
}

// This function handles the 'ufile' command to upload a file to the server
void handle_ufile(int client_sock, char *command, char *file_data) {
    // Buffer to store the destination file path
    char destination_path[1024];
    // File descriptor for the file being created
    int file_fd;

    // Extract the destination path from the 'ufile' command and check for error and send that error to Smain(client)
    int parsed = sscanf(command, "ufile %s", destination_path);
    if (parsed < 1) {
        printf("Command parsing failed\n");
        send(client_sock, "File upload failed", 18, 0);
        return;
    }

    // Create a new file path by modifying the destination path(Replace smain with stext)
    char *new_file_path = create_txt_path(destination_path);
    if (new_file_path != NULL) {
        // Ensure the destination directory exists
        char *last_slash = strrchr(new_file_path, '/');
        if (last_slash != NULL) {   
            // Temporarily remove the last part of the path
            *last_slash = '\0';
            char command_buf[BUFSIZE];
            // Create the directory if it does not exist, if fails print it and send error to client(Smain)
            snprintf(command_buf, sizeof(command_buf), "mkdir -p %s", new_file_path);
            if (system(command_buf) != 0) {
                perror("Directory creation failed");
                send(client_sock, "File upload failed", 18, 0);
                free(new_file_path);
                return;
            }
            // Restore the original path
            *last_slash = '/';  
        }

        // Create the file for writing, if error encounter print and send it to the Smain(Client)
        file_fd = open(new_file_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (file_fd < 0) {
            perror("File creation failed");
            send(client_sock, "File upload failed", 18, 0);
            close(file_fd);
            free(new_file_path);
            return;
        }

        // Write the file data to the file, if error encounter print and send it to the Smain(Client)
        ssize_t file_data_len = strlen(file_data);
        if (write(file_fd, file_data, file_data_len) < 0) {
            perror("File write failed");
            send(client_sock, "File upload failed", 18, 0);
            close(file_fd);
            free(new_file_path);
        }

        // Close the file after writing the data
        close(file_fd);

        // Send confirmation to the client
        const char *success_message = "File Uploaded successfully.";
        printf("Sending responce to Smain.\n%s\n",success_message);
        send(client_sock, success_message, strlen(success_message), 0);

        // Free the memory allocated for the new file path
        free(new_file_path);
    }else{
        // Send an error message to the client if file uploading faile
        const char *failed_message = "File uploading failed!";
        send(client_sock, failed_message, strlen(failed_message), 0);
    }
}


// function to handle the 'dfile' command, which would download a file from the server
void handle_dfile(int client_sock, char *command) {
    // Buffer to store the file path
    char file_path[1024];
    // Buffer to store data for transmission
    char buffer[BUFSIZE];
    // File descriptor for the file being read
    int file_fd;
    // Variable to store the number of bytes read
    ssize_t bytes_read;

    // Ensure command string is properly null-terminated
    command[strcspn(command, "\r\n")] = '\0';

    // Extract the file path from the command
    if (sscanf(command, "dfile %s", file_path) != 1) {
        printf("Command parsing failed!\n");
        // Send rejction to the client
        const char *success_message = "ERROR: Command parsing failed!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Create a new file path by modifying the file path(Replace smain with stext)
    char *new_file_path = create_txt_path(file_path);

    // Extract the file name from the full file path
    char *file_name = strrchr(file_path, '/') + 1;

    // Send the requested file back to the client
    send_file_back_to_smain(client_sock, new_file_path, file_name);

    // Free the memory allocated for the new file path
    free(new_file_path);
}

// function to handle the 'rmfile' command, which would remove a file from the server
void handle_rmfile(int client_sock, char *command) {
    // Buffer to store the file path
    char file_path[1024];
    // Buffer to store data for transmission
    char buffer[BUFSIZE];
    // File descriptor for the file being read
    int file_fd;
    // Variable to store the number of bytes read
    ssize_t bytes_read;

    // Ensure command string is properly null-terminated
    command[strcspn(command, "\r\n")] = '\0';

    // Extract the file path from the 'rmfile' command, and print error if any
    if (sscanf(command, "rmfile %s", file_path) != 1) {
        printf("Command parsing failed\n");
        return;
    }

    // Create a new file path by modifying the file path(Replace smain with stext)
    char *new_file_path = create_txt_path(file_path);
    if (new_file_path != NULL){
        // check if file exist or not
        if (access(new_file_path, F_OK) == -1) {
            // Send rejction to the client
            const char *success_message = "File not found!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
            return;
        }

        //if exist then delete
        if (delete_file(new_file_path) != 0) {
            // Send rejction to the client
            const char *success_message = "File remove Failed!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        }else{
            // Send confirmation to the client
            const char *success_message = "File has been removed!";
            printf("%s\n",success_message);
            send(client_sock, success_message, strlen(success_message), 0);
        }
    }else{
        // Send rejction to the client
        const char *success_message = "File remove Failed!";
        printf("%s\n",success_message);
        send(client_sock, success_message, strlen(success_message), 0);
    }
}

// Function to handle the 'dtar' command from the client(Smain)
void handle_dtar(int client_sock, char *command) {
    char path[BUFSIZE];
    // Extract the file path from the command using sscanf
    sscanf(command, "dtar %s", path);

    // Create a new file path by modifying the file path(Replace smain with stxt)
    char *new_file_path = create_txt_path(path);

    // Check if the full_path exists and is a directory
    struct stat path_stat;
    if (stat(new_file_path, &path_stat) != 0 || !S_ISDIR(path_stat.st_mode)) {
        // If the path doesn't exist or isn't a directory, inform the client(Smain) and exit the function
        printf("ERROR: Server directory does not exist, expected : %s\n", new_file_path);
        const char *error_message = "ERROR: Server directory does not exist!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }
    // If the path is valid, create a tarball of .txt files and send it to the client(Smain)
    txt_tar_file(client_sock,new_file_path);
}

// function to handle the 'display' command
void handle_display(int client_sock, char *command) {
    // Buffer to store the directory path
    char dir_path[1024];
    // Structure to store information about the directory
    struct stat path_stat;

    // Ensure command string is properly null-terminated
    command[strcspn(command, "\r\n")] = '\0';

    // Extract the file path from the 'display' command, and print error if any
    if (sscanf(command, "display %s", dir_path) != 1) {
        printf("Command parsing failed\n");
        return;
    }

    // Create a new file path by modifying the file path(Replace smain with stext)
    char *new_dir_path = create_txt_path(dir_path);

    // Check if the given path is a valid (Exist)
    if (stat(new_dir_path, &path_stat) != 0) {
        // Error in stat, path might not exist
        const char *error_message = "ERROR: Invalid path or not a directory!";
        send(client_sock, error_message, strlen(error_message), 0);
        printf("%s\n",error_message);
        return;
    }

    // Check if the path is a directory
    if (!S_ISDIR(path_stat.st_mode)) {
        // Path exists but is not a directory
        const char *error_message = "ERROR: Not a directory!";
        send(client_sock, error_message, strlen(error_message), 0);
        printf("%s\n",error_message);
        return;
    }

    // Buffer to store the list of .txt files
    char txt_files[BUFSIZE] = "";
    // Open the directory
    DIR *dir = opendir(new_dir_path);
    struct dirent *entry;
    // Read through the directory and find .txt files
    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (strstr(entry->d_name, ".txt") != NULL) {
                strcat(txt_files, entry->d_name);
                strcat(txt_files, "\n");
            }
        }
        // Close the directory after reading
        closedir(dir);
    }

    // If no files were found, send an error message to the client
    if(strlen(txt_files) == 0){
        const char *error_message = "ERROR: No files found or given path doesnot exist!";
        printf("%s\n",error_message);
    }else{
        // Print the list of .txt files
        printf("%s\n",txt_files);
        // Send the list to the client(Smain)
        send(client_sock, txt_files, strlen(txt_files), 0);
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

    // Create a new file path by modifying the file path(Replace smain with stext)
    char *txt_path = create_txt_path(full_path);
    if (txt_path != NULL) {
        // delete the file
        if (unlink(txt_path) == 0) {
            return 0;
        } else {
            // Handle error based on errno
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
        // Free the allocated memory
        free(txt_path);
    }
}


// helper function used to send data of requested doenload file to the client(Smain)
void send_file_back_to_smain(int smain_sock, const char *file_path, const char *file_name) {
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
        send(smain_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Send the file name
    send(smain_sock, file_name, strlen(file_name), 0);

    // Read the file and send its contents to the client(Smain)
    char buffer_content[BUFSIZE];
    ssize_t bytes_read, bytes_sent;
    while ((bytes_read = read(file_fd, buffer_content, sizeof(buffer_content))) > 0) {
        bytes_sent = send(smain_sock, buffer_content, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Error sending file");
            // Send rejction to the client
            const char *success_message = "ERROR: Download Failed!";
            send(smain_sock, success_message, strlen(success_message), 0);
            break;
        }
    }
    if (bytes_read < 0) {
        perror("Error reading file");
        // Send rejction to the client
        const char *success_message = "ERROR: Error reading file!";
        send(smain_sock, success_message, strlen(success_message), 0);
    }
    close(file_fd);

    // Send the end marker
    if (send(smain_sock, CMD_END_MARKER, strlen(CMD_END_MARKER), 0) == -1) {
        perror("Failed serve request");
        // Send rejction to the client
        const char *success_message = "ERROR: Failed to serve request!";
        send(smain_sock, success_message, strlen(success_message), 0);
    }
}

// Function to create a tarball of .txt files and send it to the client
void txt_tar_file(int client_sock, const char *path) {
    // variables to hold the command for creating the tarball and the target path
    char tar_cmd[BUFSIZE];
    char target_path[BUFSIZE];

    // Create path for the tarball file, which will be stored in the given path
    snprintf(target_path,sizeof(target_path), "%s/%s",path,TAR_FILE_PATH);

    // Check for the presence of .txt files first
    snprintf(tar_cmd, sizeof(tar_cmd), "find %s -name '*.txt' -print -quit", path);
    // Run the command to check for .txt files and store the result
    FILE *check = popen(tar_cmd, "r");
    // If the check command fails, inform the client(Smain) and exit the function
    if (check == NULL) {
        printf("ERROR: Failed to check for .txt files.\n");
        const char *error_message = "ERROR: Failed to check for .txt files!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }

    // If no .txt files found, send error to client(Smain)
    if (fgetc(check) == EOF) {
        printf("No .txt files found.\n");
        const char *error_message = "ERROR: No .txt files found!";
        send(client_sock, error_message, strlen(error_message), 0);
        pclose(check);
        return;
    }
    pclose(check);

    // Create the tarball if .txt files are found
    snprintf(tar_cmd, sizeof(tar_cmd), "find %s -name '*.txt' -print0 | tar -cf %s --null -T - 2>/dev/null", path, target_path);
    int result = system(tar_cmd);
    // If the tarball creation fails, inform the client(Smain) and exit the function
    if (result != 0) {
        printf("ERROR: Failed to create tarball for .txt files.\n");
        const char *error_message = "ERROR: Tar file creation failed!";
        send(client_sock, error_message, strlen(error_message), 0);
        return;
    }

    // send file name to client(Smain)
    send(client_sock, TAR_FILE_PATH, strlen(TAR_FILE_PATH), 0);


    // Check if the tarball file was successfully created
    if (access(target_path, F_OK) != 0) {
        printf("No .txt files found or failed to create tarball.\n");
        // Send rejction to the client
        const char *success_message = "ERROR: Tar file creation failed!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Open the tarball file to read its contents
    FILE *tarball = fopen(target_path, "rb");
    // If the tarball file cannot be opened, inform the client(Smain)
    if (tarball == NULL) {
        printf("Failed to open tarball file.\n");
        // Send rejction to the client
        const char *success_message = "ERROR: Tar file creation failed!";
        send(client_sock, success_message, strlen(success_message), 0);
        return;
    }

    // Send the file content
    char file_buffer[1024];
    size_t bytes_read;
    while ((bytes_read = fread(file_buffer, 1, sizeof(file_buffer), tarball)) > 0) {
        ssize_t bytes_sent = send(client_sock, file_buffer, bytes_read, 0);
        if (bytes_sent < 0) {
            perror("Failed to send tarball data");
            // Send rejction to the client
            const char *success_message = "ERROR: Tar file creation failed!";
            send(client_sock, success_message, strlen(success_message), 0);
            fclose(tarball);
            return;
        }
    }

    // Send end-of-file marker
    const char *end_marker = "END_CMD";
    send(client_sock, end_marker, strlen(end_marker), 0);

    fclose(tarball);
    printf("Tarball sent to Smain.\n");
}

// helper function to create path for text sercer by replacing smain to stext
char* create_txt_path(const char *destination_path) {
    // Pointer to store the position of "smain" in the path
    char *pos;
    // Calculate the size of the original path
    size_t new_path_size = strlen(destination_path); 
    // Allocate memory for the new path
    char *new_path = malloc(new_path_size);

    // Check if memory allocation was successful
    if (new_path == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        // Return NULL if memory allocation fails
        return NULL;
    }

    // Find the position of "smain" in the original path
    pos = strstr(destination_path, "smain");
    if (pos != NULL) {
        // If "smain" is found, copy the part before "smain" into the new path
        // Calculate the length of the prefix before "smain"
        size_t prefix_len = pos - destination_path;
        // Copy the prefix to the new path
        strncpy(new_path, destination_path, prefix_len);
        // Null-terminate the string
        new_path[prefix_len] = '\0'; 

        // Append "stext" to new_path
        strcat(new_path, "stext");

        // Append the rest of the original path after "smain" to the new path
        strcat(new_path, pos + strlen("smain"));
    } else {
        // If "smain" is not found, simply copy the original path to the new path
        strcpy(new_path, destination_path);
    }
    // Return the newly created path
    return new_path;
}

int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_size;
    pid_t child_pid;

    // Create a socket for the server
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Configure the server address
    server_addr.sin_family = AF_INET;
    // Set the port number, converting to network byte order
    server_addr.sin_port = htons(PORT);
    // Accept connections
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // Zero out the rest of the structure
    memset(server_addr.sin_zero, '\0', sizeof(server_addr.sin_zero));

    // Bind the socket to the specified port and address
    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections, with a backlog of 10 pending connections
    if (listen(server_sock, 10) < 0) {
        perror("Listen failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    printf("Stext server is listening on port %d\n", PORT);

    while (1) {
        // Accept a client connection
        addr_size = sizeof(client_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_size);
        if (client_sock < 0) {
            // Continue to the next iteration if accept fails
            perror("Accept failed");
            continue;
        }

        printf("Connection accepted from %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Fork a child process to handle the client
        child_pid = fork();
        if (child_pid == 0) {
            // In the child process
            close(server_sock);  // Close the server socket in the child
            handle_client(client_sock);  // Handle communication with the client
            close(client_sock);  // Close the client socket in the child
            exit(0);  // Exit the child process
        } else if (child_pid > 0) {
            // In the parent process
            close(client_sock);  // Close the client socket in the parent
            // Wait for terminated child processes to avoid zombie processes
            while (waitpid(-1, NULL, WNOHANG) > 0) {
                // Continue to reap any terminated child processes
            }
        } else {
            perror("Fork failed");
        }
    }

    close(server_sock);  // Close the server socket
    return 0;
}