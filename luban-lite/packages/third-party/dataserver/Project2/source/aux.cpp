#include "../header/aux.h"
#include <dirent.h>
using namespace std;

// create a listening socket
int create_listen(int port, int *server_fd, struct sockaddr_in *address){
    int opt = 1;
 
    // Creating socket file descriptor
    if ((*server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        return -1;
    }
 
    // Forcefully attaching socket to the port 8080
    if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        return -1;
    }
    (*address).sin_family = AF_INET;
    (*address).sin_addr.s_addr = INADDR_ANY;
    (*address).sin_port = htons(port);
 
    // Forcefully attaching socket to the port 8080
    if (bind(*server_fd, (struct sockaddr*)address, sizeof(*address)) < 0) {
        perror("bind failed");
        return -1;
    }
    if (listen(*server_fd, 3) < 0) {
        perror("listen");
        return -1;
    }

    return 1;
}

// from listening socket accept a new connection
int create_connection(int port, int* server_fd, struct sockaddr_in* address){
    int new_socket;
    int addrlen = sizeof(*address);

    if ((new_socket = accept(*server_fd, (struct sockaddr*)address, (socklen_t*)&addrlen)) < 0) {
        perror("accept");
        return -1;
    }

    return new_socket;
}

// connect to server (client)
int connect_to_server(int port, string ip, int* client_fd){
    int sock = 0;
    struct sockaddr_in serv_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("\n Socket creation error \n");
        return -1;
    }
 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
 
    // Convert IPv4 and IPv6 addresses from text to binary
    if (inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }
 
    if ((*client_fd = connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) < 0) {
        printf("\nConnection Failed \n");
        return -1;
    }

    return sock;
}

// read arguments (server)
int read_args(int argc,char** argv,int* port, int* thread_pool_size, int* q_size, int* block_size){
    // checking if number of arguments is right
    if(argc != 9)
        return -1;
    // getting arguments
    for(int i = 1; i < argc; i = i+2){
        if (strcmp(argv[i], "-p") == 0){
            *port = atoi(argv[i+1]);
        }else if(strcmp(argv[i], "-s") == 0){
            *thread_pool_size = atoi(argv[i+1]);
        }else if(strcmp(argv[i], "-q") == 0){
            *q_size = atoi(argv[i+1]);
        }else if(strcmp(argv[i], "-b") == 0){
            *block_size = atoi(argv[i+1]);
        }else{
            return -1;
        }
    }    
    return 0;
}

// read arguments client
int read_args_client(int argc,char** argv, int* port, string* ip,string* directory){
    // checking if number of arguments is right
    if(argc != 7)
        return -1;

    // getting arguments
    for(int i = 1; i < argc; i = i+2){
        if (strcmp(argv[i], "-p") == 0){
            *port = atoi(argv[i+1]);
        }else if(strcmp(argv[i], "-i") == 0){
            *ip = string(argv[i+1]);
        }else if(strcmp(argv[i], "-d") == 0){
            *directory = string(argv[i+1]);
        }
    }    
    return 0;
}


// get files info from given path RECURSIVELY (server)
int get_files_info(char* path, fpath** parent, int depth){
    // depth of file in dir
    depth++;
    DIR *dir, *dir2;
    struct dirent *ent;
    string sub = path;

    fpath* prev = NULL;
    fpath* current;

    // checking if it is a directory
    if ((dir = opendir(path)) != NULL) {
        // while reading files/files in directory
        while ((ent = readdir (dir)) != NULL) {
            // ignore the hidden . and .. files
            if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) // ignoring "." and ".." they are not really files
                continue;

            // get name of file
            string name(ent->d_name);
            // check if it is a directory
            if((dir2 = opendir((sub+"/"+string(ent->d_name)).c_str())) != NULL){ // checking if that name is also a directory *RECURSION*
                closedir(dir2);
                // creating a new fpath node as folder
                current = new fpath(sub+"/"+name, name, depth, *parent, true);
                // recursively get the files info in the found subfolder
                get_files_info((char*)(sub+"/"+(ent->d_name)).c_str(), &current, depth);
            }else{
                // creating a new fpath node as file
                current = new fpath(sub+"/"+name, name, depth, *parent, false);
            }

            // checking if the parent has no files inside then set first node
            if((*parent)->get_first() == NULL){
                (*parent)->set_first(current);
            }else{// else then set next node as current
                prev->set_next(current);
            }
            prev = current; // keeping previous to build the chain of files
        }

        closedir (dir);
    } else {
        return -1;
    }
    return 1;

}

