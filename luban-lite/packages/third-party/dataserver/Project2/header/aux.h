#ifndef AUX_H
#define AUX_H

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <vector>
#include "fpath.h"
#include <fstream>
#include <sys/stat.h>
#include "q.h"
#include <array>
#include <pthread.h>

using namespace std;


// connect to server (client)
int connect_to_server(int port, string ip, int* client_fd);
// from listening socket accept a new connection
int create_connection(int port, int* server_fd, struct sockaddr_in*);
// create a listening socket
int create_listen(int port, int* server_fd, struct sockaddr_in*);
// read arguments (client)
int read_args_client(int argc,char** argv,int* port, string* ip, string* dir);
// read arguments (server)
int read_args(int argc,char** argv,int* port, int* thread_pool_size, int* q_size, int* block_size);
// get files info from given path RECURSIVELY (server)
int get_files_info(char* path, fpath** , int);
// send wanted path to server (client)
void send_path(int,char* path);
// get wanted path from client (server)
char* get_path(int);
// get block size info from server
void get_info(int,int*);
// send block size info to client
void send_info(int, int);
// get file from server
int get_file(int, int);
// [debug] sending all files of given path to client (non multithreaded)
void send_files(int,fpath*, int);
// send file to client
void send_file(int,fpath*, int);

int send_data(int sock, char* buf, int size);
char* read_data(int sock, int size);
// [debug] print all the file directories in given path
void printDirs(fpath*);
// create directories(folders) if they don't exist
void createDirs(string);

// push all entries/files to shared queue of threads
void push_entries(int socket ,q** file_q_ptr, fpath* file, pthread_mutex_t& lock, pthread_mutex_t& full, pthread_mutex_t& empty, pthread_mutex_t&, pthread_cond_t&, pthread_cond_t&, pthread_t);
// push entry/file to shared queue of threads
void push_entry(int socket ,q** file_q_ptr, fpath* file, pthread_mutex_t& lock, pthread_mutex_t& full, pthread_mutex_t& empty, pthread_mutex_t&, pthread_cond_t&, pthread_cond_t&, pthread_t);
// most important job of worker functions - sending file to client
void work(q_node*, int);
// checking if specific socket is busy by another thread
bool is_busy(q_node*, int* , int);
// [debug] printing all file names in queue
void printq(q*);
// respond to waiting to achieve sync
void respond(int);
// wait respond to continue
void wait_respond(int);
// getting number of files in wanted directory by client
void num_of_files(int*, fpath*);
// initializing values in vector, specifiacaly the num of files in wanted directory by client
void set_max_in_comm_vector(int, int, vector<std::array<int, 3>>*);
#endif