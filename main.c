#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdbool.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include "dyn_string.h"

int getLineStart(int pos, const char* buf){
	for(int i = pos-1; i >= 0; --i) if(buf[i] == '\n') return i+1;
	return 0;
}

StringView getLine(int pos, String str){
	const int start = getLineStart(pos, str.ptr);

	size_t i;
	for(i = pos; i < str.len; ++i) if(str.ptr[i] == '\n') break;
	return (StringView){i+1-start, str.ptr+start};
}

struct termios oldt;
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
	FILE* file = fopen(file_path, "r+");
	if(file == NULL){ // file doesn't exist, so we open with "w+" instead.
		file = fopen(file_path, "w+");
		assert(file != NULL);
		fputc('\n', file);
		rewind(file);
	}
	size_t file_len; {
		int seekerr = fseek(file, 0, SEEK_END);
		assert(seekerr == 0);
		file_len = ftell(file);
		assert(file_len != (size_t)-1);
		seekerr = fseek(file, 0L, SEEK_SET); // rewind(file)
		assert(seekerr == 0);
	};
	
	char* contents = malloc(file_len+1);
	int cursor = 0;
	char linebuf[1024];
	int i = 0;
	while(fgets(linebuf, 1024, file)){
		int n = strlen(linebuf);
		if(linebuf[0] == '>'){
			cursor = i;
			memcpy(contents+i, linebuf+1, n-1);
			i += n-1;
		}else{
			memcpy(contents+i, linebuf, n);
			i += n;
		}
	}
	contents[i] = '\0';
	fclose(file);
	return (ListFile) {cursor, (String) {.ptr = contents, .len = i, .capacity = i}};
}

