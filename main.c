#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include "dyn_string.h"

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

StringView getLine(int pos, String str){
    const int start = getLineStart(pos, str.items);

    size_t i;
    for(i = pos; i < str.len; ++i){
        if(str.items[i] == '\n'){
            break;
        }
    }
    return (StringView){i+1-start, str.items+start};
}

struct termios oldt;
FILE* file;

void intHandler(int _){
    (void)_;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    int fd = open("/dev/null", O_RDONLY);
    dup2(fd, 0);
    close(fd);
}

typedef struct {
    int cursor;
    String contents;
} ListFile;

ListFile loadListFile(const char* file_path){
    file = fopen(file_path, "r+");
    String contents = String_init();
    int cursor = 0;
    char linebuf[1024];
    int i = 0;
    while(fgets(linebuf, 1024, file)){
        int n = strlen(linebuf);
        if(linebuf[0] == '>'){
            cursor = i;
        }
        i += n;
        updateCstring(&contents, linebuf);
    }
    return (ListFile) {cursor, contents};
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

    ListFile list = loadListFile(file_path);

    StringView line = getLine(list.cursor, list.contents);
    size_t col = line.len-1;
    String cached_line = stringFromView(line);
    while(true){
        if(debug_char){
            int ret = getchar();
            if(ret == EOF) break;
            char c = (char)ret;
            printf("Pressed key %d\n", (int)c);
            continue;
        }

        printf("\033[2K\r%.*s", (int)cached_line.len-1, cached_line.items);
        printf("\033[%ldG", col+1);
        int ret = getchar();
        if(ret == EOF) break;
        char c = (char) ret;
        if(c == '\033'){
            getchar(); // skip '['
            switch(getchar()){
                case 'A': // Up
                    if(list.cursor > 0){
                        list.cursor = getLineStart(list.cursor-1, list.contents.items);
                    }
                    line = getLine(list.cursor, list.contents);
                    // TODO: Commit cached line
                    String_deinit(cached_line);
                    cached_line = stringFromView(line);
                    col = cached_line.len-1;
                break;
                case 'B': // Down
                    if(list.cursor + line.len == (int)list.contents.len) break;
                    list.cursor += line.len;
                    line = getLine(list.cursor, list.contents);
                    // TODO: Commit cached line
                    
                    String_deinit(cached_line);
                    cached_line = stringFromView(line);
                    col = cached_line.len-1;
                break;
                case 'C': // Right
                    if(col < cached_line.len-1) col += 1;
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
        if(c >= 32 && c != 127){
            insert(&cached_line, c, col++);
        }else if(c == 127){
            orderedRemove(&cached_line, --col);
        }
    }

    String_deinit(cached_line);
    String_deinit(list.contents);
    fclose(file);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
    return 0;
}
