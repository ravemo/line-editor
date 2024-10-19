#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>
#include <termios.h>

#define BUF_SIZE 4096
#define LINE_SIZE 256

typedef struct {
    int len;
    char* ptr;
} String;

int getLineStart(int pos, char* buf){
    if(pos == 0) return 0;
    for(int i = pos-1; i >= 0; --i){
        if(buf[i] == '\n'){
            return i+1;
            break;
        }
    }
    return 0;
}

String getLine(int pos, char* buf, int n){
    const int start = getLineStart(pos, buf);

    int i;
    for(i = pos; i < n; ++i){
        if(buf[i] == '\n'){
            break;
        }
    }
    return (String){i+1-start, buf+start};
}

struct termios oldt;
void shutdown(){
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

void intHandler(int _){
    (void)_;
    shutdown();
    printf("\n");
    exit(0);
}

void loadLine(String* dst_line, char* dst_buf, String src_line){
    memset(dst_buf, 0, LINE_SIZE);
    memcpy(dst_buf, src_line.ptr, src_line.len);
    *dst_line = (String){src_line.len, dst_buf};
}

void insert(String* str, char c, int pos){
    for(int i = str->len; i >= pos; --i){
        str->ptr[i+1] = str->ptr[i];
    }
    str->ptr[pos] = c;
    str->len += 1;
}

void orderedRemove(String* str, int pos){
    for(int i = pos; i < str->len; ++i){
        str->ptr[i] = str->ptr[i+1];
    }
    str->len -= 1;
}


int main(int argc, char** argv){
    signal(SIGINT, intHandler);

    /*tcgetattr gets the parameters of the current terminal
     *     STDIN_FILENO will tell tcgetattr that it should write the settings
     *         of stdin to oldt*/
    tcgetattr(STDIN_FILENO, &oldt);
    struct termios newt;
    tcgetattr(STDIN_FILENO, &newt);

    /*ICANON normally takes care that one line at a time will be processed
     *     that means it will return if it sees a "\n" or an EOF or an EOL*/
    newt.c_lflag &= ~(ICANON);          
    newt.c_lflag &= ~(ECHO); // Don't print keys pressed

    /*Those new settings will be set to STDIN
     *     TCSANOW tells tcsetattr to change attributes immediately. */
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);

    char file_path[256] = "/tmp/single";
    bool debug_char = false;
    if(argc >= 2){
        (void)argv;
        debug_char = true;
        // TODO: Get file to load from argv, but also handle debug char
    //    strncpy(file_path, argv[1], 255);
    }
    char buf[BUF_SIZE];
    FILE* file = fopen(file_path, "r+");
    //
    // get cursor position
    int cursor = 0;
    {
        int n = fread(buf, 1, BUF_SIZE, file);

        int i = 0;
        while(i < n){
            const String line = getLine(i, buf, n);
            if(buf[i] == '>'){
                cursor = i;
                break;
            }
            i += line.len+1;
        }
    }


    const int buflen = strlen(buf);
    String line = getLine(cursor, buf, buflen);
    int col = line.len-1;
    char cached_line_buf[LINE_SIZE];
    String cached_line;
    loadLine(&cached_line, cached_line_buf, line);
    while(true){
        char c = getchar();
        if(debug_char){
            printf("Pressed key %d\n", (int)c);
            continue;
        }

        printf("\033[2K\r%.*s", cached_line.len-1, cached_line.ptr);
        printf("\033[%dG", col+1);
        if(c == '\033'){
            getchar(); // skip '['
            switch(getchar()){
                case 'A': // Up
                    if(cursor > 0){
                        cursor = getLineStart(cursor-1, buf);
                    }
                    line = getLine(cursor, buf, buflen);
                    // TODO: Commit cached line
                    loadLine(&cached_line, cached_line_buf, line);
                    col = cached_line.len-1;
                break;
                case 'B': // Down
                    cursor += line.len;
                    line = getLine(cursor, buf, buflen);
                    // TODO: Commit cached line
                    loadLine(&cached_line, cached_line_buf, line);
                    col = cached_line.len-1;
                break;
                case 'C': // Right
                    if(col < line.len-1) col += 1;
                break;
                case 'D': // Left
                    if(col > 0) col -= 1;
                break;
                case 51:
                    switch(getchar()){
                        case 126:
                            orderedRemove(&cached_line, col);
                            break;
                    }
            }
        }
        if(c >= 32 && c != 127 && cached_line.len < 255){
            insert(&cached_line, c, col++);
        }else if(c == 127){
            orderedRemove(&cached_line, --col);
        }
    }

    shutdown();
    return 0;
}
