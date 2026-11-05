#include "../header/fpath.h"

// constructor 1
fpath::fpath(string name){
            this->name = name;
            this->path = name;
            this->depth = 0;
            this->parent = NULL;
            this->next = NULL;
            this->isDir = true;
            this->first = NULL;
        }

// constructor 2
fpath::fpath(string path, string name, int depth, fpath* parent, bool isDir){
            this->name = name;
            this->path = path;
            this->depth = depth;
            this->parent = parent;
            this->next = NULL;
            this->isDir = isDir;
            this->first = NULL;
        }

// get name of file
string fpath::get_name(){
    return this->name;
}

// get path of file
string fpath::get_path(){
    return this->path;
}

// get depth of file
int fpath::get_depth(){
    return this->depth;
}

// get parent of file
fpath* fpath::get_parent(){
    return this->parent;
}

// get next file from current
fpath* fpath::get_next(){
    return this->next;
}

// get if it a directory
bool fpath::get_isDir(){
    return this->isDir;
}

// get first file (if it is directory else get NULL)
fpath* fpath::get_first(){
    if(this->isDir == true){
        return this->first;
    }
    else{
        return NULL;
    }
}

// set name of file
void fpath::set_name(string name){
    this->name = name;
}

// increase depth
void fpath::deepen(){
    this->depth = this->depth + 1;
}

// decrease depth
void fpath::up(){
    this->depth = this->depth - 1;
}

// set parent to file
void fpath::set_parent(fpath* parent){
    this->parent = parent;
}

// set next to file
void fpath::set_next(fpath* next){
    this->next = next;
}

// set if it is directory
void fpath::is_dir(bool isDir){
    this->isDir = isDir;
}

// set first file (for directories)
void fpath::set_first(fpath* first){
    this->first = first;
}

