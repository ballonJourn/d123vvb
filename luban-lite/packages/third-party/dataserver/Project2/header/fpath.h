#ifndef FPATH_H
#define FPATH_H

#include <string>

using namespace std;

class fpath{
    private:
        string name; // name of file
        string path; // whole path
        int depth; // depth of file in directory
        fpath* parent; // parent of file
        fpath* next; // next file
        bool isDir; // is directory?
        fpath* first; // first file (for directories)
    public:
        // constructor 1
        fpath(string);
        // constructor 2
        fpath(string, string, int, fpath*, bool);
        // get name of file
        string get_name();
        // get path of file
        string get_path();
        // get depth of file
        int get_depth();
        // get parent of file
        fpath* get_parent();
        // get next file from current
        fpath* get_next();
        // get if it a directory
        bool get_isDir();
        // get first file (if it is directory else get NULL)
        fpath* get_first();

        // set name of file
        void set_name(string);
        // increase depth
        void deepen();
        // decrease depth
        void up();
        // set parent to file
        void set_parent(fpath*);
        // set next to file
        void set_next(fpath*);
        // set if it is directory
        void is_dir(bool);
        // set first file (for directories)
        void set_first(fpath*);
};

#endif