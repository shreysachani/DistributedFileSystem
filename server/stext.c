#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h> // For open(), O_WRONLY, O_CREAT, etc.

#define BUFFER_SIZE 1024
#define SERVER_PORT 7114

void handle_client(int client_socket);
void process_ufile_command(int client_socket, char *filename, char *destination_path);
void process_rmfile_command(int client_socket, const char *path);
void process_dfile_command(int client_socket, const char *path);
void create_directory_path(const char *path);
char *get_home_directory();
void handle_list_command(int client_socket, const char *path);

// Global variable to store file content

char file_content[BUFFER_SIZE * 100]; // Adjust size based on expected content

int main()
{
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    // Create a socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("Error opening socket");
        exit(1);
    }

    // Prepare the sockaddr_in structure
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(SERVER_PORT);

    // Bind the socket
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Error on binding");
        close(sockfd);
        exit(1);
    }

    // Listen for incoming connections
    listen(sockfd, 5);
    printf("Stext server listening on port %d...\n", SERVER_PORT);

    while (1)
    {
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd < 0)
        {
            perror("Error on accept");
            continue;
        }
        printf("Client connected: %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
        // Fork a new process to handle the client
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("Error on fork");
            close(newsockfd);
            continue;
        }
        if (pid == 0)
        {                  // Child process
            close(sockfd); // Child process doesn't need the listening socket
            handle_client(newsockfd);
            close(newsockfd);
            exit(0); // Terminate child process
        }
        else
        {                     // Parent process
            close(newsockfd); // Parent doesn't need this socket
        }
    }
    close(sockfd);
    return 0;
}
// Handle client connection
void handle_client(int client_socket)
{
    char buffer[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char destination_path[BUFFER_SIZE];
    char *command_line;
    char *content_start;
    int content_length;

    // Clear the global variable at the start
    memset(file_content, 0, sizeof(file_content));
    while (1)
    {
        memset(buffer, 0, BUFFER_SIZE);
        int n = recv(client_socket, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0)
        {
            printf("Client disconnected.\n");
            close(client_socket);
            return;
        }

        buffer[n] = '\0';                       // Null-terminate the received string
        printf("Received data:\n%s\n", buffer); // Debug: print received data
        // Find the end of the first line (the command)
        command_line = strchr(buffer, '\n');
        if (command_line)
        {
            *command_line = '\0'; // Null-terminate the command line
            command_line++;       // Move to the start of the content
            // Skip any blank lines before the content
            while (*command_line == '\n' || *command_line == '\r')
            {
                command_line++;
            }
            printf("Command line: %s\n", buffer);        // Debug: print command line
            printf("Content start: %s\n", command_line); // Debug: print start of content

            // Store the content part
            content_start = command_line;
            content_length = strlen(content_start);
            strncpy(file_content, content_start, content_length);
            file_content[content_length] = '\0';           // Null-terminate the file content
            printf("Stored content:\n%s\n", file_content); // Debug: print stored content
            // Parse the command
            if (sscanf(buffer, "ufile %s %s", filename, destination_path) >= 2)
            {
                printf("Parsed filename: %s\n", filename);
                printf("Parsed destination path: %s\n", destination_path);
                process_ufile_command(client_socket, filename, destination_path);
                break; // Exit the loop after processing the command
            }
            else if (strncmp(buffer, "display ", 8) == 0)
            { // +5 to skip "list "
                char *path = "/home/sachanis/stext/test";
                handle_list_command(client_socket, path);
                break;
            }
            else if (strncmp(buffer, "rmfile ", 7) == 0)
            {
                process_rmfile_command(client_socket, buffer + 7); // +7 to skip "rmfile "
                break;
            }
            else if (strncmp(buffer, "dfile ", 6) == 0)
            {
                process_dfile_command(client_socket, buffer + 6); // +6 to skip "dfile "
                break;
            }
            else
            {
                send(client_socket, "Invalid command received.\n", 26, 0);
                return;
            }
        }
        else
        {
            send(client_socket, "Invalid data format.\n", 21, 0);
            return;
        }
    }
}

void handle_list_command(int client_socket, const char *path)
{
    struct dirent **namelist;
    int n;
    char response[BUFFER_SIZE] = "";

    // Use scandir to read directory entries
    n = scandir(path, &namelist, NULL, alphasort);
    if (n < 0)
    {
        perror("Could not open directory");
        const char *error_msg = "Could not open directory\n";
        write(client_socket, error_msg, strlen(error_msg));
    }
    else
    {
        for (int i = 0; i < n; i++)
        {
            if (namelist[i]->d_type == 8)
            { // If it's a regular file
                strcat(response, namelist[i]->d_name);
                strcat(response, "\n");
            }
            free(namelist[i]); // Free the allocated memory for each entry
        }
        free(namelist); // Free the array of pointers

        if (strlen(response) > 0)
        {
            write(client_socket, response, strlen(response));
        }
        else
        {
            const char *no_files_msg = "No files found in directory\n";
            write(client_socket, no_files_msg, strlen(no_files_msg));
        }
    }
}

// Process the 'dfile' command for retrieving file content
void process_dfile_command(int client_socket, const char *path)
{
    char home_dir[BUFFER_SIZE];
    char corrected_path[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];
    int file;
    ssize_t bytes_read;
    snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());
    if (strncmp(path, "~smain", 6) == 0)
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/stext/%s", home_dir, path + 7);
    }
    else
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/%s", home_dir, path);
    }
    snprintf(full_path, sizeof(full_path), "%s", corrected_path);
    // Check if file exists
    if (access(full_path, F_OK) != 0)
    {
        const char *not_found_msg = "File does not exist.\n";
        write(client_socket, not_found_msg, strlen(not_found_msg));
        return;
    }
    // Open file for reading
    file = open(full_path, O_RDONLY);
    if (file < 0)
    {
        perror("Error opening file for reading");
        const char *error_msg = "Error opening file.\n";
        write(client_socket, error_msg, strlen(error_msg));
        return;
    }
    // Read and send file content
    while ((bytes_read = read(file, file_content, sizeof(file_content) - 1)) > 0)
    {
        file_content[bytes_read] = '\0'; // Null-terminate the file content
        write(client_socket, file_content, bytes_read);
    }
    if (bytes_read < 0)
    {
        perror("Error reading file");
        const char *error_msg = "Error reading file.\n";
        write(client_socket, error_msg, strlen(error_msg));
    }
    close(file);
}
// Process the 'rmfile' command for deleting files
void process_rmfile_command(int client_socket, const char *path)
{
    char home_dir[BUFFER_SIZE];
    char corrected_path[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];
    // Get home directory for 'stext'
    snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());
    // Handle path replacement from '~smain' to '~stext'
    if (strncmp(path, "~smain", 6) == 0)
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/stext/%s", home_dir, path + 7);
    }
    else
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/%s", home_dir, path);
    }
    // Full path for the file
    snprintf(full_path, sizeof(full_path), "%s", corrected_path);
    // Check if file exists
    if (access(full_path, F_OK) == 0)
    {
        // File exists, attempt to remove it
        if (remove(full_path) == 0)
        {
            printf("File removed successfully.\n");
            const char *success_msg = "File removed successfully.\n";
            write(client_socket, success_msg, strlen(success_msg));
        }
        else
        {
            perror("Error removing file");
            const char *error_msg = "Error removing file.\n";
            write(client_socket, error_msg, strlen(error_msg));
        }
    }
    else
    {
        // File does not exist
        const char *not_found_msg = "File does not exist.\n";
        write(client_socket, not_found_msg, strlen(not_found_msg));
    }
}
// Process the 'ufile' command for .txt files
void process_ufile_command(int client_socket, char *filename, char *destination_path)
{
    printf("Processing 'ufile' command...\n");
    char home_dir[BUFFER_SIZE];
    char corrected_path[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];
    // Get home directory for 'stext'
    snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());
    // Handle path replacement from '~smain' to '~stext'
    if (strncmp(destination_path, "~smain", 6) == 0)
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/stext/%s", home_dir, destination_path + 7);
    }
    else
    {
        snprintf(corrected_path, sizeof(corrected_path), "%s/%s", home_dir, destination_path);
    }
    // Create directory structure
    create_directory_path(corrected_path);
    // Full path for the file
    snprintf(full_path, sizeof(full_path), "%s/%s", corrected_path, filename);
    // Open file for writing
    int file = open(full_path, O_WRONLY | O_CREAT, 0666);
    if (file < 0)
    {
        perror("Error opening file for writing");
        printf("Failed to open file for writing: %s\n", full_path);
        write(client_socket, "Failed to upload the file.\n", 27);
        return;
    }
    // Write the content from the global variable to the file
    ssize_t bytes_written = write(file, file_content, strlen(file_content));
    if (bytes_written < 0)
    {
        perror("Error writing to file");
        write(client_socket, "Error writing file content.\n", 28);
    }
    else
    {
        printf("File content written successfully.\n");
    }
    close(file);
    printf("File closed successfully.\n");
    // Clear the global variable after writing the content
    memset(file_content, 0, sizeof(file_content));
    write(client_socket, "File uploaded successfully.\n", 28);
    printf("File uploaded and confirmation sent to client.\n");
}
// Create the directory structure if it doesn't exist
void create_directory_path(const char *path)
{
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%s", path);
    char *p = temp + 1; // Skip the initial '/'
    while (*p)
    {
        if (*p == '/')
        {
            *p = '\0'; // Temporarily terminate the string
            if (mkdir(temp, S_IRWXU) < 0 && errno != EEXIST)
            {
                perror("Error creating directory");
            }
            *p = '/'; // Restore the '/' character
        }
        p++;
    }
    if (mkdir(temp, S_IRWXU) < 0 && errno != EEXIST)
    {
        perror("Error creating directory");
    }
}
// Get the home directory
char *get_home_directory()
{
    return getenv("HOME");
}