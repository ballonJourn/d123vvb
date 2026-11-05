#include "../header/aux.h"
 
using namespace std;

int main(int argc, char** argv)
{
    int sock = 0, client_fd; // socket number and client fd
    int block_size; // block size that server will sent
    int port; // port to connect to
    string dir, ip; // directory wanted and ip client connects

    // get arguments from command line
    read_args_client(argc, argv, &port, &ip, &dir);
    cout << "Client's parameters are:" << endl;
    cout << "ServerIP: " << ip << endl;
    cout << "port: " << port << endl;
    cout << "directory: " << dir << endl << endl;

    cout << "Connecting to " << ip << " on port " << port << endl;

    // connect to server!
    sock = connect_to_server(port, ip, &client_fd);

    // get block size so client know how much to read
    get_info(sock,&block_size);
    // cout << block_size << endl;
    
    // send wanted path to server
    send_path(sock, (char*)dir.c_str());

    // get files until finished
    while(get_file(sock,block_size) == 1);
    
    cout << "CONNECTION CLOSED" << endl;
    // closing the connected socket
    close(client_fd);
    return 0;
}