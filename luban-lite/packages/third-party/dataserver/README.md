# System Programming Assignment - Data Server & Remote Client

## Overview
This project entails the complete implementation of a data server and a remote client. The data server initializes and synchronizes threads and structures to serve client requests, while the client is a simple program designed to connect to the server and request files.

### Server Operation
#### SERVER
1. The server starts and reads command-line arguments.
2. Initializes the necessary mutexes for thread synchronization and other structures and variables.
3. Creates a listening socket to accept new client connections.
4. Initializes worker threads responsible for sending files to clients. These threads are given an index argument that corresponds to their position in the 'busy' array, which tracks whether a socket is occupied by a thread.
5. Enters an infinite loop, accepting new connections and creating new communication threads that read the requested files and place them in a custom queue.

#### COMMUNICATION THREAD
When the server accepts a new connection, it creates a new communication thread:
1. Sends the block_size to the client and receives the requested path.
2. Uses a custom fpath class to create a structure of file information (path, whether it's a folder, name, etc.) without storing file data.
3. Finds the number of files in the requested path.
4. Maintains a vector recording the form [current_socket, number_of_files, files_served], managing which sockets have been fully served to safely close connections.
5. Adds q_nodes to the queue, which activates workers via mutexes to start serving clients.

#### WORKER THREAD
Worker threads wait in an endless loop for a node to enter the shared queue. Once activated:
1. Takes control of the queue with mutexes.
2. Retrieves the first element from the queue.
3. Checks if the associated socket is busy.
4. If the socket is busy, moves the node to the back of the queue and repeats step 2.
5. If the socket is not busy, marks it as busy in the 'busy' array at the given index.
6. Sends file information and then the file itself to the client in block_size chunks.
7. Increments the 'files served' counter and closes the connection when all files are served.

The above process repeats continuously.

### Client Operation
#### The client:

1. Reads command-line arguments.
2. Attempts to connect to the server.
3. Receives the block_size from the server.
4. Sends the desired path to the server.
5. Continuously reads files until the server has sent all.
6. Closes the connection.

#### Classes
- q: A classic queue implementation.
- q_node: A node in the q class containing necessary elements like socket and file (fpath).
- fpath: A comprehensive class holding directory and subdirectory details, forming a tree structure.

## Auxiliary Functions (aux.cpp/h)
Contains all helper functions used by the client and server, fully commented.

Two key functions are highlighted: get_file() and send_file():

- get_file(): Reads a file from a path, creates a new file, and writes data to it in block_size chunks from the socket.
- send_file(): Sends a file to the client in block_size chunks, providing the file size initially.

## Compilation and Sample Execution Commands:
Compile the programs with make.

Example execution:
```bash
./dataServer -p 8080 -s 20 -q 10 -b 512
./remoteClient -p 8080 -i 127.0.0.1 -d from/dir
```

Note: The PATH of the directory should not start with "./" or "/", but directly with the name, e.g., "from/dir".

When the client downloads files, a "results" directory is created to store them to avoid overwriting existing files when running the server and client from the same directory.

## Prerequisite
A testing program has been created to test multi-client functionality. It creates 10 clients through fork connecting to the server.

To use the testing program, compile with g++ test.cpp and run ./a.out while the SERVER IS RUNNING ON THE LOCAL MACHINE!

## License

This project is for educational use only and is part of the coursework for _Κ24 System Programming_ at _DiT, NKUA_.
