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

int getLineStart(int pos, const char* buf){
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
volatile bool interrupted_once = false;

void intHandler(int _){
    (void)_;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    if(!interrupted_once){
        // If interrupted once, try to close the stdin so we can exit gracefully
        int fd = open("/dev/null", O_RDONLY);
        dup2(fd, 0);
        close(fd);
        interrupted_once = true;
    }else{
        // if interrupted twice, we are most likely stuck somewhere that is not
        // in the getchar call, so we just quit everything
        exit(SIGINT);
    }
}

typedef struct {
    size_t cursor;
    String contents;
} ListFile;

ListFile loadListFile(const char* file_path){
    file = fopen(file_path, "r+");
    if(file == NULL){ // file doesn't exist, so we open with "w+" instead.
        file = fopen(file_path, "w+");
        assert(file != NULL);
        fputc('\n', file);
        rewind(file);
    }
    String contents = String_init();
    int cursor = 0;
    char linebuf[1024];
    int i = 0;
    while(fgets(linebuf, 1024, file)){
        int n = strlen(linebuf);
        if(linebuf[0] == '>'){
            cursor = i;
            updateCstring(&contents, linebuf+1);
        }else{
            updateCstring(&contents, linebuf);
        }
        i += n;
    }
    append(&contents, '\0');
    return (ListFile) {cursor, contents};
}

void saveListFile(ListFile list){
    if(list.cursor >= list.contents.len-1){
        printf("\033[2K\rInvalid cursor position (%ld/%ld), cancelling save.\n",
               list.cursor, list.contents.len-1);
        return;
    }
    file = freopen(NULL, "w+", file);
    const char old_c = list.contents.items[list.cursor];
    list.contents.items[list.cursor] = '\0';
    fputs(list.contents.items, file);
    fputc('>', file);
    list.contents.items[list.cursor] = old_c;
    fputs(list.contents.items + list.cursor, file);
}

void printViewBytes(StringView view){
    printf("\n\n( ");
    for(int i = 0; i < view.len; ++i){
        printf("%d ", (int)view.ptr[i]);
    }
    printf(")\n\n");
}

int getCursorNLinesBefore(int n, const char* str, int cursor){
    for(int i = 0; i < n; ++i){
        if(cursor == 0) return -1;
        cursor = getLineStart(cursor-1, str);
    }
    return cursor;
}


int main(int argc, char** argv){
    /*tcgetattr gets the parameters of the current terminal
     *     STDIN_FILENO will tell tcgetattr that it should write the settings
     *         of stdin to oldt*/
    tcgetattr(STDIN_FILENO, &oldt);

    // better call this early, but not before setting oldt
    signal(SIGINT, intHandler);

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
    if(argc >= 2){
        strncpy(file_path, argv[1], 255);
    }

    ListFile list = loadListFile(file_path);

    StringView line = getLine(list.cursor, list.contents);
    size_t col = line.len-1;
    String cached_line = stringFromView(line);
    int transpose_count = 0;
    while(true){
        printf("\033[2K\r%.*s", (int)cached_line.len-1, cached_line.items);
        printf("\033[%ldG", col+1);
        int ret = getchar();
        if(ret == EOF) break;
        char c = (char) ret;
        if(c == 20)
            transpose_count += 1;
        else
            transpose_count = 0;
        
        if(c == '\033'){
            getchar(); // skip '['
            switch(c = getchar()){
                case 'A': // Up
                    // Commit cached line
                    assert(list.contents.items[list.cursor+line.len-1] == '\n');
                    assert(cached_line.items[cached_line.len-1] == '\n');
                    removeSubstring(&list.contents, list.cursor, line.len);
                    insertString(&list.contents, cached_line, list.cursor);

                    // Change cursor
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
                    if(list.cursor + line.len >= list.contents.len-2) break;
                    // Commit cached line
                    removeSubstring(&list.contents, list.cursor, line.len);
                    insertString(&list.contents, cached_line, list.cursor);

                    // Change cursor
                    list.cursor += line.len;
                    line = getLine(list.cursor, list.contents);
                    
                    // Cache new line
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
                case '3':
                    switch(c = getchar()){
                        case '~':
                            if(col >= cached_line.len-1) break;
                            orderedRemove(&cached_line, col);
                        break;
                        default:
                            printf("\033[2K\rUnhandled escape character after \\033[S: char code %d\n", (int)c);
                    }
                break;
                default:
                    printf("\033[2K\rUnhandled escape character after \\033[: char code %d\n", (int)c);

            }
        }else if(c >= 32 && c != 127){
            insert(&cached_line, c, col++);
        }else if(c == 127){ // backspace
            if(col > 0){
                orderedRemove(&cached_line, --col);
            }
        }else if(c == 10){ // Enter
            // Commit cached line
            removeSubstring(&list.contents, list.cursor, line.len);
            insertString(&list.contents, cached_line, list.cursor);

            // Change cursor
            list.cursor += cached_line.len;
            insert(&list.contents, '\n', list.cursor);
            line = getLine(list.cursor, list.contents);
            assert(line.len == 1);
            assert(line.ptr[0] == '\n');
            
            // Cache new line
            String_deinit(cached_line);
            cached_line = stringFromView(line);
            col = cached_line.len-1;
        }else if(c == 12){ // Remove line
            if(list.cursor > 0){
                removeSubstring(&list.contents, list.cursor, line.len);
                list.cursor = getLineStart(list.cursor-1, list.contents.items);
            }else{
                removeSubstring(&list.contents, list.cursor, line.len-1);
            }
            line = getLine(list.cursor, list.contents);
            String_deinit(cached_line);
            cached_line = stringFromView(line);
            col = cached_line.len-1;
        }else if(c == 20){ // Transpose line
            if(list.cursor == 0) continue; // Nothing to do here.
            // Transpose count behavior (applied at last line g):
            // 1: 1 2 3 4 -> 1 2 4 3
            // 2: 1 2 4 3 -> 1 3 4 2
            // 3: 1 3 4 2 -> 2 3 4 1
            // 4: 1 2 4 3 -> 1 2 3 4
            // 5: 1 2 3 4 -> 1 2 4 3
            int prev_cursor = getCursorNLinesBefore(transpose_count, list.contents.items, list.cursor);
            if(prev_cursor < 0){
                // Just move the line at the cursor to the top
                transpose_count = 0;
                removeSubstring(&list.contents, list.cursor, line.len);
                insertString(&list.contents, cached_line, 0);
                list.cursor = list.cursor - line.len + cached_line.len;
            }else{
                const StringView prev_line = getLine(prev_cursor, list.contents);
                // Insert the prev string after the current string
                insertString(&list.contents,
                             (String){
                                .len = prev_line.len,
                                .capacity = prev_line.len,
                                .items = prev_line.ptr
                            },
                            list.cursor + line.len);
                // Remove the current string and last_string;
                // so far this amounts to replacing the current with the prev_string
                assert(prev_cursor == 0 || list.contents.items[prev_cursor-1] == '\n');
                removeSubstring(&list.contents, list.cursor, line.len);
                assert(prev_cursor == 0 || list.contents.items[prev_cursor-1] == '\n');
                removeSubstring(&list.contents, prev_cursor, prev_line.len);
                assert(prev_cursor == 0 || list.contents.items[prev_cursor-1] == '\n');
                // Insert the new current string where prev_string previously was
                insertString(&list.contents, cached_line, prev_cursor);

                // Update Cursor to where it would be if it remained fixed
                list.cursor = list.cursor - prev_line.len + cached_line.len; 
            }
            assert(list.cursor == 0 || list.contents.items[list.cursor-1] == '\n');
            line = getLine(list.cursor, list.contents);
            
            // Cache new line
            String_deinit(cached_line);
            cached_line = stringFromView(line);
            col = cached_line.len-1;
        }else{
            printf("\033[2K\rUnhandled character code %d\n", (int)c);
        }
    }

    removeSubstring(&list.contents, list.cursor, line.len);
    insertString(&list.contents, cached_line, list.cursor);

    saveListFile(list);
    String_deinit(cached_line);
    String_deinit(list.contents);
    fclose(file);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    printf("\n");
    return 0;
}