// [debug] print all the file directories in given path
void printDirs(fpath* file){
    fpath* current = file->get_first();
    while(current != NULL){
        if(current->get_isDir() == true)
            printDirs(current);
        else{
        cout << current->get_depth() << " - " << current->get_parent()->get_path() << " - " << current->get_name() << " - " << "is DIR: " + to_string(current->get_isDir()) << endl;
        }
        current = current->get_next();
    }
}

// send wanted path to server (client)
void send_path(int sock, char* path){
    // giving BIG buffer size just to be sure path is right
    char buffer[512] = {'\0'};
    memcpy(buffer, path, strlen(path));
    cout << buffer << endl;
    
    if(send_data(sock,(char*)buffer, 512) == -1){
    	cout << "COULDN'T SEND PATH TO SERVER" << endl;
    }
    

}

// get wanted path from client (server)
char* get_path(int sock){
    // giving BIG buffer size just to be sure path is right
    char* buffer;
    string path = "";

    buffer = read_data(sock, 512);
    if(buffer == NULL){
        cout << "ERROR GETTING WANTED PATH" << endl;
        return NULL;
    }

    // getting path from buffer
    for(int i = 0; i < 512; i++){
        if(buffer[i] == '\0')
            break;
        path = path + buffer[i];
    }
    free(buffer);
    path = path + '\0';
    
    // char* to string
    char* newPath = new char[path.size()]; // MIN KSEXASW NA KANW FREE
    memcpy(newPath, (char*)path.c_str(), path.size());
    return newPath;

}

// get block size info from server
void get_info(int sock, int* block){
    char* buffer; // reading one int
    buffer = read_data(sock, sizeof(int));
    if(buffer == NULL){
        cout << "ERROR GETTING BLOCK INFO" << endl;
        return;
    }
    memcpy(block, buffer, sizeof(int));
    free(buffer);
}

// send block size info to client
void send_info(int sock, int block){
    int* block_ptr = &block;
    if(send_data(sock, (char*)(block_ptr), sizeof(int)) == -1){
    	cout << "COULDN'T SEND BLOCK TO CLIENT" << endl;
    	return;
    }

}

// respond to waiting to achieve sync
void respond(int socket){
    char buf[4] = "}#%";
    send(socket, buf, 4, 0);
}

// wait respond to continue
void wait_respond(int socket){
    char buf[4];
    int counter = 0;
    do{
    read(socket, buf, 4);
    counter += 1;
    }while(buf[0] != '}' && buf[1] != '#' && buf[2] != '%' && counter < 100);
}

// [debug] sending all files of given path to client (non multithreaded)
void send_files(int sock, fpath* file, int block){
    if(file->get_parent() == NULL)
        send_file(sock, file, block);

    fpath* current = file->get_first();
    while(current != NULL){
        send_file(sock, current, block);
        if(current->get_isDir() == true)
            send_files(sock, current, block);
        current = current->get_next();
    }
}

// send file to client
void send_file(int socket, fpath* file, int block){
    // get path of file
    string path = file->get_path();
    // this is number of times server will send block of files
    int send_requests;
    if(file->get_isDir()){
        cout << path << endl; // will never go in anymore may as well be deleted
        return;
    }
    FILE *f;
    // opening file to be read
    f = fopen(path.c_str(), "rb");
    // get size of file
    fseek(f,0,SEEK_END);
    int size = ftell(f);

    // sending file path to client
    char info[(file->get_path()).length() + 1];
    strcpy(info, (file->get_path()).c_str());
    
    if(send_data(socket,(char*)info, 512) == -1){
    	cout << "COULDN'T SEND PATH TO CLIENT" << endl;
    	return;
    }
    // send file size to client
    char temp[sizeof(int)];
    memcpy(&temp, &size, sizeof(int));

    if(send_data(socket,(char*)temp, sizeof(int)) == -1){
    	cout << "COULDN'T SEND FILE SIZE TO CLIENT" << endl;
    	return;
    }

    // compute number of blocks to client
    fseek(f, 0, SEEK_SET);
    if(size == 0){
        send_requests = 0;
    }else if(block%size == 0){
        send_requests = size/block;
    }else{
        send_requests = (size/block) + 1;
    }
    // sending send_requests blocks to client
    for(int i = 0; i < send_requests; i++){
        char buffer[block] = {""};
        fseek(f,i*block, SEEK_SET);
        fread(buffer, block, 1, f);
        if(send_data(socket,(char*)buffer, block) == -1){
        	cout << "COULDN'T SEND PACKET TO CLIENT" << endl;
        }

    }
    // close file
    fclose(f);
}

