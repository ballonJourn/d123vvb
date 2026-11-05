#ifndef QN_H
#define QN_H

#include <string>
#include "fpath.h"

using namespace std;

class q_node{
    private:
        q_node* next_node; // next node
        int sock; // socket to serve
        fpath* file; // file
        bool last; // is last node (not used)
    public:
        // constructor
        q_node(int, fpath*);
        // get file (whole fpath*)
        fpath* get_file();
        // get socket
        int get_sock();
        // get next node
        q_node* next();
        bool is_last();


        // set if it's last file (not used in the end)
        void set_last(bool);
        // set next node
        void set_next(q_node*);
};

#endif
