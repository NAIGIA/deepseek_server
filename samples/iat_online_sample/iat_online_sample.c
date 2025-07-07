// /*
// * 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。
// */

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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "request.h"

#define PORT 11434
#define SERVER_IP "192.168.48.7"   //设置IP地址
#define BUFFER_SIZE 4096 * 10

#define	BUFFER2_SIZE	4096
#define FRAME_LEN	640 
#define HINTS_SIZE  100

#define AUDIOFILE  "wav/record.wav" // 音频文件路径

void send_request_to_deepseek_tcp(const char *prompt)
{

    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    // 创建 socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Socket creation error\n");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // 将 IP 地址从字符串转换为二进制格式
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0)
    {
        printf("Invalid address / Address not supported\n");
        close(sock);
        return;
    }

    // 连接到服务器
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Connection Failed\n");
        close(sock);
        return;
    }

    // 构造请求体
    char request_body[1024] = {0};
    snprintf(request_body, sizeof(request_body),
             "{\"model\": \"deepseek-r1:1.5b\", \"prompt\": \"%s\", \"stream\": false, \"include_context\": false}",
             prompt);

    // 构造请求头
    char request_headers[512] = {0};
    snprintf(request_headers, sizeof(request_headers),
             "POST /api/generate HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "\r\n",
             SERVER_IP, PORT, strlen(request_body));

    // 合并完整请求
    char request[4096] = {0};
    snprintf(request, sizeof(request), "%s%s", request_headers, request_body);

    // 发送请求
    send(sock, request, strlen(request), 0);
    printf("Request sent:\n%s\n", request);

    // 接收响应（一次性读，或可加循环）
    int valread = read(sock, buffer, BUFFER_SIZE);
    if (valread > 0)
    {
        printf("Response:\n%s\n", buffer);
    }
    else
    {
        printf("Failed to receive response.\n");
    }

    // 关闭 socket
    close(sock);
}


int send_audio_file_over_tcp(const char* filename)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(50021);
    bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    listen(sockfd, 1);

    printf("🟢 等待客户端连接...\n");
    int connfd = accept(sockfd, NULL, NULL);
    if (connfd < 0) { perror("accept"); close(sockfd); return -1; }
    printf("✅ 接收到连接\n");

    send(connfd, "连接成功", strlen("连接成功") + 1, 0);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (fd < 0) { perror("open"); close(connfd); close(sockfd); return -1; }

    int size;
    if (recv(connfd, &size, sizeof(int), 0) <= 0) {
        perror("recv file size");
        close(fd); close(connfd); close(sockfd); return -1;
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

    // ❌ 不再关闭 connfd，这里返回给 main 使用
    // close(connfd);
    

    printf("✅ 文件接收完成: %s\n", filename);
    return connfd;
}



/* 上传用户词表 */
int upload_userwords()
{
	char*			userwords	=	NULL;
	unsigned int	len			=	0;
	unsigned int	read_len	=	0;
	FILE*			fp			=	NULL;
	int				ret			=	-1;

	fp = fopen("userwords.txt", "rb");
	if (NULL == fp)										
	{
		printf("\nopen [userwords.txt] failed! \n");
		goto upload_exit;
	}

	fseek(fp, 0, SEEK_END);
	len = ftell(fp); //获取音频文件大小
	fseek(fp, 0, SEEK_SET);  					
	
	userwords = (char*)malloc(len + 1);
	if (NULL == userwords)
	{
		printf("\nout of memory! \n");
		goto upload_exit;
	}

	read_len = fread((void*)userwords, 1, len, fp); //读取用户词表内容
	if (read_len != len)
	{
		printf("\nread [userwords.txt] failed!\n");
		goto upload_exit;
	}
	userwords[len] = '\0';
	
	MSPUploadData("userwords", userwords, len, "sub = uup, dtt = userword", &ret); //上传用户词表
	if (MSP_SUCCESS != ret)
	{
		printf("\nMSPUploadData failed ! errorCode: %d \n", ret);
		goto upload_exit;
	}
	
upload_exit:
	if (NULL != fp)
	{
		fclose(fp);
		fp = NULL;
	}	
	if (NULL != userwords)
	{
		free(userwords);
		userwords = NULL;
	}
	
	return ret;
}

