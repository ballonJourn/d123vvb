#include "../header/aux.h"
#include "../header/fpath.h"
#include <signal.h>

using namespace std;

// making these vars shared between threads
int block_size, new_socket, thread_pool_size;
q* file_q;
pthread_mutex_t q_lock, q_full, q_has_something, q_empty, vec_lock, print;
int* busy;
pthread_cond_t emp, f;

vector<std::array<int, 3>> comm_thread_info;

void* thread_func_worker(void* arg){
  // this is an index when creating the thread
  int index = *((int*) arg);
  // getting thread id
  pthread_t id = pthread_self();

  // infinite loop for worker - waiting for new tasks
  while(1){
    // locking when empty
    pthread_mutex_lock(&q_empty);
    // locking because of reading the file queue
    pthread_mutex_lock(&q_lock);
    
    while(file_q->empty()){
      // unlock now that you have your info
      pthread_mutex_unlock(&q_lock);
      // wait until something is in the queue
      pthread_cond_wait(&emp, &q_empty);
      // lock to see if it's empty or not
      pthread_mutex_lock(&q_lock);
      if(!file_q->empty()){
        break; // not empty - break to go on working on that task
      }
    }

    // still empty - continue waiting but don't hold the queue
    pthread_mutex_unlock(&q_empty); 
    
    q_node* entry;
    bool busyFlag;
    do{
      // get first entry on queue
      entry = file_q->front();
      if(entry == NULL){
        break;
      }else{
        file_q->pop();
        // check if the socket is currently used by another thread
        busyFlag = is_busy(entry,busy, thread_pool_size);
        if(busyFlag){
          // is used now - push entry to the back and get next
          file_q->push(entry);
        }
      }
    }while(busyFlag); // until you find a task with non busy socket

    // just for safety reasons - don't want segmentation faults
    if (entry == NULL){
      pthread_mutex_unlock(&q_lock);
      continue;
    } 

    // lock print for aesthetic and readability reasons
    pthread_mutex_lock(&print);
    cout << "[Thread: " << id << "]: Received task <" << entry->get_file()->get_path() << ", " << entry->get_sock() << ">" << endl;
    pthread_mutex_unlock(&print);
    
    int temp_sock = entry->get_sock();
    
    // this thread is busy with socket temp_sock 
    busy[index] = temp_sock;  

    // if you just popped a file from a full queue tell worker to continue pushing
    if(file_q->size() <= file_q->capacity() - 1)
      pthread_cond_broadcast(&f);

    // finished with queue right now
    pthread_mutex_unlock(&q_lock);


    // lock print for aesthetic and readability reasons
    pthread_mutex_lock(&print);
    cout << "[Thread: " << id << "]: About to read file " << entry->get_file()->get_path() << endl;
    pthread_mutex_unlock(&print);
    
    // here is all the work done (sending file info, the file itself etc)
    work(entry, block_size);
    // FIX: entries (and their file copy) were never freed
    delete entry->get_file();
    delete entry;
    
    // locking to avoid segmentations (data racing)
    pthread_mutex_lock(&vec_lock);
    int i;
    // search all the vector containing [socket, num_of_files, files_sent]
    for(i = 0; i < (int)comm_thread_info.size(); i++){
      if(comm_thread_info[i][0] == temp_sock){  // if wanted socket found
          comm_thread_info[i][2] = comm_thread_info[i][2] + 1; // increase number of files sent
          break;
      }
    }

    // better way to see
    // cout << "Socket -> " << comm_thread_info[i][0] << " -----  " << comm_thread_info[i][2] << "/" << comm_thread_info[i][1] << " ----- "  << "Thread No. -> " << index << endl;

    // if file sent = num_of_files then you sent the whole directory and you are done
    if(comm_thread_info[i][1] == comm_thread_info[i][2]){ 
      close(temp_sock); // closing connection

      // erase the record holding data about that client and his request
      comm_thread_info.erase(comm_thread_info.begin() + i); 
    }
    pthread_mutex_unlock(&vec_lock);
    // done with that task so this thread is done with that socket now
    busy[index] = -1;
  }
  return NULL;
}

