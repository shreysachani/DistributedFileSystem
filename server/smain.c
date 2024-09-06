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

#define BUFFER_SIZE 1024
#define SERVER_PORT 7139
#define STEXT_SERVER_IP "127.0.0.1"
#define STEXT_SERVER_PORT 7114
#define SPDF_SERVER_PORT 7115

void handle_client(int client_socket);
void process_ufile_command(int client_socket, char *filename, char *destination_path);
void process_dfile_command(int client_socket, char *filepath);
void process_rmfile_command(int client_socket, char *filepath);
void process_display_command(int client_socket, char *path);
void process_txt_file(int client_socket, char *filename, char *destination_path);
void process_pdf_file(int client_socket, char *filename, char *destination_path);
int is_c_file(const char *filename);
int is_txt_file(const char *filename);
int is_pdf_file(const char *filename);
void create_directory_path(const char *path);
void forward_to_server(const char *server_ip, int server_port, const char *command);
void list_files_in_directory(const char *path, char *output, size_t output_size);
void get_files_from_server(const char *server_ip, int server_port, const char *path, char *output, size_t output_size);
void handle_list_command(int client_socket, const char *path);
void handle_display_command(int client_socket, const char *path);

char *get_home_directory()
{
  return getenv("HOME");
}

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
  printf("Smain server listening on port %d...\n", SERVER_PORT);

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
    {                // Child process
      close(sockfd); // Child process doesn't need the listening socket
      handle_client(newsockfd);
      close(newsockfd);
      exit(0); // Terminate child process
    }
    else
    {                   // Parent process
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

    buffer[n] = '\0';                         // Null-terminate the received string
    printf("Received command: %s\n", buffer); // Debug: print received command

    // Parse the command
    if (sscanf(buffer, "ufile %s %s", filename, destination_path) == 2)
    {
      process_ufile_command(client_socket, filename, destination_path);
    }

    // In the handle_client function, add this case:
    else if (strncmp(buffer, "dfile ", 6) == 0)
    {
      char filepath[BUFFER_SIZE];
      if (sscanf(buffer, "dfile %s", filepath) == 1)
      {
        process_dfile_command(client_socket, filepath);
      }
      else
      {
        write(client_socket, "Invalid dfile command.\n", 23);
      }
    }
    else if (strncmp(buffer, "rmfile ", 7) == 0)
    {
      process_rmfile_command(client_socket, buffer + 7); // +7 to skip "rmfile "
    }
    else if (strncmp(buffer, "display ", 8) == 0)
    {
      handle_display_command(client_socket, buffer + 8);
      ; // +8 to skip "display "
    }
    else
    {
      write(client_socket, "Invalid command received.\n", 26);
    }
  }
}

void forward_to_server(const char *server_ip, int server_port, const char *command)
{
  int sockfd;
  struct sockaddr_in serv_addr;
  char buffer[BUFFER_SIZE];

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Error opening socket to forward command");
    return;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(server_port);
  serv_addr.sin_addr.s_addr = inet_addr(server_ip);
  inet_pton(AF_INET, server_ip, &serv_addr.sin_addr);

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    perror("Error connecting to forwarding server");
    close(sockfd);
    return;
  }

  // write(sockfd, command, strlen(command));
  snprintf(buffer, sizeof(buffer), "%s\n", command);
  if (send(sockfd, buffer, strlen(buffer), 0) < 0)
  {
    perror("Error sending command to server");
  }
  close(sockfd);
}

void process_ufile_command(int client_socket, char *filename, char *destination_path)
{

  if (is_c_file(filename))
  {
    // Handle .c file locally
    char home_dir[BUFFER_SIZE];
    char corrected_path[BUFFER_SIZE];
    char full_path[BUFFER_SIZE];

    snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());

    // Replace '~' with home directory path
    if (destination_path[0] == '~')
    {
      snprintf(corrected_path, sizeof(corrected_path), "%s/%s", home_dir, destination_path + 1);
    }
    else
    {
      snprintf(corrected_path, sizeof(corrected_path), "%s/%s", home_dir, destination_path);
    }

    // Debug print

    create_directory_path(corrected_path);

    snprintf(full_path, sizeof(full_path), "%s/%s", corrected_path, filename);

    FILE *file = fopen(full_path, "w");
    if (!file)
    {
      perror("Error opening file for writing");
      write(client_socket, "Failed to upload the file.\n", 27);
      return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
    {
      fwrite(buffer, sizeof(char), bytes_received, file);
      if (bytes_received < sizeof(buffer))
      {
        break;
      }
    }

    if (bytes_received < 0)
    {
      perror("Error reading file content");
    }
    else
    {
      printf("File content received successfully.\n");
    }

    fclose(file);
    write(client_socket, "File uploaded successfully.\n", 28);
  }
  else if (is_txt_file(filename))
  {
    // Forward .txt file to Stext server
    process_txt_file(client_socket, filename, destination_path);
  }
  else if (is_pdf_file(filename))
  {
    // Forward .txt file to Stext server
    process_pdf_file(client_socket, filename, destination_path);
  }
  else
  {
    // Forward request to Spdf or other servers if needed
    write(client_socket, "Unsupported file type.\n", 23);
  }
}

