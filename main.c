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

typedef int LineIdx;
#define MAX_LINE_SIZE 512
typedef struct {
	int len;
	char* str;
	LineIdx prev;
	LineIdx next;
} Line;

char* strbuf;
Line* lines;
size_t line_count = 1; // we reserve the first for null references

LineIdx createNewLine() {
	lines[line_count] = (Line) {
		.str = strbuf + line_count*MAX_LINE_SIZE,
		.len = 1,
	};
	lines[line_count].str[0] = '\n';
	return line_count++;
}
void removeLine(LineIdx idx) {
	LineIdx last_prev = lines[idx].prev;
	LineIdx last_next = lines[idx].next;
	if(last_prev) lines[last_prev].next = last_next;
	if(last_next) lines[last_next].prev = last_prev;
	lines[idx].prev = 0;
	lines[idx].next = 0;
}
void insertBefore(LineIdx to_insert, LineIdx next) {
	assert(lines[to_insert].prev == 0);
	assert(lines[to_insert].next == 0);
	lines[to_insert].next = next;
	if(next) {
		LineIdx tmp = lines[next].prev;
		lines[next].prev = to_insert;
		lines[to_insert].prev = tmp;
		if(tmp) lines[tmp].next = to_insert;
	}
}
void insertAfter(LineIdx to_insert, LineIdx prev) {
	assert(lines[to_insert].prev == 0);
	assert(lines[to_insert].next == 0);
	lines[to_insert].prev = prev;
	if(prev) {
		LineIdx tmp = lines[prev].next;
		lines[prev].next = to_insert;
		lines[to_insert].next = tmp;
		if(tmp) lines[tmp].prev = to_insert;
	}
}
LineIdx loadListFile(const char* file_path){
	FILE* file = fopen(file_path, "r+");
	if(file == NULL){ // file doesn't exist, so we open with "w+" instead.
		file = fopen(file_path, "w+");
		assert(file != NULL);
	}
	
	LineIdx cursor = 0;
	LineIdx cur_idx = createNewLine();
	insertAfter(cur_idx, 0);
	Line* line = &lines[cur_idx];
	while(fgets(line->str, MAX_LINE_SIZE, file)){
		line->len = strlen(line->str);
		if(line->str[0] == '>') {
			cursor = cur_idx;
			for(int i = 0; i < line->len-1; ++i) line->str[i] = line->str[i+1];
			--line->len;
		}
		LineIdx prev_idx = cur_idx;
		cur_idx = createNewLine();
		insertAfter(cur_idx, prev_idx);
		line = &lines[cur_idx];
	}
	fclose(file);
	if(cursor == 0) cursor = cur_idx;
	return cursor;
}

void saveListFile(LineIdx cursor, const char* file_path){
	assert(cursor != 0);
	FILE* file = fopen(file_path, "w+");
	assert(file != NULL);
	LineIdx idx = cursor;
	// get first index
	while(lines[idx].prev) idx = lines[idx].prev;
	// write all lines in order
	assert(idx != 0);
	while(idx) {
		Line line = lines[idx];
		line.str[line.len] = 0;
		if(idx == cursor) fputc('>', file);
		fputs(line.str, file);
		idx = line.next;
	}
	fclose(file);
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

	size_t lines_alloc_size = sizeof(Line)*((size_t)1 << 16);
	strbuf = malloc((size_t)1 << 24);
	assert(strbuf != NULL);
	lines = malloc(lines_alloc_size);
	assert(lines != NULL);
	LineIdx cursor = loadListFile(file_path);

	Line* line = &lines[cursor];
	int col = line->len-1;
	int transpose_count = 0;
	bool changed = false;
	while(true){
		line = &lines[cursor];
		assert(line->str[line->len-1] == '\n');
		printf("\033[2K\r%.*s", line->len-1, line->str);
		printf("\033[%dG", col+1);
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
					if(!line->prev) break;
					cursor = line->prev;
					col = lines[cursor].len-1;
					changed = true;
				break;
				case 'B': // Down
					if(!line->next) break;
					cursor = line->next;
					col = lines[cursor].len-1;
					changed = true;
				break;
				case 'C': // Right
					if(col < line->len-1) col += 1;
				break;
				case 'D': // Left
					if(col > 0) col -= 1;
				break;
				case '3':
					switch(c = getchar()){
						case '~': // If pressed "delete"
							if(col >= line->len-1) break;
							for(int i = col; i < line->len-1; ++i)
								line->str[i] = line->str[i+1];
							--line->len;
							changed = true;
						break;
						default:
							printf("\033[2K\rUnhandled escape character after \\033[S: char code %d\n", (int)c);
					}
				break;
				default:
					printf("\033[2K\rUnhandled escape character after \\033[: char code %d\n", (int)c);
			}
		}else if(c >= 32 && c != 127){ // printable character
			for(int i = line->len++; i > col; --i)
				line->str[i] = line->str[i-1];
			line->str[col++] = c;
			changed = true;
		}else if(c == 127 || c == 8){ // backspace
			if(col > 0) {
				for(int i = --col; i < line->len-1; ++i)
					line->str[i] = line->str[i+1];
				--line->len;
				changed = true;
			}
		}else if(c == 10 || c == 14){ // Enter or Ctrl+N (resp.)
			LineIdx new_cursor = createNewLine();
			if(c == 14) insertBefore(new_cursor, cursor);
			else insertAfter(new_cursor, cursor);
			cursor = new_cursor;
			col = 0;
			changed = true;
		}else if(c == 12){ // Remove line
			if(line->len > 1){
				printf(" <- Do you really want to remove this? [y/N] ");
				if((ret = getchar()) == EOF) break;
				c = ret;
				if(c == 'n' || c == 'N' || c == 10) continue;
			}
			if(!line->prev && !line->next) {
				line->str[0] = '\n';
				line->len = 1;
			} else {
				LineIdx new_cursor = line->prev ? line->prev : line->next;
				removeLine(cursor);
				cursor = new_cursor;
			}
			col = lines[cursor].len-1;
			changed = true;
		}else if(c == 20){ // Transpose line
			if(!line->prev) continue; // Nothing to do here.
			// Transpose count behavior (applied at last line g):
			// 1: 1 2 3 4 -> 1 2 4 3
			// 2: 1 2 4 3 -> 1 3 4 2
			// 3: 1 3 4 2 -> 2 3 4 1
			// 4: 1 2 4 3 -> 1 2 3 4
			// 5: 1 2 3 4 -> 1 2 4 3
			int cur_transpose = transpose_count;
			LineIdx target = cursor;
			while(lines[target].prev && cur_transpose > 0)
				target = lines[target].prev;
			if(cur_transpose > 0) transpose_count = 0; // reset the count if we hit the top
			assert(target != cursor);
			LineIdx last_prev = line->prev;
			removeLine(cursor);
			insertBefore(cursor, target);
			cursor = last_prev;
			col = lines[cursor].len-1;
			changed = true;
		}else{
			printf("\033[2K\rUnhandled character code %d\n", (int)c);
		}

		if(changed){
			saveListFile(cursor, file_path);
			changed = false;
		}
	}

	saveListFile(cursor, file_path);
	free(lines);
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	printf("\n");
	return 0;
}
