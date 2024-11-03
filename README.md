
# Distributed File System (DFS)

**Distributed File System** implemented using **C** and **socket programming** on **Linux**. This project uses a **three-server architecture** to transparently distribute files across multiple specialized servers, efficiently managing file operations based on file types and supporting multiple concurrent clients.

## Table of Contents

- [Project Description](#project-description)
- [System Architecture](#system-architecture)
- [Features](#features)
- [How to Run the Project](#how-to-run-the-project)
- [How the System Works](#how-the-system-works)
- [Supported Operations](#supported-operations)
- [Notes](#notes)

## Project Description

The **Distributed File System (DFS)** consists of:
- A **main server (smain)** that interacts with users and routes requests internally.
- Two **specialized servers**, **spdf** and **stext**, which manage **PDF** and **text** files, respectively.

Users interact only with **smain**, which routes requests to the appropriate specialized servers based on file type, providing a **transparent and efficient** experience. The system supports various file operations, including:
- **Upload**
- **Download**
- **Removal**
- **Archive creation**

It is designed for **multiple concurrent clients** to ensure optimal performance and scalability.


## System Architecture

The DFS architecture comprises:

1. **smain**: The main server that acts as the primary user interface and routes requests to the specialized servers.
2. **spdf**: Specialized server for handling **PDF (.pdf)** files.
3. **stext**: Specialized server for managing **text (.txt)** files.

## Features

- **Transparent file distribution**: Files are distributed based on type without user intervention.
- **Concurrent client handling**: Supports multiple clients simultaneously to improve response times.
- **File operations**: Supports **upload**, **download**, **remove**, and **archive (tar)**.
- **Efficient load balancing**: Routes requests to specialized servers for **PDF** and **text** files, reducing load on the main server.
- **Scalability**: Designed to handle increasing clients and server instances.
- **Data integrity**: Ensures file consistency across servers.

## How to Run the Project

### Prerequisites

- Linux environment
- GCC (GNU Compiler Collection)


### Steps to Run

## Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/shreysachani/DistributedFileSystem.git
   ```

2. Open new terminal and switch to client directory
   ```bash
   cd client
   ```
   
3. Change permission for compile.sh
   ```bash
   sudo chmod +x compile.sh
   ```
   
4. Run compile.sh:
   ```bash
   ./compile.sh
   ```
   
5. Start the specialized servers: Open two separate terminal windows and run the following:
   ```bash
   ./spdf
   ./stext
   ```

6. Start the main server: Open another terminal window and run:
   ```bash
   ./smain
   ```

7. Run the client: In a separate terminal window, execute the client program:
   ```bash
   ./client
   ```

## How the System Works

### Client-Server Communication

- Clients communicate exclusively with **Smain**, submitting requests for file operations.

### Concurrent Client Handling

- **smain** forks a new process for each client request, allowing concurrent processing.

### File Type-based Distribution

- **smain** forwards requests based on file type:
  - **.pdf** requests are handled by **Spdf**.
  - **.txt** requests go to **Stext**.
  - **.c** files are processed directly by **Smain**.

### On-Demand Connections

- **smain** only connects with **spdf** and **stext** when needed, optimizing resource usage.

### Request Queueing

- While processing, **smain** continues to listen and queue new client requests.


## Supported Operations

| Command  | Description                                                          |
|----------|----------------------------------------------------------------------|
| `ufile`  | Uploads a file to the system based on its type (.c, .pdf, .txt)      |
| `dfile`  | Downloads a file from the system                                     |
| `rmfile` | Removes a file from the system                                       |
| `dtar`   | Creates and downloads a tar archive of specified file types          |
| `display`| Lists files in a specified directory                                 |

## Example Commands

- To upload a file:

```bash
ufile <file-path>
```

- To download a file:

```bash
dfile <file-name>
```

- To remove a file:

```bash
rmfile <file-name>
```

- To create and download a tar archive of all `.pdf` files:

```bash
dtar pdf
```

- To display the files in a directory:

```bash
display <directory-path>
```


## Notes

This project was developed as part of the **COMP-8567** course to showcase concepts in **distributed systems**, **socket programming**, and **efficient file management**. While designed for educational purposes, it can be expanded to support additional file operations and server types.