void process_dfile_command(int client_socket, char *filepath)
{
  printf("Processing 'dfile' command...\n");

  char home_dir[BUFFER_SIZE];
  char full_path[BUFFER_SIZE];

  snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());

  // Replace '~' with home directory path
  if (filepath[0] == '~')
  {
    snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, filepath + 1);
  }
  else
  {
    snprintf(full_path, sizeof(full_path), "%s", filepath);
  }

  // Check if the file exists and is readable
  if (access(full_path, R_OK) == -1)
  {
    perror("Error accessing file");
    write(client_socket, "File not found or not readable.\n", 32);
    return;
  }

  // Open the file
  FILE *file = fopen(full_path, "rb");
  if (!file)
  {
    perror("Error opening file for reading");
    write(client_socket, "Failed to open the file.\n", 25);
    return;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Send file size to client
  char size_msg[BUFFER_SIZE];
  snprintf(size_msg, sizeof(size_msg), "FILE_SIZE %ld\n", file_size);
  write(client_socket, size_msg, strlen(size_msg));

  // Send file content
  char buffer[BUFFER_SIZE];
  size_t bytes_read;
  while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
  {
    if (send(client_socket, buffer, bytes_read, 0) < 0)
    {
      perror("Error sending file content");
      fclose(file);
      return;
    }
  }

  fclose(file);
  printf("File sent successfully.\n");
}

void process_rmfile_command(int client_socket, char *filepath)
{
  printf("Processing 'rmfile' command...\n");

  char home_dir[BUFFER_SIZE];
  char full_path[BUFFER_SIZE];
  char buffer[BUFFER_SIZE];

  // Get the home directory
  snprintf(home_dir, sizeof(home_dir), "%s", get_home_directory());

  // Replace '~' with home directory path
  if (strncmp(filepath, "~", 1) == 0)
  {
    snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, filepath + 1);
  }
  else
  {
    snprintf(full_path, sizeof(full_path), "%s", filepath);
  }

  // Check file type and forward request to the appropriate server
  if (is_txt_file(filepath))
  {
    // Prepare the command to forward
    snprintf(buffer, sizeof(buffer), "rmfile %s", filepath);
    forward_to_server(STEXT_SERVER_IP, STEXT_SERVER_PORT, buffer);
    write(client_socket, "File removal command forwarded to Stext server.\n", 49);
  }
  else if (is_pdf_file(filepath))
  {
    // Prepare the command to forward
    snprintf(buffer, sizeof(buffer), "rmfile %s", filepath);
    forward_to_server(STEXT_SERVER_IP, SPDF_SERVER_PORT, buffer);
    write(client_socket, "File removal command forwarded to Spdf server.\n", 48);
  }
  else
  {
    // Check if the file exists and is writable
    if (access(full_path, W_OK) == -1)
    {
      perror("Error accessing file");
      write(client_socket, "File not found or not writable.\n", 32);
      return;
    }

    // Attempt to remove the file
    if (remove(full_path) == 0)
    {
      printf("File removed successfully.\n");
      write(client_socket, "File removed successfully.\n", 28);
    }
    else
    {
      perror("Error removing file");
      write(client_socket, "Failed to remove the file.\n", 27);
    }
  }
}

