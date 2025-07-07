#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "request.h" // 你用于访问 DeepSeek 的函数

#define PORT 50021
#define AUDIOFILE "wav/record.wav"
#define BUFFER_SIZE 4096 * 10
#define FRAME_LEN 640
#define HINTS_SIZE 100

// 🔁 从客户端接收音频
int recv_audio_from_client(int connfd, const char* filename) {
    send(connfd, "连接成功", strlen("连接成功") + 1, 0);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    int size;
    if (recv(connfd, &size, sizeof(int), 0) <= 0) {
        perror("recv file size");
        close(fd);
        return -1;
    }

    printf("📦 文件大小: %d 字节\n", size);
    int total = 0;
    char buf[1024];
    while (total < size) {
        int r = recv(connfd, buf, sizeof(buf), 0);
        if (r <= 0) break;
        write(fd, buf, r);
        total += r;
    }

    close(fd);
    send(connfd, "文件接收成功", strlen("文件接收成功") + 1, 0);
    printf("✅ 文件接收完成: %s\n", filename);
    return 0;
}

// ✅ 上传用户词表（不变）
int upload_userwords() {
    FILE* fp = fopen("userwords.txt", "rb");
    if (!fp) {
        printf("打开 userwords.txt 失败\n");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* userwords = (char*)malloc(len + 1);
    if (!userwords) {
        printf("内存不足\n");
        fclose(fp);
        return -1;
    }

    fread(userwords, 1, len, fp);
    userwords[len] = '\0';
    fclose(fp);

    int ret;
    MSPUploadData("userwords", userwords, len, "sub = uup, dtt = userword", &ret);
    free(userwords);
    return ret;
}

// ✅ 执行语音识别 + 回复
void run_iat(const char* audio_file, const char* session_params, int connfd) {
    FILE* f_pcm = fopen(audio_file, "rb");
    if (!f_pcm) {
        printf("无法打开音频文件\n");
        return;
    }

    fseek(f_pcm, 0, SEEK_END);
    long pcm_size = ftell(f_pcm);
    fseek(f_pcm, 0, SEEK_SET);

    char* p_pcm = (char*)malloc(pcm_size);
    fread(p_pcm, 1, pcm_size, f_pcm);
    fclose(f_pcm);

    const char* session_id = NULL;
    char rec_result[BUFFER_SIZE] = {0};
    char rec_out[4096] = {0};
    char hints[HINTS_SIZE] = {0};

    int errcode = MSP_SUCCESS;
    session_id = QISRSessionBegin(NULL, session_params, &errcode);
    if (MSP_SUCCESS != errcode) {
        printf("QISRSessionBegin 失败: %d\n", errcode);
        free(p_pcm);
        return;
    }

    long count = 0;
    int ep_stat = MSP_EP_LOOKING_FOR_SPEECH;
    int rec_stat = MSP_REC_STATUS_SUCCESS;

    while (pcm_size > 0) {
        unsigned int len = 10 * FRAME_LEN;
        if (pcm_size < 2 * len)
            len = pcm_size;
        int aud_stat = (count == 0) ? MSP_AUDIO_SAMPLE_FIRST : MSP_AUDIO_SAMPLE_CONTINUE;

        int ret = QISRAudioWrite(session_id, (const void*)&p_pcm[count], len, aud_stat, &ep_stat, &rec_stat);
        if (ret != MSP_SUCCESS) break;

        count += len;
        pcm_size -= len;

        if (MSP_REC_STATUS_SUCCESS == rec_stat) {
            const char* rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
            if (rslt)
                strncat(rec_result, rslt, sizeof(rec_result) - strlen(rec_result) - 1);
        }

        if (MSP_EP_AFTER_SPEECH == ep_stat)
            break;

        usleep(200000);
    }

    QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
    while (MSP_REC_STATUS_COMPLETE != rec_stat) {
        const char* rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
        if (rslt)
            strncat(rec_result, rslt, sizeof(rec_result) - strlen(rec_result) - 1);
        usleep(150000);
    }

    QISRSessionEnd(session_id, hints);
    free(p_pcm);

    // ✅ 回传识别结果
    printf("📝 识别结果: %s\n", rec_result);
    send(connfd, rec_result, strlen(rec_result) + 1, 0);

    // ✅ 接入大模型
    ask_ollama(rec_result, rec_out, sizeof(rec_out));
    send(connfd, rec_out, strlen(rec_out) + 1, 0);
    printf("🤖 LLM回复: %s\n", rec_out);

    close(connfd);
}

// ✅ 主程序入口
int main(int argc, char* argv[]) {
    int ret = MSPLogin(NULL, NULL, "appid = 4bf71c36, work_dir = .");
    if (ret != MSP_SUCCESS) {
        printf("MSPLogin 失败: %d\n", ret);
        return -1;
    }

    printf("上传用户词表？(0/1): ");
    int upload_on = 1;
    scanf("%d", &upload_on);
    getchar(); // 清除回车

    if (upload_on) {
        if (upload_userwords() != MSP_SUCCESS) {
            printf("用户词表上传失败\n");
        } else {
            printf("用户词表上传成功\n");
        }
    }

    // ✅ 只监听一次，复用 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 5);
    printf("🟢 服务启动，监听端口 %d...\n", PORT);

    const char* session_params = "sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = utf8";

    while (1) {
        int connfd = accept(server_fd, NULL, NULL);
        if (connfd < 0) {
            perror("accept");
            continue;
        }

        printf("📥 新客户端连接\n");
        if (recv_audio_from_client(connfd, AUDIOFILE) == 0) {
            run_iat(AUDIOFILE, session_params, connfd);
        } else {
            close(connfd);
        }
    }

    MSPLogout();
    return 0;
}
