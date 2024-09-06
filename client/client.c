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

#define BUFFER_SIZE 1024

// Function prototypes
int connect_to_server(const char *server_ip, int server_port);
int validate_ufile_command(char *filename, char *destination_path);
int validate_dfile_command(const char *destination_path);
int validate_rmfile_command(const char *destination_path);
int validate_display_command(const char *display_path);
int validate_filename_extension(const char *filename);
void receive_file_list(int sockfd);
void error_message(const char *msg);
int file_exists(const char *filename);
void send_command(int sockfd, const char *command);
void receive_file(int server_socket, const char *filename);

int main()
{
  const char *server_ip = "127.0.0.1"; // Server IP address (localhost)
  int server_port = 7139;              // Server port number
  int sockfd;

  sockfd = connect_to_server(server_ip, server_port);
  if (sockfd < 0)
  {
    error_message("Failed to connect to server");
    return 1;
  }

  char command[256];
  char filename[128];
  char destination_path[128];

  while (1)
  {
    printf("client24s$ ");
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = '\0'; // Remove newline character

    // Check if the command is ufile
    if (sscanf(command, "ufile %s %s", filename, destination_path) == 2)
    {
      if (validate_ufile_command(filename, destination_path))
      {
        send_command(sockfd, command);
        printf("Command sent to server.\n");
      }
    }
    // Check if the command is dfile
    else if (sscanf(command, "dfile %s", destination_path) == 1)
    {
      if (validate_dfile_command(destination_path))
      {
        send_command(sockfd, command);
        printf("Command sent to server.\n");

        // Extract filename from filepath
        char *filename = strrchr(destination_path, '/');
        if (filename == NULL)
        {
          filename = destination_path;
        }
        else
        {
          filename++; // Move past the '/'
        }

        // Receive the file
        receive_file(sockfd, filename);
      }
    }
    // Check if the command is rmfile
    else if (sscanf(command, "rmfile %s", destination_path) == 1)
    {
      if (validate_rmfile_command(destination_path))
      {
        send_command(sockfd, command);
        printf("Command sent to server.\n");
        char *filename = strrchr(destination_path, '/');
        if (filename == NULL)
        {
          filename = destination_path;
        }
        else
        {
          filename++; // Move past the '/'
        }
      }
    }
    else if (strncmp(command, "display ", 8) == 0)
    {
      char display_path[BUFFER_SIZE];
      if (sscanf(command, "display %s", display_path) == 1)
      {
        if (validate_display_command(display_path))
        {
          send_command(sockfd, command);
          printf("Command sent to server.\n");

          // Receive and display the file list
          receive_file_list(sockfd);
        }
      }
    }
    else
    {
      error_message("Invalid command syntax. Use 'ufile filename destination_path', 'dfile filepath', or 'rmfile filepath'");
    }
  }

  close(sockfd);
  return 0;
}

// Connect to the server (Smain)
int connect_to_server(const char *server_ip, int server_port)
{
  int sockfd;
  struct sockaddr_in serv_addr;

  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    error_message("Error opening socket");
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(server_port);
  serv_addr.sin_addr.s_addr = inet_addr(server_ip);

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    error_message("Connection failed");
    close(sockfd);
    return -1;
  }

  printf("Connected to server at %s:%d\n", server_ip, server_port);
  return sockfd;
}

// Validate ufile command
int validate_ufile_command(char *filename, char *destination_path)
{
  // Validate filename extension (.c, .txt, .pdf)
  if (!validate_filename_extension(filename))
  {
    error_message("Invalid file extension. Only .c, .txt, or .pdf files are allowed.");
    return 0;
  }

  // Check if file exists in PWD
  if (!file_exists(filename))
  {
    error_message("File does not exist in the current directory.");
    return 0;
  }

  // Check if destination path starts with "~smain"
  if (strncmp(destination_path, "~smain", 6) != 0)
  {
    error_message("Invalid destination path. It must start with ~smain.");
    return 0;
  }

  return 1;
}

int validate_dfile_command(const char *destination_path)
{
  // Check if filepath is not empty
  if (destination_path[0] == '\0')
  {
    error_message("destination_path cannot be empty");
    return 0;
  }

  return 1;
}
int validate_rmfile_command(const char *destination_path)
{
  // Check if filepath is not empty
  if (destination_path[0] == '\0')
  {
    error_message("destination_path cannot be empty");
    return 0;
  }

  return 1;
}

