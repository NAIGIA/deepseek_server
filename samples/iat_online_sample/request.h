#ifndef _REQUEST_H_
#define _REQUEST_H_

#include <string.h>
#include <stdbool.h>

int init_socket();
int get_server_ip(char* interface_name);
char* unicode_to_utf8(const char* unicode_str) ;
char* decode_unicode_escapes(const char* input) ;
int parse_json(const char *json, char *text);
int send_request(const char *text);
bool isValid(const char* s) ;
void removeSubstr(char *str, const char *sub) ;
int ask_ollama(const char* prompt, char* result_out, size_t result_size);

#endif