void run_iat(const char* audio_file, const char* session_begin_params, int connfd)
{
	const char*		session_id					=	NULL;
	char			rec_result[BUFFER_SIZE]		=	{NULL};	
	char      		rec_out[4096]				=	{NULL};
	char			hints[HINTS_SIZE]			=	{NULL}; //hints为结束本次会话的原因描述，由用户自定义
	unsigned int	total_len					=	0; 
	int				aud_stat					=	MSP_AUDIO_SAMPLE_CONTINUE ;		//音频状态
	int				ep_stat						=	MSP_EP_LOOKING_FOR_SPEECH;		//端点检测
	int				rec_stat					=	MSP_REC_STATUS_SUCCESS ;			//识别状态
	int				errcode						=	MSP_SUCCESS ;

	FILE*			f_pcm						=	NULL;
	char*			p_pcm						=	NULL;
	long			pcm_count					=	0;
	long			pcm_size					=	0;
	long			read_size					=	0;

	
	if (NULL == audio_file)
		goto iat_exit;

	f_pcm = fopen(audio_file, "rb");
	if (NULL == f_pcm) 
	{
		printf("\nopen [%s] failed! \n", audio_file);
		goto iat_exit;
	}
	
	fseek(f_pcm, 0, SEEK_END);
	pcm_size = ftell(f_pcm); //获取音频文件大小 
	fseek(f_pcm, 0, SEEK_SET);		

	p_pcm = (char *)malloc(pcm_size);
	if (NULL == p_pcm)
	{
		printf("\nout of memory! \n");
		goto iat_exit;
	}

	read_size = fread((void *)p_pcm, 1, pcm_size, f_pcm); //读取音频文件内容
	if (read_size != pcm_size)
	{
		printf("\nread [%s] error!\n", audio_file);
		goto iat_exit;
	}
	
	printf("\n开始语音听写 ...\n");
	session_id = QISRSessionBegin(NULL, session_begin_params, &errcode); //听写不需要语法，第一个参数为NULL
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRSessionBegin failed! error code:%d\n", errcode);
		goto iat_exit;
	}
	
	while (1) 
	{
		unsigned int len = 10 * FRAME_LEN; // 每次写入200ms音频(16k，16bit)：1帧音频20ms，10帧=200ms。16k采样率的16位音频，一帧的大小为640Byte
		int ret = 0;

		if (pcm_size < 2 * len) 
			len = pcm_size;
		if (len <= 0)
			break;

		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if (0 == pcm_count)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;

		printf(">");
		ret = QISRAudioWrite(session_id, (const void *)&p_pcm[pcm_count], len, aud_stat, &ep_stat, &rec_stat);
		if (MSP_SUCCESS != ret)
		{
			printf("\nQISRAudioWrite failed! error code:%d\n", ret);
			goto iat_exit;
		}
			
		pcm_count += (long)len;
		pcm_size  -= (long)len;
		
		if (MSP_REC_STATUS_SUCCESS == rec_stat) //已经有部分听写结果
		{
			const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
			if (MSP_SUCCESS != errcode)
			{
				printf("\nQISRGetResult failed! error code: %d\n", errcode);
				goto iat_exit;
			}
			if (NULL != rslt)
			{
				unsigned int rslt_len = strlen(rslt);
				total_len += rslt_len;
				if (total_len >= BUFFER_SIZE)
				{
					printf("\nno enough buffer for rec_result !\n");
					goto iat_exit;
				}
				strncat(rec_result, rslt, rslt_len);
			}
		}

		if (MSP_EP_AFTER_SPEECH == ep_stat)
			break;
		usleep(200*1000); //模拟人说话时间间隙。200ms对应10帧的音频
	}
	errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRAudioWrite failed! error code:%d \n", errcode);
		goto iat_exit;	
	}

	while (MSP_REC_STATUS_COMPLETE != rec_stat) 
	{
		const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
		if (MSP_SUCCESS != errcode)
		{
			printf("\nQISRGetResult failed, error code: %d\n", errcode);
			goto iat_exit;
		}
		if (NULL != rslt)
		{
			unsigned int rslt_len = strlen(rslt);
			total_len += rslt_len;
			if (total_len >= BUFFER_SIZE)
			{
				printf("\nno enough buffer for rec_result !\n");
				goto iat_exit;
			}
			strncat(rec_result, rslt, rslt_len);
		}
		usleep(150*1000); //防止频繁占用CPU
	}
	printf("\n语音听写结束\n");
	printf("=============================================================\n");
	printf("%s\n",rec_result);
	printf("=============================================================\n");

    // ✅ 回传识别结果给客户端
    if (connfd > 0) {
        send(connfd, rec_result, strlen(rec_result) + 1, 0);
		//send_request_to_deepseek_tcp(rec_result);  // ✅ 将识别结果作为 prompt 传入
		ask_ollama(rec_result, rec_out, sizeof(rec_out));
		printf("\n %s \n", rec_out);
		printf("\n已发送\n");
        close(connfd);  // ✅ 关闭连接

    }

	