void handle_list_command(int client_socket, const char *path)
{
  DIR *dir;
  struct dirent *ent;
  char *response = NULL;
  size_t response_size = 0;
  size_t response_length = 0;

  dir = opendir(path);
  if (dir != NULL)
  {
    while ((ent = readdir(dir)) != NULL)
    {
      if (ent->d_type == 8)
      { // If it's a regular file
        size_t name_length = strlen(ent->d_name);

        // Resize response buffer to fit the new file name and a newline
        response_size = response_length + name_length + 2; // +1 for '\n' and +1 for '\0'
        response = realloc(response, response_size);

        if (response == NULL)
        {
          perror("Memory allocation failed");
          const char *error_msg = "Memory allocation failed\n";
          write(client_socket, error_msg, strlen(error_msg));
          closedir(dir);
          return;
        }

        // Append the file name and a newline to the response
        strncpy(response + response_length, ent->d_name, name_length);
        response_length += name_length;
        response[response_length] = '\n';
        response_length++;
      }
    }
    closedir(dir);

    if (response_length > 0)
    {
      response[response_length] = '\0'; // Null-terminate the string
      write(client_socket, response, response_length);
    }
    else
    {
      const char *no_files_msg = "No files found in directory\n";
      write(client_socket, no_files_msg, strlen(no_files_msg));
    }

    free(response); // Free allocated memory
  }
  else
  {
    perror("Could not open directory");
    const char *error_msg = "Could not open directory\n";
    write(client_socket, error_msg, strlen(error_msg));
  }
}

void handle_display_command(int client_socket, const char *path)
{
  char smain_files[BUFFER_SIZE * 3] = "";

  char stext_files[BUFFER_SIZE] = "";
  char spdf_files[BUFFER_SIZE] = "";

  char full_path[BUFFER_SIZE];
  snprintf(full_path, sizeof(full_path), "%s/%s", get_home_directory(), path + 1); // Skip initial ~

  // Call handle_list_command to list files on smain server
  // handle_list_command(client_socket, full_path); // Already implemented to handle smain files
  list_files_in_directory(full_path, smain_files, sizeof(smain_files));

  // Call other servers for their files in the same path
  get_files_from_server(STEXT_SERVER_IP, STEXT_SERVER_PORT, path, stext_files, sizeof(stext_files));
  get_files_from_server(STEXT_SERVER_IP, SPDF_SERVER_PORT, path, spdf_files, sizeof(spdf_files));

  // Combine and send the result
  char result[BUFFER_SIZE * 3];
  snprintf(result, sizeof(result), "Files in %s:\nSmain:\n%s%s%s", path, smain_files, stext_files, spdf_files);

  write(client_socket, result, strlen(result));
}

void list_files_in_directory(const char *path, char *output, size_t output_size)
{
  DIR *dir;
  struct dirent *ent;
  if ((dir = opendir(path)) != NULL)
  {
    while ((ent = readdir(dir)) != NULL)
    {
      if (ent->d_type == 8)
      { // Regular file
        strncat(output, ent->d_name, output_size - strlen(output) - 1);
        strncat(output, "\n", output_size - strlen(output) - 1);
      }
    }
    closedir(dir);
  }
  else
  {
    perror("Could not open directory");
  }
}
void get_files_from_server(const char *server_ip, int server_port, const char *path, char *response, size_t response_size)
{
  int sockfd;
  struct sockaddr_in servaddr;

  // Create socket
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("Socket creation failed");
    return;
  }

  memset(&servaddr, 0, sizeof(servaddr));

  // Set server address
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(server_port);
  inet_pton(AF_INET, server_ip, &servaddr.sin_addr);

  // Connect to the server
  if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
  {
    perror("Connection failed");
    close(sockfd);
    return;
  }

  // Send the display command
  char command[BUFFER_SIZE];
  snprintf(command, sizeof(command), "display %s\n", path);
  send(sockfd, command, strlen(command), 0);

  // Receive the file list from the server
  int n = recv(sockfd, response, response_size - 1, 0);
  if (n > 0)
  {
    response[n] = '\0'; // Null-terminate the response
  }
  else
  {
    strncpy(response, "Error receiving data\n", response_size);
  }

  close(sockfd);
}

