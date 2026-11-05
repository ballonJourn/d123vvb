#include <iostream>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h> 
#include <sys/wait.h>
#include <string>

using namespace std;

int main(){
    for(int i = 1; i <= 10; i++){
        pid_t  pid;
        int ret = 1;
        int status;

        pid = fork();
 
        if (pid == -1){
            printf("can't fork, error occured\n");
            exit(EXIT_FAILURE);
        }else if (pid == 0){
        
            string path = "from";
            char * argv_list[] = {"./remoteClient","-i","127.0.0.1","-p","8080","-d",(char*)path.c_str(),NULL};
        
            execv("./remoteClient",argv_list);
            exit(0);
        }
    }


    return 0;
}
