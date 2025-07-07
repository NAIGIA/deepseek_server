#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include "cJSON.h"
#include <stdbool.h>
#include <ctype.h>
#include "request.h"

#define PORT 11434
#define INTERFACE_NAME "192.168.48.7"
#define SERVER_IP "192.168.48.7"
#define BUFFER_SIZE 4096 * 5

// char SERVER_IP[INET_ADDRSTRLEN] = {0};
int sock = 0;

//1. 初始化 socket 连接
int init_socket()
{
    struct sockaddr_in serv_addr;
    //1.1 创建 socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("\n Socket creation error \n");
        return -1;
    }
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);
    //1.2 将 IP 地址从字符串转换为二进制格式
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0)
    {
        printf("\nInvalid address/ Address not supported \n");
        return -2;
    }
    //1.3 连接到服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("\nConnection Failed \n");
        return -3;
    }
    
    return sock;
}

// 2. 获取接口 IP 地址
int get_server_ip(char* interface_name)
{
    struct ifaddrs *ifaddr, *current_if;

    //2.1 获取所有接口信息
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs failed");
        return -4;
    }

    //2.2 遍历所有接口
    for (current_if = ifaddr; current_if != NULL; current_if = current_if->ifa_next) 
    {
        //2.3 忽略回环接口
        if (current_if->ifa_addr == NULL) 
            continue;

        //2.4 处理 IPv4 地址，忽略 IPv6 地址，判断是否为指定接口
        if (current_if->ifa_addr->sa_family == AF_INET && strcmp(current_if->ifa_name, interface_name) == 0) 
        {
            //2.5 获取结构体
            struct sockaddr_in *addr = (struct sockaddr_in *)current_if->ifa_addr;
            //2.6 将 IP 地址转换为字符串格式
            inet_ntop(AF_INET, &addr->sin_addr, SERVER_IP, INET_ADDRSTRLEN);

            //2.7 释放资源
            freeifaddrs(ifaddr);
            return 0;
        }
    }

    //2.8 如果没有找到指定接口的 IP 地址，释放资源并返回错误码
    freeifaddrs(ifaddr);

    return -5;
}

// 3. unicode转utf8
char* unicode_to_utf8(const char* unicode_str) 
{
    unsigned int code;
    //3.1 提取 4 位十六进制 Unicode 码点
    sscanf(unicode_str, "%4x", &code);

    //3.2 转换为 UTF-8 编码
    char* utf8 = NULL;
    if (code <= 0x7F) {
        utf8 = malloc(2);
        utf8[0] = code;
        utf8[1] = '\0';
    } else if (code <= 0x7FF) {
        utf8 = malloc(3);
        utf8[0] = 0xC0 | (code >> 6);
        utf8[1] = 0x80 | (code & 0x3F);
        utf8[2] = '\0';
    } else if (code <= 0xFFFF) {
        utf8 = malloc(4);
        utf8[0] = 0xE0 | (code >> 12);
        utf8[1] = 0x80 | ((code >> 6) & 0x3F);
        utf8[2] = 0x80 | (code & 0x3F);
        utf8[3] = '\0';
    }
    return utf8;
}

//4. 解码unicode转义字符
char* decode_unicode_escapes(const char* input) 
{
    char* output = malloc(strlen(input) * 4 + 1); // 预留足够空间
    char* out_ptr = output;
    const char* in_ptr = input;

    while (*in_ptr) {
        if (strncmp(in_ptr, "\\u", 2) == 0) {
            // 处理 \uXXXX
            char* utf8 = unicode_to_utf8(in_ptr + 2);
            strcpy(out_ptr, utf8);
            out_ptr += strlen(utf8);
            in_ptr += 6; // 跳过 \uXXXX
            free(utf8);
        } else {
            *out_ptr++ = *in_ptr++;
        }
    }
    *out_ptr = '\0';
    return output;
}


//5. 解析JSON语句(非流式)
int parse_json(const char *json, char *text)
{
    // 将解析后的文本存储在 text 中
    //5.1 查找 HTTP 响应正文
    char *body = strstr(json, "\r\n\r\n");
    if (!body) return 1;
    
    //5.2 跳过 HTTP 头部
    while(*body != '{')
        if(*body++ == '\0') 
            return 1;
    
    //printf("HTTP响应正文: %s\n", body);

    //5.3 解析JSON
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        //printf("JSON解析错误: %s\n", cJSON_GetErrorPtr());
        cJSON_Delete(root);
        return 2;
    }
    
    //5.4 提取字段
    cJSON *response = cJSON_GetObjectItem(root, "response");
    if(response == NULL) 
    {
        //printf("JSON字段不存在\n");
        return 3;
    }

    strcpy(text, response->valuestring); 
    //5.6 清理资源
    cJSON_Delete(root);

    return 0;
}