// get file from server
int get_file(int sock, int block){
    // here will be stored the integer for the file size
    char* buffer;
    // here will be stored the path of the file sent by the server
    char* bufferpath;
    int fileSize;
    int read_requests;
    string name = "";
    string path = "";

    // get path of file from server
    bufferpath = read_data(sock, 512);
    if(bufferpath == NULL){
        return -1;
    }

    // convert bufferpath from char* to string
    for(int i = 0; i < 512; i++){
        if(bufferpath[i] == '\0')
            break;
        path = path + bufferpath[i];
    }

    free(bufferpath);

    int i;
    // getting the position where the name of the file starts from the whole path
    for(i = path.size()-1; i > 0; i--){ // maybe >= 0
        if(path[i] == '/')
            break;
    }
    // convert the name to string
    name = path.substr(i+1, path.size()-1);
    // keep the rest of the path without the name
    path = path.substr(0, i);
    
    // get the file size
    buffer = read_data(sock, sizeof(int));
    if(buffer == NULL){
        cout << "ERROR GETTING FILESIZE" <<  endl;
    }

    memcpy(&fileSize, buffer, sizeof(int));
    free(buffer);
    // compute the number of read requests from server
    if(fileSize == 0){
        read_requests = 0;
    }else if(block%fileSize == 0){
        read_requests = fileSize/block;
    }else{
        read_requests = (fileSize/block) + 1;
    }

    FILE *f2;
    
    // removing all the "../" so we can save the file in current directory and 
    // not lose it somewhere in our system!!!
    while(path[0] == '.'){
        int pos = 0;
        while(path[pos] == '.'){
            pos++;
        }
        path.replace(0,pos+1, "");
    }
    
    // creating the path for the file to be saved in
    path = "results/" + path;
    // check if file exists, if yes remove it
    if((f2 = fopen((char*)(path + "/" + name).c_str(), "rb"))){
        fclose(f2);
        remove((path + "/" + name).c_str());
    }

    // create directories/folders from wanted path if they don't exist already
    createDirs(path);
    
    // open file to write
    f2 = fopen((char*)(path + "/" + name).c_str(), "wb");
    
    // read the data from the server by block_size and write it on the file
    for(int i = 0; i < read_requests; i++){
        // initialize buffer
        char* b;
        b = read_data(sock, block);
        if(b == NULL){
            cout << "MISSED PACKET - EXITING" << endl;
            return -1;
        }

        // this is the last iteration, so just get
        // the leftover bytes from the buffer and break
        if(read_requests-1 == i){
            int left = fileSize%block;
            if(left != 0){
                char last[left];
                memcpy(last,b,left);
                fwrite(last, left, 1, f2);
                break;
            }
        }
        // write bytes to file
        fwrite(b, block, 1, f2);
        free(b);
    }
    // close file
    fclose(f2);
    cout << "Received: " << path + "/" + name << endl;
    return 1;
}

// create directories(folders) if they don't exist
void createDirs(string path){
    // counter to check how many folders there are on the path
    int counter = 0;
    char wanted = '/'; // this character shows that there are subfolders in the path
    for(int i = 0; i< (int)(path.length()); i++){
        if(path[i] == wanted)
            counter++;
    }
    int pos[counter];
    int index = 0;
    // get index of "/" character in the path and save it on pos[counter]
    for(int i = 0; i< (int)(path.length()); i++){
        if(path[i] == wanted){
            pos[index] = i;
            index++;
        }

    }

    struct stat buf; // changed it from pointer to real object (for compilation warnings)
    // now having all the positions in the path where "/" exists
    // check if path exists and create it if it doesn't
    // for instance path = "a/b/c/" check if a/ exists, if a/b/ exists/ if a/b/c/ exists
    for(int i = 0; i < counter; i++){
        string newpath = path.substr(0,pos[i]); 
        
        if(stat((char*)newpath.c_str(), &buf) == -1){
            mkdir((char*)newpath.c_str(), 0700);
        }
    }

    if(stat((char*)path.c_str(), &buf) == -1){ // checking if directory exists
        mkdir((char*)path.c_str(), 0700); // create directory
    }
}