void saveListFile(ListFile list, const char* file_path){
	if(list.cursor >= list.contents.len-1){
		printf("\033[2K\rInvalid cursor position (%ld/%ld), cancelling save.\n",
		       list.cursor, list.contents.len-1);
		return;
	}
	FILE* file = fopen(file_path, "w+");
	assert(file != NULL);
	const char old_c = list.contents.ptr[list.cursor];
	list.contents.ptr[list.cursor] = '\0';
	fputs(list.contents.ptr, file);
	fputc('>', file);
	list.contents.ptr[list.cursor] = old_c;
	fputs(list.contents.ptr + list.cursor, file);
	fclose(file);
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

int cacheLine(ListFile list, String* cached_line, StringView* line, int* edit_count){
	*line = getLine(list.cursor, list.contents);
	String_deinit(*cached_line);
	*cached_line = stringFromView(*line);
	*edit_count += 1;
	return cached_line->len-1;
}


int main(int argc, char** argv){
	/*tcgetattr gets the parameters of the current terminal
	 *	 STDIN_FILENO will tell tcgetattr that it should write the settings
	 *		 of stdin to oldt*/
	tcgetattr(STDIN_FILENO, &oldt);

	// better call this early, but not before setting oldt
	signal(SIGINT, intHandler);

	struct termios newt;
	tcgetattr(STDIN_FILENO, &newt);

	/*ICANON normally takes care that one line at a time will be processed
	 *	 that means it will return if it sees a "\n" or an EOF or an EOL*/
	newt.c_lflag &= ~(ICANON);
	newt.c_lflag &= ~(ECHO); // Don't print keys pressed

	/*Those new settings will be set to STDIN
	 *	 TCSANOW tells tcsetattr to change attributes immediately. */
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
	int edit_count = 0;
	while(true){
		printf("\033[2K\r%.*s", (int)cached_line.len-1, cached_line.ptr);
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
					assert(list.contents.ptr[list.cursor+line.len-1] == '\n');
					assert(cached_line.ptr[cached_line.len-1] == '\n');
					removeSubstring(&list.contents, list.cursor, line.len);
					insertString(&list.contents, cached_line, list.cursor);

					// Change cursor
					if(list.cursor > 0){
						list.cursor = getLineStart(list.cursor-1, list.contents.ptr);
					}

					col = cacheLine(list, &cached_line, &line, &edit_count);
				break;
				case 'B': // Down
					if(list.cursor + line.len >= list.contents.len-2) break;
					// Commit cached line
					removeSubstring(&list.contents, list.cursor, line.len);
					insertString(&list.contents, cached_line, list.cursor);

					assert(list.cursor == 0 || list.contents.ptr[list.cursor-1] == '\n');
					// Change cursor
					list.cursor += cached_line.len;
					
					col = cacheLine(list, &cached_line, &line, &edit_count);
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
		}else if(c == 127 || c == 8){ // backspace
			if(col > 0){
				orderedRemove(&cached_line, --col);
			}
		}else if(c == 10 || c == 14){ // Enter or Ctrl+N
			// Commit cached line
			removeSubstring(&list.contents, list.cursor, line.len);
			insertString(&list.contents, cached_line, list.cursor);

			// Change cursor
			// if ctrl+N, we want are adding a new line above, so we don't change the cursor
			if(c == 10)
				list.cursor += cached_line.len;
			insert(&list.contents, '\n', list.cursor);
			col = cacheLine(list, &cached_line, &line, &edit_count);
		}else if(c == 12){ // Remove line
			if(cached_line.len > 1){
				printf(" <- Do you really want to remove this? [y/N] ");
				if((ret = getchar()) == EOF) break;
				c = ret;
				if(c == 'n' || c == 'N' || c == 10) continue;
			}
			removeSubstring(&list.contents, list.cursor, line.len);
			if(list.cursor > 0){
				list.cursor = getLineStart(list.cursor-1, list.contents.ptr);
			}
			col = cacheLine(list, &cached_line, &line, &edit_count);
		}else if(c == 20){ // Transpose line
			if(list.cursor == 0) continue; // Nothing to do here.
			// Transpose count behavior (applied at last line g):
			// 1: 1 2 3 4 -> 1 2 4 3
			// 2: 1 2 4 3 -> 1 3 4 2
			// 3: 1 3 4 2 -> 2 3 4 1
			// 4: 1 2 4 3 -> 1 2 3 4
			// 5: 1 2 3 4 -> 1 2 4 3
			int prev_cursor = getCursorNLinesBefore(transpose_count, list.contents.ptr, list.cursor);
			if(prev_cursor < 0){
				// Just move the line at the cursor to the top
				transpose_count = 0;
				const int prev_len = getLine(getLineStart(list.cursor-1, list.contents.ptr), list.contents).len;
				removeSubstring(&list.contents, list.cursor, line.len);
				insertString(&list.contents, cached_line, 0);
				list.cursor = list.cursor - prev_len + cached_line.len;
			}else{
				const StringView prev_line = getLine(prev_cursor, list.contents);
				// Insert the prev string after the current string
				insertString(&list.contents,
					(String){
						.len = prev_line.len,
						.capacity = prev_line.len,
						.ptr = prev_line.ptr
					},
					list.cursor + line.len);
				// Remove the current string and last_string;
				// so far this amounts to replacing the current with the prev_string
				assert(prev_cursor == 0 || list.contents.ptr[prev_cursor-1] == '\n');
				removeSubstring(&list.contents, list.cursor, line.len);
				assert(prev_cursor == 0 || list.contents.ptr[prev_cursor-1] == '\n');
				removeSubstring(&list.contents, prev_cursor, prev_line.len);
				assert(prev_cursor == 0 || list.contents.ptr[prev_cursor-1] == '\n');
				// Insert the new current string where prev_string previously was
				insertString(&list.contents, cached_line, prev_cursor);

				// Update Cursor to where it would be if it remained fixed
				list.cursor = list.cursor - prev_line.len + cached_line.len;
			}
			assert(list.cursor == 0 || list.contents.ptr[list.cursor-1] == '\n');
			
			col = cacheLine(list, &cached_line, &line, &edit_count);
		}else{
			printf("\033[2K\rUnhandled character code %d\n", (int)c);
		}

		if(edit_count == 1){
			saveListFile(list, file_path);
			edit_count = 0;
		}
	}

	removeSubstring(&list.contents, list.cursor, line.len);
	insertString(&list.contents, cached_line, list.cursor);

	saveListFile(list, file_path);
	String_deinit(cached_line);
	String_deinit(list.contents);
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	printf("\n");
	return 0;
}