// Validate file extension (.c, .txt, .pdf)
int validate_filename_extension(const char *filename)
{
  const char *dot = strrchr(filename, '.');
  if (!dot || dot == filename)
    return 0; // No extension found
  if (strcmp(dot, ".c") == 0 || strcmp(dot, ".txt") == 0 || strcmp(dot, ".pdf") == 0)
  {
    return 1;
  }
  return 0;
}
void receive_file(int server_socket, const char *filename)
{
  char buffer[BUFFER_SIZE];
  long file_size;

  // Read file size
  if (read(server_socket, buffer, BUFFER_SIZE) <= 0)
  {
    perror("Error reading file size");
    return;
  }

  if (sscanf(buffer, "FILE_SIZE %ld", &file_size) != 1)
  {
    fprintf(stderr, "Invalid file size received\n");
    return;
  }

  // Open file for writing
  FILE *file = fopen(filename, "wb");
  if (!file)
  {
    perror("Error creating file");
    return;
  }

  // Receive and write file content
  long total_received = 0;
  ssize_t bytes_received;
  while (total_received < file_size &&
         (bytes_received = read(server_socket, buffer, sizeof(buffer))) > 0)
  {
    fwrite(buffer, 1, bytes_received, file);
    total_received += bytes_received;
  }

  fclose(file);

  if (total_received == file_size)
  {
    printf("File received successfully\n");
  }
  else
  {
    fprintf(stderr, "Error receiving file\n");
  }
}

// Check if file exists in the current working directory
int file_exists(const char *filename)
{
  struct stat buffer;
  return (stat(filename, &buffer) == 0);
}

// Print error message to the user
void error_message(const char *msg)
{
  fprintf(stderr, "Error: %s\n", msg);
}

// Send command to the server
void send_command(int sockfd, const char *command)
{
  if (write(sockfd, command, strlen(command)) < 0)
  {
    perror("Failed to send command to server");
    return;
  }

  // Extract command type and filename
  char cmd_type[10];
  char filename[BUFFER_SIZE];
  if (sscanf(command, "%9s %s", cmd_type, filename) != 2)
  {
    fprintf(stderr, "Invalid command format\n");
    return;
  }

  if (strcmp(cmd_type, "ufile") == 0)
  {
    // Handle file upload

    FILE *file = fopen(filename, "rb");
    if (!file)
    {
      perror("Failed to open the file");
      return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
      if (write(sockfd, buffer, bytes_read) < 0)
      {
        perror("Failed to send file content to server");
        break;
      }
    }

    if (ferror(file))
    {
      perror("Error reading from file");
    }

    fclose(file);
    printf("File content sent successfully.\n");
  }
  else if (strcmp(cmd_type, "rmfile") == 0)
  {

    // Wait for server response
    char response[BUFFER_SIZE];
    ssize_t bytes_received = recv(sockfd, response, sizeof(response) - 1, 0);
    if (bytes_received > 0)
    {
      response[bytes_received] = '\0';
    }
  }
  else
  {
    fprintf(stderr, "Unknown command type: %s\n", cmd_type);
  }
}

int validate_display_command(const char *display_path)
{
  // Check if display_path starts with "~smain"
  if (strncmp(display_path, "~smain", 6) != 0)
  {
    error_message("Invalid display path. It must start with ~smain.");
    return 0;
  }

  return 1;
}

void receive_file_list(int sockfd)
{
  char buffer[BUFFER_SIZE];
  ssize_t bytes_received;

  // Clear buffer and receive data
  memset(buffer, 0, BUFFER_SIZE);
  while ((bytes_received = recv(sockfd, buffer, BUFFER_SIZE - 1, 0)) > 0)
  {
    buffer[bytes_received] = '\0';
    printf("%s", buffer);
    // Check for end of file list marker
    if (strstr(buffer, "END_OF_FILE_LIST") != NULL)
    {
      break;
    }
    // Clear buffer for the next read
    memset(buffer, 0, BUFFER_SIZE);
  }
  if (bytes_received < 0)
  {
    error_message("Error receiving file list from server");
  }
}