// push all entries/files to shared queue of threads
void push_entries(int socket ,q** file_q_ptr, fpath* file, pthread_mutex_t& lock, pthread_mutex_t& full, pthread_mutex_t& empty, pthread_mutex_t& print,pthread_cond_t& emp, pthread_cond_t& f, pthread_t id){
    // get first file/folder from parent
    fpath* current = file->get_first();
    // while something exists
    while(current != NULL){
        // check if current is directory
        if(current->get_isDir() == true)
            // push entries of subdirectory RECURSIVELY
            push_entries(socket, file_q_ptr, current, lock, full, empty, print, emp, f, id);
        else{
            // push entry/file into shared queue
            push_entry(socket, file_q_ptr, current, lock, full, empty, print, emp, f, id);
        }
        current = current->get_next();
    }
}

// push entry/file to shared queue of threads
void push_entry(int socket, q** file_q_ptr, fpath* file, pthread_mutex_t& lock, pthread_mutex_t& full, pthread_mutex_t& empty, pthread_mutex_t& print,pthread_cond_t& emp, pthread_cond_t& f, pthread_t id){
    // create a new q_node from the given file
    q_node* new_entry = new q_node(socket, file);
    pthread_mutex_lock(&full);
    while(1){
        pthread_mutex_lock(&lock);
        if((*file_q_ptr)->size() == (*file_q_ptr)->capacity()){
            pthread_mutex_unlock(&lock);
            pthread_cond_wait(&f,&full);
        }else{
            pthread_mutex_unlock(&lock);
            break;
        }
    }
    pthread_mutex_unlock(&full);
    pthread_mutex_lock(&lock);
    pthread_mutex_lock(&print);
    cout << "[Thread: " << id << "]: Adding file " << new_entry->get_file()->get_path() << " to the queue..." << endl;
    pthread_mutex_unlock(&print);
    // push file into shared queue SAFELY
    (*file_q_ptr)->push(new_entry);
    pthread_mutex_unlock(&lock);
    pthread_cond_broadcast(&emp);


}

// most important job of worker functions - sending file to client
void work(q_node* entry, int block){
    // from q_node get path and socket
    int socket = entry->get_sock();
    fpath* file = entry->get_file();
    send_file(socket, file, block);
}

// checking if specific socket is busy by another thread
bool is_busy(q_node* entry, int* busy, int size){
    // for debug
    if(entry == NULL){
        cout << "RIP" << endl;
    }

    // check all positions in busy[worker_threads_number]
    // and check if a worker is busy with specific socket
    int sock = entry->get_sock();
    for(int i = 0; i < size; i++){
        if(busy[i] == sock)
            return true;
    }

    return false;

}

// [debug] printing all file names in queue
void printq(q* files){
    cout << "~QUEUE PRINT~" << endl;
    while(files->front() != NULL){
        string entry = files->front()->get_file()->get_name();
        cout << entry << endl;
        files->pop();
    }
    cout <<"~~~~~~~~~~~~~~~~~~" << endl;
}

// getting number of files in wanted directory by client
void num_of_files(int* counter, fpath* file){
    // iterate through the whole fpath nodes
    fpath* current = file->get_first();
    while(current != NULL){
        if(current->get_isDir() == true) // checking if it is a directory
            num_of_files(counter, current); // recursively
        else{
            *counter = *counter + 1; // it is a file so just increment counter
        }
        current = current->get_next(); // get next file/folder
    }

}

// initializing values in vector, specifiacaly the num of files in wanted directory by client
void set_max_in_comm_vector(int sock, int counter, vector<std::array<int, 3>>* vec){
    int i;
    for(i = 0; i < (int)(vec->size()); i++){
        // find wanted sock and set the number of files
        // from the wanted directory
        if((*vec)[i][0] == sock){
            (*vec)[i][1] = counter;
            break;
        }
    }
    return;
}

char* read_data(int sock, int size){
    int read_bytes = 0;
    int total_bytes = 0;
    
    char* b = new char[size]();
    for(int i = 0; i < size; i++){
        b[i] = '\0';
    }

    while(total_bytes < size){
        read_bytes = read(sock, b+total_bytes, size - total_bytes);

        if (read_bytes == -1 || read_bytes == 0)
        {
            return NULL;
        }
        total_bytes += read_bytes;    
    }
    respond(sock);
    return b;
}

int send_data(int sock, char* buf, int size){
    int sent_bytes = 0;
    int total_bytes = 0;
    while(total_bytes < size){
        sent_bytes = send(sock, buf+total_bytes, size - total_bytes,0);
        wait_respond(sock);
        if (sent_bytes == -1)
        {
            return -1;
        }
        total_bytes += sent_bytes;    
    }
    
    return 1;
}
