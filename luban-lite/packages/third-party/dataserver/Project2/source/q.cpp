#include "../header/q.h"
#include "../header/aux.h"

using namespace std;

// constructor
q::q(int capacity){
    this-> front_node = NULL;
    this-> back_node = NULL;
    this->capacityq = capacity;
    this->sizeq = 0;
}

// get front element of queue
q_node* q::front(){
    return this->front_node;
}

// get back element of queue
q_node* q::back(){
    return this->back_node;
}

// get current size of queue
int q::size(){
    return this->sizeq;
}

// get capacity of queue
int q::capacity(){
    return this->capacityq;
}

// check if queue is empty
bool q::empty(){
    return this->sizeq == 0;
}

// remove front node from queue
void q::pop(){
    if(this->sizeq == 0)
        return;
    else if(this->sizeq == 1){
        this->front_node = NULL;
        this->back_node = NULL;
        this->sizeq = 0;
        return;
    }
    this->front_node = this->front_node->next();
    this->sizeq = this->sizeq - 1;
    //destroy temp
}

// push new node to the back of the queue
void q::push(q_node* node){
    if(this->sizeq == 0){
        this->back_node = node;
        this->front_node = node;
        this->sizeq = 1;
    }else{
        node->set_next(NULL);
        this->back_node->set_next(node); 
        this->back_node = node;
        this->sizeq = this->sizeq + 1;
    }


}