#if 0
//使用流式获取，需要另外编写套接字的接收代码，不建议，太麻烦
//5. 解析JSON语句(流式)
int parse_stream_json(char *input_str, char *text)
{
    char *result = NULL;
    size_t buf_size = 0;
    size_t buf_used = 0;

    const char *parse_ptr = input_str;
    while (*parse_ptr) {
        // 跳过空白字符
        while (isspace(*parse_ptr)) parse_ptr++;
        if (!*parse_ptr) break;

        // 记录解析起始位置
        const char *start = parse_ptr;

        // 解析JSON
        cJSON *json = cJSON_Parse(parse_ptr);
        if (!json) {
            // 查找下一个可能的JSON对象起始
            while (*parse_ptr && *parse_ptr != '{') parse_ptr++;
            continue;
        }

        // 提取response字段
        cJSON *response = cJSON_GetObjectItem(json, "response");
        if (response && cJSON_IsString(response)) {
            const char *text = response->valuestring;
            size_t text_len = strlen(text);

            // 扩展缓冲区（增加安全校验）
            if (text_len > 0) {
                size_t needed = buf_used + text_len + 1;
                if (needed > buf_size) {
                    size_t new_size = buf_size ? buf_size * 2 : text_len + 1;
                    while (new_size < needed) new_size *= 2;
                    
                    char *new_buf = realloc(result, new_size);
                    if (!new_buf) {
                        cJSON_Delete(json);
                        free(result);
                        return -1;
                    }
                    result = new_buf;
                    buf_size = new_size;
                }

                memcpy(result + buf_used, text, text_len);
                buf_used += text_len;
                result[buf_used] = '\0';
            }
        }

        // 正确移动指针：通过计算已解析内容长度
        parse_ptr = start + cJSON_GetArraySize(json);
        cJSON_Delete(json);
    }


    if (result) {
        sprintf(text, "%s", result);
    } else {
        free(result);
        return -1;
    }

    free(result);
    return 0;
}
#endif

// 6. 发送请求
int send_request(const char *text)
{
    //6.1 构造 HTTP 请求体（关闭流式响应）
    char request_body[1024] = {0};
    snprintf(request_body, 1024, "{\"model\": \"deepseek-r1:1.5b\", \"prompt\": \"%s\", \"stream\":  \
    false,\"include_context\": false}", text);
    //6.2 编辑请求头
    char request_headers[256] = {0};
    snprintf(request_headers, sizeof(request_headers),
    "POST /api/generate HTTP/1.1\r\n"
    "Host: %s:11434\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: %zu\r\n"
    "\r\n",
    SERVER_IP,
    strlen(request_body));
    //6.3 编辑请求协议
    char request[4096] = {0};
    snprintf(request, sizeof(request), "%s%s", request_headers, request_body);
    //6.4 发送请求
    int ret = send(sock, request, strlen(request), 0);
    if (ret < 0)
    {
        printf("Failed to send request.\n");
        return -6;
    }
    //printf("Request sent:\n%s\n", request);

    return 0;
}

//7. 判断括号是否匹配（判断数据是否完整，括号不匹配说明数据没接收完整）
bool isValid(const char* s) 
{
    char stack[BUFFER_SIZE];  // 简易栈
    int top = -1;      // 栈顶指针
    
    for (int i = 0; s[i]; i++) {
        if (s[i] == '(' || s[i] == '[' || s[i] == '{') 
        {
            stack[++top] = s[i];  // 左括号入栈
        } 
        else if (s[i] == ')' || s[i] == ']' || s[i] == '}') 
        {
            if (top == -1) return false;  // 栈空说明不匹配
            char left = stack[top--];     // 弹出栈顶元素
            if (!(left == '(' && s[i] == ')' ||  // 直接判断匹配
                  left == '[' && s[i] == ']' || 
                  left == '{' && s[i] == '}')) {
                return false;
            }
        }
    }
    return top == -1;  // 栈必须为空
}

// 8. 删除字符串中的子串
void removeSubstr(char *str, const char *sub) 
{
    size_t sub_len = strlen(sub);
    if (sub_len == 0) return; // 防止空子串

    char *pos;
    while ((pos = strstr(str, sub)) != NULL) {
        // 计算需要移动的剩余长度（包括结尾的 \0）
        size_t tail_len = strlen(pos + sub_len) + 1;
        memmove(pos, pos + sub_len, tail_len);
    }
}

int ask_ollama(const char* prompt, char* result_out, size_t result_size)
{
    int err_val = 0;
    char text[1024] = {0};
    char allbuf[BUFFER_SIZE * 2] = {0};
    char buffer[BUFFER_SIZE] = {0};
    int valread = 0;
    int count = 0;

    // 获取 IP（只需执行一次，可移除）
    if (SERVER_IP[0] == '\0') {
        err_val = get_server_ip(INTERFACE_NAME);
        if (err_val != 0) {
            printf("get_server_ip error: %d\n", err_val);
            return -1;
        }
    }

    // 初始化 socket
    err_val = init_socket();
    if (err_val < 0) {
        printf("init_socket error: %d\n", err_val);
        return -2;
    }

    // 发送请求
    err_val = send_request(prompt);
    if (err_val < 0) {
        printf("send_request error: %d\n", err_val);
        close(sock);
        return -3;
    }

    // 接收响应
    valread = read(sock, buffer, BUFFER_SIZE);
    if (valread > 0) {
        sprintf(allbuf, "%s", buffer);

        if (!isValid(allbuf)) {
            read(sock, buffer, BUFFER_SIZE);
            strcat(allbuf, buffer);
            count++;
        }

        memset(text, 0, sizeof(text));

        err_val = parse_json(allbuf, text);
        if (err_val == 0) {
            // 清洗字符串
            removeSubstr(text, "</think>");
            removeSubstr(text, "<think>");
            if (!count) removeSubstr(text, "\n\n");

            strncpy(result_out, text, result_size - 1);
            result_out[result_size - 1] = '\0';
        } else {
            printf("parse_json error: %d\n", err_val);
            close(sock);
            return -4;
        }
    } else {
        printf("Failed to receive response.\n");
        close(sock);
        return -5;
    }

    close(sock);
    return 0;
}
