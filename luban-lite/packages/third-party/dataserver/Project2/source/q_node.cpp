#include "../header/q_node.h"
#include "../header/aux.h"

using namespace std;

// constructor
q_node::q_node(int sock, fpath* file){
    this->sock = sock;
    this->file = file;
    this->last = false;

    this->next_node = NULL;
}

// get file (whole fpath*)
fpath* q_node::get_file(){
    return this->file;
}

// get socket
int q_node::get_sock(){
    return this->sock;
}

// get next node
q_node* q_node::next(){
    return this->next_node;
}

// set next node
void q_node::set_next(q_node* node){
    this->next_node = node;
}

// set if it's last file (not used in the end)
void q_node::set_last(bool value){
    this->last = value;
}
