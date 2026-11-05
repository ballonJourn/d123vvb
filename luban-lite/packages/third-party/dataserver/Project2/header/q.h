#ifndef Q_H
#define Q_H

#include <string>
#include "fpath.h"
#include "q_node.h"

using namespace std;

class q{
    private:
        q_node* front_node; // front node of queue
        q_node* back_node; // back node of queue
        int capacityq; // capacity of queue
        int sizeq; // current size of queue
    public:
        // constructor
        q(int);
        // get front element of queue
        q_node* front();
        // get back element of queue
        q_node* back();
        // get current size of queue
        int size();
        // get capacity of queue
        int capacity();
        // check if queue is empty
        bool empty();

        // remove front node from queue
        void pop();
        // push new node to the back of the queue
        void push(q_node*);

};

#endif
