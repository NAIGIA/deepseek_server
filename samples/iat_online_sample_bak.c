/*
* 语音听写(iFly Auto Transform)技术能够实时地将语音转换成对应的文字。
*/

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

#define	BUFFER_SIZE	4096
#define FRAME_LEN	640 
#define HINTS_SIZE  100



int send_audio_file_over_tcp(const char* filename)
{
    // 创建TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    //设置端口重用
    int on = 1;
    setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));

    //将套接字与IP和端口绑定
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY); // 自动获取本机IP
    addr.sin_port = htons(50021); // 端口号PORT

    bind(sockfd, (struct sockaddr *)&addr, len);

    //将sockfd设置为被动监听套接字
    listen(sockfd, 3);

    //等待客户端的连接
    int connfd = accept(sockfd, NULL, NULL);
    printf("等待客户端的连接\n");
    if(connfd < 0)
    {
        perror("accept failed\n");
        exit(1);
    }
    else
    {
        printf("accept success\n");
    }
    // 回复客户端连接成功
    send(connfd,"连接成功",sizeof("连接成功"),0);

    int voice_fd = open(filename, O_RDWR | O_CREAT, 0777);
    if(voice_fd < 0)
    {
        perror("open failed\n");
        exit(1);
    }

    //接收客户端发送过来的文件总字节数
    int file_size;
    recv(connfd,&file_size,4,0);
    printf("file_size = %d\n",file_size);
    printf("接受文件大小成功，等待客户端发送文件\n");

    // 接收客户端发送过来的文件数据
    char recvbuf[100];
    char recv_size,tail_size=0;
    while(1)
    {
        bzero(recvbuf, 100);
        recv_size = recv(connfd, recvbuf, 100, 0);
        tail_size += recv_size;
        printf("tail_size = %d\n",tail_size);
        write(voice_fd, recvbuf, recv_size);
        if( tail_size == file_size)//接收完毕
        {
            printf("接受文件成功\n");
            break;
        }   
    }

    //给客户端回复信息
    send(connfd,"文件接收成功",sizeof("文件接收成功"),0);
    
    // 5. 关闭套接字
    close(connfd);
    close(sockfd);

    return 0;
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

void run_iat(const char* audio_file, const char* session_begin_params)
{
	const char*		session_id					=	NULL;
	char			rec_result[BUFFER_SIZE]		=	{NULL};	
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

	scanf("%d", &upload_on);
	if (upload_on)
	{
		printf("上传用户词表 ...\n");
		ret = upload_userwords();
		if (MSP_SUCCESS != ret)
			goto exit;	
		printf("上传用户词表成功\n");
	}

	//send_audio_file_over_tcp("wav/iflytek01.wav");  // 新增的 TCP 音频发送服务端
	run_iat("wav/iflytek01.wav", session_begin_params); //iflytek02音频内容为“中美数控”；如果上传了用户词表，识别结果为：“中美速控”。
exit:
	printf("按任意键退出 ...\n");
	getchar();
	MSPLogout(); //退出登录

	return 0;
}