iat_exit:
	if (NULL != f_pcm)
	{
		fclose(f_pcm);
		f_pcm = NULL;
	}
	if (NULL != p_pcm)
	{	free(p_pcm);
		p_pcm = NULL;
	}

	QISRSessionEnd(session_id, hints);
}


int main(int argc, char* argv[])
{
	int			ret						=	MSP_SUCCESS;
	int			upload_on				=	1; //是否上传用户词表
	const char* login_params			=	"appid = 4bf71c36, work_dir = ."; // 登录参数，appid与msc库绑定,请勿随意改动

	/*
	* sub:				请求业务类型
	* domain:			领域
	* language:			语言
	* accent:			方言
	* sample_rate:		音频采样率
	* result_type:		识别结果格式
	* result_encoding:	结果编码格式
	*
	*/
	const char* session_begin_params	=	"sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = utf8";

	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, login_params); //第一个参数是用户名，第二个参数是密码，均传NULL即可，第三个参数是登录参数	
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed , Error code %d.\n",ret);
		goto exit; //登录失败，退出登录
	}

	printf("\n########################################################################\n");
	printf("## 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。##\n");
	printf("########################################################################\n\n");
	printf("演示示例选择:是否上传用户词表？\n0:不使用\n1:使用\n");

	const char *audiofile = "wav/record.wav";

	scanf("%d", &upload_on);
	if (upload_on)
	{
		printf("上传用户词表 ...\n");
		ret = upload_userwords();
		if (MSP_SUCCESS != ret)
			goto exit;	
		printf("上传用户词表成功\n");
	}
	// 新增的 TCP 音频发送服务端
	// send_audio_file_over_tcp(audiofile);  
	// run_iat(AUDIOFILE, session_begin_params); //iflytek02音频内容为“中美数控”；如果上传了用户词表，识别结果为：“中美速控”。
	// run_iat("wav/iflytek02.wav", session_begin_params); //iflytek02音频内容为“中美数控”；如果上传了用户词表，识别结果为：“中美速控”。
    int connfd = send_audio_file_over_tcp(audiofile);  
    if (connfd > 0)
        run_iat(AUDIOFILE, session_begin_params, connfd);
    else
        printf("❌ 音频接收失败，终止识别流程\n");

exit:
	printf("按任意键退出 ...\n");
	getchar();
	MSPLogout(); //退出登录

	return 0;
}

/*
 * 服务端：监听TCP端口，接收客户端WAV文件，调用讯飞SDK识别，回传文本
 */

// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <unistd.h>
// #include <fcntl.h>
// #include <sys/socket.h>
// #include <arpa/inet.h>
// #include <sys/types.h>
// #include <sys/wait.h>
// #include "qisr.h"
// #include "msp_cmn.h"
// #include "msp_errors.h"

// #define BUFFER_SIZE 4096
// #define AUDIOFILE "wav/record.wav"
// #define PORT 50021

// // 接收wav文件，返回连接描述符
// int recv_audio_file(int server_fd, const char* filepath)
// {
//     printf("等待客户端连接...\n");
//     int connfd = accept(server_fd, NULL, NULL);
//     if (connfd < 0) {
//         perror("accept");
//         return -1;
//     }
//     printf("客户端已连接\n");

//     // 发送连接成功消息
//     send(connfd, "连接成功", strlen("连接成功") + 1, 0);

//     // 接收文件大小（网络字节序）
//     int file_size_net = 0;
//     if (recv(connfd, &file_size_net, sizeof(file_size_net), 0) <= 0) {
//         perror("recv file size");
//         close(connfd);
//         return -1;
//     }
//     int file_size = ntohl(file_size_net);
//     printf("文件大小: %d bytes\n", file_size);

//     int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
//     if (fd < 0) {
//         perror("open file");
//         close(connfd);
//         return -1;
//     }

//     int total = 0;
//     char buf[BUFFER_SIZE];
//     while (total < file_size) {
//         int r = recv(connfd, buf, sizeof(buf), 0);
//         if (r <= 0) break;
//         write(fd, buf, r);
//         total += r;
//     }
//     close(fd);
//     if (total < file_size) {
//         printf("文件接收不完整\n");
//         close(connfd);
//         return -1;
//     }

//     // 发送接收成功消息
//     send(connfd, "文件接收成功", strlen("文件接收成功") + 1, 0);

//     printf("文件接收完成: %s\n", filepath);
//     return connfd; // 返回连接，用于后续识别结果回传
// }

// // 语音识别并回传结果
// void run_iat_and_reply(const char* audio_file, const char* session_begin_params, int connfd)
// {
//     const char* session_id = NULL;
//     char rec_result[BUFFER_SIZE] = {0};
//     char hints[100] = {0};
//     unsigned int total_len = 0;
//     int aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
//     int ep_stat = MSP_EP_LOOKING_FOR_SPEECH;
//     int rec_stat = MSP_REC_STATUS_SUCCESS;
//     int errcode = MSP_SUCCESS;

//     FILE* f_pcm = fopen(audio_file, "rb");
//     if (!f_pcm) {
//         printf("打开音频文件失败: %s\n", audio_file);
//         return;
//     }
//     fseek(f_pcm, 0, SEEK_END);
//     long pcm_size = ftell(f_pcm);
//     fseek(f_pcm, 0, SEEK_SET);

//     char* p_pcm = malloc(pcm_size);
//     if (!p_pcm) {
//         fclose(f_pcm);
//         printf("内存分配失败\n");
//         return;
//     }

//     if (fread(p_pcm, 1, pcm_size, f_pcm) != (size_t)pcm_size) {
//         printf("读取音频文件失败\n");
//         free(p_pcm);
//         fclose(f_pcm);
//         return;
//     }
//     fclose(f_pcm);

//     printf("开始语音识别...\n");
//     session_id = QISRSessionBegin(NULL, session_begin_params, &errcode);
//     if (errcode != MSP_SUCCESS) {
//         printf("QISRSessionBegin失败，错误码: %d\n", errcode);
//         free(p_pcm);
//         return;
//     }

//     long pcm_count = 0;
//     while (pcm_size > 0) {
//         unsigned int len = 10 * 640;
//         if (pcm_size < 2 * len) len = pcm_size;
//         if (len <= 0) break;

//         aud_stat = (pcm_count == 0) ? MSP_AUDIO_SAMPLE_FIRST : MSP_AUDIO_SAMPLE_CONTINUE;