void* thread_func_comm(void* arg){
  // getting thread id
  pthread_t id = pthread_self();
  // get socket to serve
  int current_socket = *((int*) arg);
  // send block_size to client so he knows how many bytes to read on the file
  send_info(current_socket, block_size);

  // get path from client
  char* path = get_path(current_socket);
  if(path == NULL){
    close(current_socket);
    delete((int*)arg);
    return NULL;
  }
  // make it a string to help yourself
  // FIX: only paths below the server root are served (no "..", no absolute paths)
  string strPath = resolve_request(string(path));
  delete[] path;
  if(strPath.empty()){
    pthread_mutex_lock(&print);
    cout << "[Thread: " << id << "]: Rejected request outside the server root" << endl;
    pthread_mutex_unlock(&print);
    close(current_socket);
    delete((int*)arg);
    return NULL;
  }
  
  // lock print for aesthetic and readability reasons
  pthread_mutex_lock(&print);
  cout << "[Thread: " << id << "]: About to scan directory " << strPath << endl;
  pthread_mutex_unlock(&print);
  
  // create a structure holding the files info and hierarchy (NOT THE DATA)
  fpath* root = new fpath(strPath, strPath,0, NULL, true);
  get_files_info((char*)strPath.c_str(), &root, root->get_depth());

  // get how many files are in the wanted directory
  int counter = 0;
  num_of_files(&counter, root);

  // FIX: an empty or missing directory never closed the connection and the
  // client waited forever
  if(counter == 0){
    close(current_socket);
    free_tree(root);
    delete((int*)arg);
    return NULL;
  }

  // lock the vector to avoid segmentations and wrong readings (data race)
  pthread_mutex_lock(&vec_lock);
  comm_thread_info.push_back({current_socket, counter, 0});
  pthread_mutex_unlock(&vec_lock);
  
  // push all the entries to file_q
  push_entries(current_socket, &file_q, root, q_lock, q_full, q_empty, print, emp, f, id);
  // FIX: every queued entry owns a copy of its node, so the tree can go now
  free_tree(root);
  delete((int*)arg);

  return NULL;
}





int main(int argc, char** argv)
{

  // INITIALIZE ALL THE NEEDED MUTEXES

  if (pthread_mutex_init(&q_lock, NULL) != 0) {
        printf("\n mutex init LOCK has failed\n");
        return 1;
  }

  if (pthread_mutex_init(&q_full, NULL) != 0) {
        printf("\n mutex init FULL has failed\n");
        return 1;
  }

  if (pthread_mutex_init(&q_empty, NULL) != 0) {
        printf("\n mutex init EMPTY has failed\n");
        return 1;
  }

  if (pthread_mutex_init(&vec_lock, NULL) != 0) {
        printf("\n mutex init VEC LOCK has failed\n");
        return 1;
  }

  if (pthread_mutex_init(&print, NULL) != 0) {
        printf("\n mutex init PRINT has failed\n");
        return 1;
  }


  int port, q_size;
  // ADDED: optional "-r <root>" (directory the clients may read from)
  string root_dir = ".";
  if(argc == 11 && strcmp(argv[9], "-r") == 0){
    root_dir = argv[10];
    argc = 9;
  }
  // get arguments from command line
  if(read_args(argc, argv, &port, &thread_pool_size, &q_size, &block_size) == -1){
    cout << "Wrong arguments!" << endl;
    cout << "usage: dataServer -p <port> -s <thread_pool_size> -q <queue_size> -b <block_size> [-r <root>]" << endl;
    return -1;
  }
  set_server_root(root_dir);
  // FIX: a client that disconnects mid-transfer used to kill the server (SIGPIPE)
  signal(SIGPIPE, SIG_IGN);

  cout << "Server's parameters are:" << endl;
  cout << "port: " << port << endl;
  cout << "thread_pool_size: " << thread_pool_size << endl;
  cout << "queue_size: " << q_size << endl;
  cout << "Block_size: " << block_size << endl;
  cout << "root: " << root_dir << endl;
  

  // create an int[thread_pool_size] which holds if a worker thread is busy
  // with a socket and the value at the index of the thread is the socket he is
  // busy with
  busy = new int[thread_pool_size];
  for(int i = 0; i < thread_pool_size; i++){
    // initialized with -1 (no socket has such value)
    busy[i] = -1;
  }

  // create a new (custom) queue with capacity q_size
  file_q = new q(q_size);

  // create/initialize the listening and get the needed values
  int server_fd;

  struct sockaddr_in address;
  
  if(!create_listen(port, &server_fd, &address)){
    cout << "ERROR" << endl;
    return -1;
  }

  // create the thread pool of workers
  pthread_t threadPool[thread_pool_size];
  for(int i = 0; i < thread_pool_size; i++){
      int* index = new int();
      *index = i;
      // this is the index for busy[]
      pthread_create(&threadPool[i], NULL, thread_func_worker, index);
  }

  cout << "Server was successfully initialized..." << endl;
  cout << "Listening for connections to port " << port << endl;
  while(1){
    // wait until someone connects
    new_socket = create_connection(port, &server_fd, &address);
    if(new_socket < 0) // FIX: accept() errors spawned a thread on fd -1
      continue;
    printf("Accepted connection from %s\n", inet_ntoa(address.sin_addr));
    pthread_t new_thread;
    int* sock = new int();
    *sock = new_socket;
    // create a new communication thread with the new socket as argument
    if(pthread_create(&new_thread, NULL, thread_func_comm, sock) != 0){
      close(new_socket);
      delete sock;
      continue;
    }
    // FIX: comm threads were never joined; detach so their resources are freed
    pthread_detach(new_thread);
    // pthread_join(new_thread, NULL);
  }
  // closing the listening socket
  shutdown(server_fd, SHUT_RDWR);
  return 0;
}