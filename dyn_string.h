#include <stdlib.h>
#include <string.h>

typedef unsigned int uint;

typedef struct {
    size_t capacity;
    size_t len;
    char* items;
} String;

String String_init(){
    return (String) {
        .capacity = 0,
        .len = 0,
        .items = NULL,
    };
}

String String_initCapacity(int capacity){
    return (String) {
        .capacity = capacity,
        .len = 0,
        .items = malloc(capacity),
    };
}

void String_deinit(String list){
    free(list.items);
}

void append(String* list, char item){
    if(list->capacity == 0){
        list->capacity = 1;
        list->items = realloc(list->items, sizeof(char));
    }else if(list->len == list->capacity){
        list->capacity *= 2;
        list->items = realloc(list->items, list->capacity*sizeof(char));
    }
    list->items[list->len++] = item;
}

void insert(String* list, char item, unsigned int index){
    if(index >= list->len){
        append(list, item);
        return;
    }
    char tmp = list->items[index];
    list->items[index] = item;
    for(unsigned int i = index+1; i < list->len; ++i){
        char tmp2 = list->items[i];
        list->items[i] = tmp;
        tmp = tmp2;
    }
    append(list, tmp);
}

void insertString(String* list, String other, unsigned int index){
    if(list->len + other.len > list->capacity){
        list->capacity += other.capacity;
        list->items = realloc(list->items, list->capacity*sizeof(char));
    }
    const size_t to_move = list->len - index;
    memmove(list->items + index + other.len,
            list->items + index,
            to_move);

    memmove(list->items + index, other.items, other.len);
    list->len += other.len;
}


void update(String* list, String other){
    if(list->len + other.len > list->capacity){
        list->capacity += other.capacity;
        list->items = realloc(list->items, list->capacity*sizeof(char));
    }
    memcpy(&list->items[list->len], other.items, other.len);
    list->len += other.len;
}

void updateCstring(String* list, const char* other){
    const size_t other_len = strlen(other);
    if(list->len + other_len > list->capacity){
        list->capacity += other_len;
        list->items = realloc(list->items, list->capacity*sizeof(char));
    }
    memcpy(&list->items[list->len], other, other_len);
    list->len += other_len;
}

char orderedRemove(String* list, size_t index){
    if(index == list->len-1){
        return list->items[list->len--];
    }
    char to_return = list->items[index];
    for(uint i = index; i < list->len-1; ++i){
        list->items[i] = list->items[i+1];
    }
    list->len -= 1;
    return to_return;
}
char swapRemove(String* list, size_t index){
    char to_return = list->items[index];
    list->items[index] = list->items[list->len--];
    return to_return;
}
void removeSubstring(String* list, size_t start, size_t len){
    if(start >= list->len - len){
        list->len = start;
        return;
    }
    for(uint i = start; i < list->len - len; ++i){
        list->items[i] = list->items[i+len];
    }
    list->len -= len;
}

uint find(String haystack, char needle){
    uint i;
    for(i = 0; i < haystack.len; ++i){
        if(needle == haystack.items[i]){
            break;
        }
    }
    return i;
}

String stringFromCstring(char* cstr){
    String ret;
    ret.capacity = ret.len= strlen(cstr);
    ret.items = malloc(ret.len);
    memcpy(ret.items, cstr, ret.len);
    return ret;
}

char* cloneCstring(char* cstr){
    char* ret = malloc(strlen(cstr)+1);
    strcpy(ret, cstr);
    return ret;
}

typedef struct {
    int len;
    char* ptr;
} StringView;

String stringFromView(StringView view) {
    String str = String_initCapacity(view.len);
    memcpy(str.items, view.ptr, view.len);
    str.len = view.len;
    return str;
}