// Process .txt file by communicating with Stext server
void process_txt_file(int client_socket, char *filename, char *destination_path)
{

  int sockfd;
  struct sockaddr_in serv_addr;

  // Create a socket to connect to Stext server
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Error opening socket to Stext server");
    write(client_socket, "Failed to forward file to Stext server.\n", 40);
    return;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(STEXT_SERVER_PORT);
  serv_addr.sin_addr.s_addr = inet_addr(STEXT_SERVER_IP);

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    perror("Error connecting to Stext server");
    close(sockfd);
    write(client_socket, "Failed to forward file to Stext server.\n", 40);
    return;
  }
  printf("d path: %s\n", destination_path);

  // Forward command to Stext server
  char command[BUFFER_SIZE];
  snprintf(command, sizeof(command), "ufile %s %s", filename, destination_path);

  printf("%s \n ", command);
  if (send(sockfd, command, strlen(command), 0) < 0)
  {
    perror("Error forwarding command to Stext server");
    close(sockfd);
    send(client_socket, "Failed to forward file to Stext server.\n", 40, 0);
    return;
  }

  // Send a delimiter to indicate the end of the command
  const char *delimiter = "\r\n\r\n"; // You can use any unique sequence
  if (send(sockfd, delimiter, strlen(delimiter), 0) < 0)
  {
    perror("Error sending delimiter to Stext server");
    close(sockfd);
    send(client_socket, "Failed to forward file to Stext server.\n", 40, 0);
    return;
  }

  // Forward file content to Stext server
  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;
  while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
  {
    fwrite(buffer, sizeof(char), bytes_received, stdout);
    printf("\n");
    if (send(sockfd, buffer, bytes_received, 0) < 0)
    {
      perror("Error forwarding file content to Stext server");
      close(sockfd);
      write(client_socket, "Failed to forward file to Stext server.\n", 40);
      return;
    }
    if (bytes_received < sizeof(buffer))
    {
      // This indicates the end of the file content
      break;
    }
  }

  if (bytes_received < 0)
  {
    perror("Error reading file content from client");
    write(client_socket, "Error reading file content.\n", 28);
  }
  else
  {
    // printf("[DEBUG] File content forwarded to Stext server successfully.\n");
  }

  close(sockfd);
}

void process_pdf_file(int client_socket, char *filename, char *destination_path)
{

  int sockfd;
  struct sockaddr_in serv_addr;

  // Create a socket to connect to Spdf server
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    perror("Error opening socket to Spdf server");
    write(client_socket, "Failed to forward file to Spdf server.\n", 40);
    return;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(SPDF_SERVER_PORT);
  serv_addr.sin_addr.s_addr = inet_addr(STEXT_SERVER_IP);

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    perror("Error connecting to Spdf server");
    close(sockfd);
    write(client_socket, "Failed to forward file to Spdf server.\n", 40);
    return;
  }
  printf("d path: %s\n", destination_path);

  // Forward command to Spdf server
  char command[BUFFER_SIZE];
  snprintf(command, sizeof(command), "ufile %s %s", filename, destination_path);

  printf("%s \n ", command);
  if (send(sockfd, command, strlen(command), 0) < 0)
  {
    perror("Error forwarding command to Spdf server");
    close(sockfd);
    send(client_socket, "Failed to forward file to Spdf server.\n", 40, 0);
    return;
  }

  // Send a delimiter to indicate the end of the command
  const char *delimiter = "\r\n\r\n"; // You can use any unique sequence
  if (send(sockfd, delimiter, strlen(delimiter), 0) < 0)
  {
    perror("Error sending delimiter to Spdf server");
    close(sockfd);
    send(client_socket, "Failed to forward file to Spdf server.\n", 40, 0);
    return;
  }

  // Forward file content to Spdf server
  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;
  while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0)
  {
    fwrite(buffer, sizeof(char), bytes_received, stdout);
    printf("\n");
    if (send(sockfd, buffer, bytes_received, 0) < 0)
    {
      perror("Error forwarding file content to Spdf server");
      close(sockfd);
      write(client_socket, "Failed to forward file to Spdf server.\n", 40);
      return;
    }
    if (bytes_received < sizeof(buffer))
    {
      // This indicates the end of the file content
      break;
    }
  }

  close(sockfd);
}

// Check if the file is a .c file
int is_c_file(const char *filename)
{
  const char *dot = strrchr(filename, '.');
  return dot && strcmp(dot, ".c") == 0;
}

// Check if the file is a .txt file
int is_txt_file(const char *filename)
{
  return (strstr(filename, ".txt") != NULL);
}

int is_pdf_file(const char *filename)
{
  return (strstr(filename, ".pdf") != NULL);
}

// Create directory path if it doesn't exist
void create_directory_path(const char *path)
{
  char temp[BUFFER_SIZE];
  snprintf(temp, sizeof(temp), "%s", path);
  for (char *p = temp + 1; *p; p++)
  {
    if (*p == '/')
    {
      *p = '\0';
      mkdir(temp, S_IRWXU);
      *p = '/';
    }
  }
  mkdir(temp, S_IRWXU);
}