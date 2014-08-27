#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX 256

#define NELEMS(x) ((sizeof(x)) / (sizeof((x)[0])))

char *prog1 = "./mzihttpd";

int main(int argc, char **argv) {
    int i;
    int retval;
    //int flag;
    //char c;

    char cmd[MAX];

    char *argsHttp[] = {
        "-c", "--port abc", "-h", "--config notexist", "-w"
    };
    
    for(i = 0; i < NELEMS(argsHttp); i++) {
        snprintf(cmd, sizeof(cmd), "%s %s", prog1, argsHttp[i]);
        fprintf(stdout, "\nEexcuting: %s\n\n", cmd);
        retval = system(cmd);
        fprintf(stdout, "\n\nReturn value: %d (%s)\n", retval, strerror(retval));
    }

    return 0;
}