//         int ret = QISRAudioWrite(session_id, p_pcm + pcm_count, len, aud_stat, &ep_stat, &rec_stat);
//         if (ret != MSP_SUCCESS) {
//             printf("QISRAudioWrite失败，错误码: %d\n", ret);
//             QISRSessionEnd(session_id, hints);
//             free(p_pcm);
//             return;
//         }

//         pcm_count += len;
//         pcm_size -= len;

//         if (rec_stat == MSP_REC_STATUS_SUCCESS) {
//             const char* rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
//             if (errcode != MSP_SUCCESS) {
//                 printf("QISRGetResult失败，错误码: %d\n", errcode);
//                 QISRSessionEnd(session_id, hints);
//                 free(p_pcm);
//                 return;
//             }
//             if (rslt) {
//                 strncat(rec_result, rslt, sizeof(rec_result) - strlen(rec_result) - 1);
//                 total_len = strlen(rec_result);
//             }
//         }
//         if (ep_stat == MSP_EP_AFTER_SPEECH) break;

//         usleep(200000);
//     }

//     errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
//     if (errcode != MSP_SUCCESS) {
//         printf("QISRAudioWrite最后一次失败，错误码: %d\n", errcode);
//         QISRSessionEnd(session_id, hints);
//         free(p_pcm);
//         return;
//     }

//     while (rec_stat != MSP_REC_STATUS_COMPLETE) {
//         const char* rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
//         if (errcode != MSP_SUCCESS) {
//             printf("QISRGetResult失败,错误码: %d\n", errcode);
//             QISRSessionEnd(session_id, hints);
//             free(p_pcm);
//             return;
//         }
//         if (rslt) {
//             strncat(rec_result, rslt, sizeof(rec_result) - strlen(rec_result) - 1);
//             total_len = strlen(rec_result);
//         }
//         usleep(150000);
//     }

//     printf("识别结果：\n%s\n", rec_result);

//     // 回传识别结果
//     if (connfd > 0) {
//         send(connfd, rec_result, strlen(rec_result) + 1, 0);
//         printf("已发送识别结果：%s\n",&rec_result);
//         close(connfd);
//     }

//     QISRSessionEnd(session_id, hints);
//     free(p_pcm);
// }

// int main()
// {
//     int ret = MSPLogin(NULL, NULL, "appid = 你的appid, work_dir = .");
//     if (ret != MSP_SUCCESS) {
//         printf("MSPLogin失败，错误码: %d\n", ret);
//         return -1;
//     }
//     printf("讯飞SDK登录成功\n");

//     // 创建TCP监听套接字
//     int server_fd = socket(AF_INET, SOCK_STREAM, 0);
//     if (server_fd < 0) {
//         perror("socket");
//         MSPLogout();
//         return -1;
//     }

//     int on = 1;
//     setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

//     struct sockaddr_in addr = {0};
//     addr.sin_family = AF_INET;
//     addr.sin_addr.s_addr = htonl(INADDR_ANY);
//     addr.sin_port = htons(PORT);

//     if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
//         perror("bind");
//         close(server_fd);
//         MSPLogout();
//         return -1;
//     }

//     if (listen(server_fd, 1) < 0) {
//         perror("listen");
//         close(server_fd);
//         MSPLogout();
//         return -1;
//     }

//     printf("服务器启动，监听端口 %d\n", PORT);

//     while (1) {
//         int connfd = recv_audio_file(server_fd, AUDIOFILE);
//         if (connfd > 0) {
//             // 调用讯飞语音识别并发送结果
//             const char* params = "sub = iat, domain = iat, language = zh_cn, accent = mandarin, sample_rate = 16000, result_type = plain, result_encoding = utf8";
//             run_iat_and_reply(AUDIOFILE, params, connfd);
//         }
//     }

//     close(server_fd);
//     MSPLogout();
//     return 0;
// }


