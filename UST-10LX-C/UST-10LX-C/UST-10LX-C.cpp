/*!
\file
\brief Sample to get URG data using Win32

\author Satofumi KAMIMURA

$Id: capture_sample.cpp 1724 2010-02-25 10:43:11Z satofumi $

Compling and execute process
- In case of Visual Studio
- Select capture_sample.sln from capture_sample.zip
- When Visual Studio is started, press F5 to build and execute.
- If COM port is not found, then change the com_port in main function.

- In case of MinGW, Cygwin
- % g++ capture_sample.cpp -o capture_sample
- % ./capture_sample
- If COM port is not found, then change the com_port in main function.

\attention Change com_port, com_baudrate values in main() with relevant values.
\attention We are not responsible for any loss or damage occur by using this program
\attention We appreciate the suggestions and bug reports
*/

#include "stdafx.h"
//#define _CRT_SECURE_NO_WARNINGS

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace std;


// To record the output of SCIP,define RAW_OUTPUT
//#define RAW_OUTPUT

#if defined(RAW_OUTPUT)
static FILE* Raw_fd_ = NULL;
#endif


enum {
	Timeout = 1000,               // [msec]
	EachTimeout = 2,              // [msec]
	LineLength = 64 + 3 + 1 + 1 + 1 + 16,
};

static HANDLE HCom = INVALID_HANDLE_VALUE;
static int ReadableSize = 0;
static char* ErrorMessage = "no error.";


/*!
\brief Manage sensor information
*/
typedef struct
{
	enum {
		MODL = 0,                   //!< Sensor model information              0
		DMIN,                       //!< Minimum measurable distance [mm]      1
		DMAX,                       //!< Maximum measurable distance [mm]      2
		ARES,                       //!< Angle of resolution                   3
		AMIN,                       //!< Minimum measurable area               4
		AMAX,                       //!< Maximum measurable area               5
		AFRT,                       //!< Front direction value                 6
		SCAN,                       //!< Standard angular velocity             7
	};
	string model;                 //!< Obtained MODL information
	long distance_min;            //!< Obtained DMIN information
	long distance_max;            //!< Obtained DMAX information
	int area_total;               //!< Obtained ARES information
	int area_min;                 //!< Obtained AMIN information
	int area_max;                 //!< Obtained AMAX information
	int area_front;               //!< Obtained AFRT information
	int scan_rpm;                 //!< Obtained SCAN information                  

	int first;                    //!< Starting position of measurement         测量开始的位置
	int last;                     //!< End position of measurement              测量的最后位置
	int max_size;                 //!< Maximum size of data                     数据最大长度
	long last_timestamp;          //!< Time stamp when latest data is obtained  获得最新数据的时间标记
} urg_state_t;


// Delay
static void delay(int msec)
{
	Sleep(msec);
}


static int com_changeBaudrate(long baudrate)
{
	DCB dcb;

	GetCommState(HCom, &dcb);
	dcb.BaudRate = baudrate;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.fParity = FALSE;
	dcb.StopBits = ONESTOPBIT;
	SetCommState(HCom, &dcb);

	return 0;
}


// Serial transceiver
static int com_connect(const char* device, long baudrate)
{
#if defined(RAW_OUTPUT)
	Raw_fd_ = fopen("raw_output.txt", "w");
#endif

	char adjust_device[16];
	_snprintf(adjust_device, 16, "\\\\.\\%s", device);           //第二种打开串口方式，可以打开COM10以上串口
	HCom = CreateFileA(adjust_device, GENERIC_READ | GENERIC_WRITE, 0,
		NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if (HCom == INVALID_HANDLE_VALUE) {
		return -1;
	}

	// Baud rate setting
	return com_changeBaudrate(baudrate);
}


static void com_disconnect(void)
{
	if (HCom != INVALID_HANDLE_VALUE) {
		CloseHandle(HCom);
		HCom = INVALID_HANDLE_VALUE;
	}
}


static int com_send(const char* data, int size)
{
	DWORD n;
	WriteFile(HCom, data, size, &n, NULL);   //WriteFile函数将数据写入一个文件。
	return n;
}


static int com_recv(char* data, int max_size, int timeout)      //串口读取
{
	if (max_size <= 0) {
		return 0;
	}

	if (ReadableSize < max_size) {                            
		DWORD dwErrors;
		COMSTAT ComStat;
		ClearCommError(HCom, &dwErrors, &ComStat);
		ReadableSize = ComStat.cbInQue;              //当前串口中存有的数据个数
	}

	if (max_size > ReadableSize) {
		COMMTIMEOUTS pcto;
		int each_timeout = 2;

		if (timeout == 0) {
			max_size = ReadableSize;

		}
		else {                                                            //   ReadableSize>max_size
			if (timeout < 0) {
				/* If timeout is 0, this function wait data infinity */   //如果timeout为0，此函数无限制等待数据
				timeout = 0;
				each_timeout = 0;
			}

			/* set timeout */
			GetCommTimeouts(HCom, &pcto);
			pcto.ReadIntervalTimeout = timeout;
			pcto.ReadTotalTimeoutMultiplier = each_timeout;
			pcto.ReadTotalTimeoutConstant = timeout;
			SetCommTimeouts(HCom, &pcto);
		}
	}

	DWORD n;
	ReadFile(HCom, data, (DWORD)max_size, &n, NULL);
#if defined(RAW_OUTPUT)
	if (Raw_fd_) {
		for (int i = 0; i < n; ++i) {
			fprintf(Raw_fd_, "%c", data[i]);
		}
		fflush(Raw_fd_);
	}
#endif
	if (n > 0) {
		ReadableSize -= n;
	}

	return n;
}


// The command is transmitted to URG
static int urg_sendTag(const char* tag)
{
	char send_message[LineLength];
	_snprintf(send_message, LineLength, "%s\n", tag);
	int send_size = (int)strlen(send_message);
	com_send(send_message, send_size);

	return send_size;
}


// Read one line data from URG    
static int urg_readLine(char *buffer)                      //LF,CR断句
{
	int i;
	for (i = 0; i < LineLength - 1; ++i) {
		char recv_ch;
		int n = com_recv(&recv_ch, 1, Timeout);           //没再接受到字节了返回0
		if (n <= 0) {
			if (i == 0) {
				return -1;              // timeout
			}
			break;
		}
		if ((recv_ch == '\r') || (recv_ch == '\n')) {
			break;
		}
		buffer[i] = recv_ch;                             //一个字节一个字节填
	}
	buffer[i] = '\0';

	return i;
}


// Trasmit command to URG and wait for response   向URG发送信息并且等待回应
static int urg_sendMessage(const char* command, int timeout, int* recv_n)
{
	int send_size = urg_sendTag(command);
	int recv_size = send_size + 2 + 1 + 2;     //回响+2位status+1位SUM+2位LF
	char buffer[LineLength];

	int n = com_recv(buffer, recv_size, timeout);
	*recv_n = n;

	if (n < recv_size) {
		// if received data size is incorrect
		return -1;
	}

	if (strncmp(buffer, command, send_size - 1)) {            //为何只比较send_size-1个字符？而不是整个send_size的字符
		// If there is mismatch in command
		return -1;
	}

	// !!! check checksum here		                          //SUM校验和不知道怎么搞，协议书里没有提到

	// Convert the response string into hexadecimal number and return that value
	//把得到的字符串转换为十六进制数并返回
	char reply_str[3] = "00";
	reply_str[0] = buffer[send_size];
	reply_str[1] = buffer[send_size + 1];                   //此处将status变换为长整型
	return strtol(reply_str, NULL, 16);                     //strtol函数会将参数nptr字符串根据参数base来转换成长整型数
}


// Change baudrate
static int urg_changeBaudrate(long baudrate)
{
	char buffer[] = "SSxxxxxx\r";
	_snprintf(buffer, 10, "SS%06d\r", baudrate);
	int dummy = 0;
	int ret = urg_sendMessage(buffer, Timeout, &dummy);

	if ((ret == 0) || (ret == 3) || (ret == 4)) {
		return 0;
	}
	else {
		return -1;
	}
}


// Read out URG parameter     读取URG参数
static int urg_getParameters(urg_state_t* state)      
{
	// Read parameter
	urg_sendTag("PP");
	char buffer[LineLength];
	int line_index = 0;
	enum {
		TagReply = 0,
		DataReply,
		Other,
	};
	int line_length;
	for (; (line_length = urg_readLine(buffer)) > 0; ++line_index) {

		if (line_index == Other + urg_state_t::MODL) {
			buffer[line_length - 2] = '\0';
			state->model = &buffer[5];

		}
		else if (line_index == Other + urg_state_t::DMIN) {
			state->distance_min = atoi(&buffer[5]);            //atoi (表示 ascii to integer)是把字符串转换成整型数的一个函数

		}
		else if (line_index == Other + urg_state_t::DMAX) {
			state->distance_max = atoi(&buffer[5]);

		}
		else if (line_index == Other + urg_state_t::ARES) {
			state->area_total = atoi(&buffer[5]);

		}
		else if (line_index == Other + urg_state_t::AMIN) {
			state->area_min = atoi(&buffer[5]);
			state->first = state->area_min;

		}
		else if (line_index == Other + urg_state_t::AMAX) {
			state->area_max = atoi(&buffer[5]);
			state->last = state->area_max;

		}
		else if (line_index == Other + urg_state_t::AFRT) {
			state->area_front = atoi(&buffer[5]);                           //atoi (表示 ascii to integer)是把字符串转换成整型数的一个函数,书面转换成数值

		}
		else if (line_index == Other + urg_state_t::SCAN) {
			state->scan_rpm = atoi(&buffer[5]);
		}
	}

	if (line_index <= Other + urg_state_t::SCAN) {
		return -1;
	}
	// Calculate the data size   计算数据长度
	state->max_size = state->area_max + 1;

	return 0;
}


/*!
\brief Connection to URG

\param state [o] Sensor information
\param port [i] Device
\param baudrate [i] Baudrate [bps]

\retval 0 Success
\retval < 0 Error
*/
static int urg_connect(urg_state_t* state,
	const char* port, const long baudrate)
{
	static char message_buffer[LineLength];

	if (com_connect(port, baudrate) < 0) {                        //如果波特率、串口设置失败
		_snprintf(message_buffer, LineLength,                     //_snprintf函数将可变个参数(...)按照format格式化成字符串，然后将其复制到str中
			"Cannot connect COM device: %s", port);              //写入错误信息到ErrorMessage内
		ErrorMessage = message_buffer;
		return -1;                                                
	}

	const long try_baudrate[] = { 19200, 115200, 38400 };
	size_t n = sizeof(try_baudrate) / sizeof(try_baudrate[0]);
	for (size_t i = 0; i < n; ++i) {

		// Search for the communicate able baud rate by trying different baud rate
		if (com_changeBaudrate(try_baudrate[i])) {
			ErrorMessage = "change baudrate fail.";
			return -1;
		}

		// Change to SCIP2.0 mode
		int recv_n = 0;
		urg_sendMessage("SCIP2.0", Timeout, &recv_n);
		if (recv_n <= 0) {
			// If there is difference in baud rate value,then there will be no
			// response. So if there is no response, try the next baud rate.
			continue;
		}

		// If specified baudrate is different, then change the baudrate

		if (try_baudrate[i] != baudrate) {
			urg_changeBaudrate(baudrate);

			// Wait for SS command applied.
			delay(100);

			com_changeBaudrate(baudrate);
		}

		// Get parameter
		if (urg_getParameters(state) < 0) {                            //用PP指令获取传感器参数，用来检测串口连接状态
			ErrorMessage =
				"PP command fail.\n"
				"This COM device may be not URG, or URG firmware is too old.\n"
				"SCIP 1.1 protocol is not supported. Please update URG firmware.";
			return -1;
		}
		state->last_timestamp = 0;

		// success        
		return 0;
	}

	// fail
	ErrorMessage = "no urg ports.";
	return -1;
}


/*!
\brief Disconnection
*/
static void urg_disconnect(void)
{
	com_disconnect();
}


/*!
\brief Receive range data by using GD command

\param state[i] Sensor information

\retval 0 Success
\retval < 0 Error
*/
static int urg_captureByGD(const urg_state_t* state)
{
	char send_message[LineLength];
	_snprintf(send_message, LineLength,
		"GD%04d%04d%02d", state->first, state->last, 1);
	   //GD0000000001
	return urg_sendTag(send_message);
}


/*!
\brief Get range data by using MD command

\param state [i] Sensor information
\param capture_times [i] capture times

\retval 0 Success
\retval < 0 Error
*/
static int urg_captureByMD(const urg_state_t* state, int capture_times)
{
	// 100 夞傪挻偊傞僨乕僞庢摼偵懳偟偰偼丄夞悢偵 00 (柍尷夞庢摼)傪巜掕偟丄
	// QT or RS 僐儅儞僪偱僨乕僞庢摼傪掆巭偡傞偙偲好好
	if (capture_times >= 100) {
		capture_times = 0;
	}

	char send_message[LineLength];
	_snprintf(send_message, LineLength, "MD%04d%04d%02d%01d%02d",
		state->first, state->last, 1, 0, capture_times);

	return urg_sendTag(send_message);
}


// Decode 6bit data
static long urg_decode(const char data[], int data_byte)
{
	long value = 0;
	for (int i = 0; i < data_byte; ++i) {
		value <<= 6;
		value &= ~0x3f;
		value |= data[i] - 0x30;   //拼接
	}
	return value;
}


// Receive range data
static int urg_addRecvData(const char buffer[], long data[], int* filled)
{
	static int remain_byte = 0;
	static char remain_data[3];
	const int data_byte = 3;

	const char* pre_p = buffer;
	const char* p = pre_p;

	if (*filled <= 0) {
		remain_byte = 0;
	}

	if (remain_byte > 0) {
		memmove(&remain_data[remain_byte], buffer, data_byte - remain_byte);            //void *memmove( void* dest, const void* src, size_t count );由src所指内存区域复制count个字节到dest所指内存区域 
		data[*filled] = urg_decode(remain_data, data_byte);
		++(*filled);
		pre_p = &buffer[data_byte - remain_byte];
		p = pre_p;
		remain_byte = 0;
	}

	do {
		++p;
		if ((p - pre_p) >= static_cast<int>(data_byte)) {
			data[*filled] = urg_decode(pre_p, data_byte);                              //三个三个字符打包、解码     buff[]:0~64位的字符数据（原始、未解码）   data[]：step_first~step_last，表示每个点的数据。每三位buff数据，解码得到一个点数据（距离），填充到data[]内
			++(*filled);
			pre_p = p;
		}
	} while (*p != '\0');
	remain_byte = (int)(p - pre_p);
	memmove(remain_data, pre_p, remain_byte);

	return 0;
}


static int checkSum(char buffer[], int size, char actual_sum)
{
	char expected_sum = 0x00;
	int i;

	for (i = 0; i < size; ++i) {
		expected_sum += buffer[i];
	}
	expected_sum = (expected_sum & 0x3f) + 0x30;

	return (expected_sum == actual_sum) ? 0 : -1;
}


/*!
\brief Receive URG data

應掕僨乕僞傪攝楍偵奿擺偟丄奿擺僨乕僞悢傪栠傝抣偱曉偡丅

\param state [i] Sensor information
\param data [o] range data
\param max_size [i] range data buffer size

\retval >= 0 number of range data
\retval < 0 Error
*/
static int urg_receiveData(urg_state_t* state, long data[], size_t max_size)
{
	int filled = 0;

	// fill -1 from 0 to first
	for (int i = state->first - 1; i >= 0; --i) {                     //暂时不知道是干嘛的    step0~step_first都填充-1
		data[filled++] = -1;
	}

	char message_type = 'M';                     //
	char buffer[LineLength];
	int line_length;
	for (int line_count = 0; (line_length = urg_readLine(buffer)) >= 0;                //此循环一直到没接受到字节才会自然中断，由此产生的自然中断是异常的 
	++line_count) {

		// check sum
		if ((line_count > 3) && (line_length >= 3)) {
			if (checkSum(buffer, line_length - 1, buffer[line_length - 1]) < 0) {    //校验和
				fprintf(stderr, "line_count: %d: %s\n", line_count, buffer);
				return -1;
			}
		}

		if ((line_count >= 6) && (line_length == 0)) {                   //最后的重复LF，代表结束符

			// 僨乕僞庴怣偺姰椆
			for (size_t i = filled; i < max_size; ++i) {
				// fill -1 to last of data buffer
				data[filled++] = -1;
			}
			return filled;                                                                      //此函数这里是正常出口

		}
		else if (line_count == 0) {                                      //适用于GD或者GE或者MD、ME
			// 憲怣儊僢僙乕僕偺嵟弶偺暥帤偱儊僢僙乕僕偺敾掕傪峴偆        //验证字头
			if ((buffer[0] != 'M') && (buffer[0] != 'G')) {
				return -1;
			}
			message_type = buffer[0];

		}
		else if (!strncmp(buffer, "99b", 3)) {                                         //这个函数用来比较s1和s2字符串的前maxlen个字符。如果两个字符串相等的话，strncmp将返回0。如果s1是s2的一个子串的话，s1小于s2。
			// "99b" 傪専弌偟丄埲崀傪乽僞僀儉僗僞儞僾乿乽僨乕僞乿偲傒側偡
			line_count = 4;

		}
		else if ((line_count == 1) && (message_type == 'G')) {
			line_count = 4;                                              //GD命令跳过了status的检测

		}
		else if (line_count == 4) {
			// "99b" 屌掕
			if (strncmp(buffer, "99b", 3)) {                   //99是status返回值，表示正常，b为99的校验和
				return -1;
			}

		}
		else if (line_count == 5) {
			state->last_timestamp = urg_decode(buffer, 4);    //解码             GD的timestamp 4字符编码的24位整型

		}
		else if (line_count >= 6) {
			// 庢摼僨乕僞
			if (line_length > (64 + 1)) {
				line_length = (64 + 1);                              //超过64个字节数据 +1 SUM
			}
			buffer[line_length - 1] = '\0';                          //前面已经校验过了，把SUM换成'\0'
			int ret = urg_addRecvData(buffer, data, &filled);
			if (ret < 0) {
				return ret;
			}
		}
	}
	return -1;
}


void outputData(long data[], int n, size_t total_index)
{
	char output_file[] = "data_xxxxxxxxxx.csv";
	_snprintf(output_file, sizeof(output_file), "data_%03d.csv", total_index);
	FILE* fd = fopen(output_file, "w");
	if (!fd) {
		perror("fopen");       //perror(s) 用来将上一个函数发生错误的原因输出到标准设备(stderr)。
		return;
	}

	for (int i = 0; i < n; ++i) {
		fprintf(fd, "%ld, ", data[i]);    //格式化输出至磁盘文件
	}
	fprintf(fd, "\n");

	fclose(fd);
}


int main(int argc, char *argv[])                                                      //**********   main   ****************
{
	// COM 億乕僩愝掕
	// 需要按照实际情况更改串口号
	const char com_port[] = "COM3";
	const long com_baudrate = 115200;

	// URG 状态
	urg_state_t urg_state;
	int ret = urg_connect(&urg_state, com_port, com_baudrate);                       //***  尝试连接，若错误打印错误信息
	if (ret < 0) {
		// 若urg连接出错，则打印出错信息
		printf("urg_connect: %s\n", ErrorMessage);

		// 按键退出
		getchar();
		exit(1);
	}

	int max_size = urg_state.max_size;
	long* data = new   long[max_size];

	enum { CaptureTimes = 5 };
	size_t total_index = 0;      //总索引数

	//////////////////////////////////////////////////////////////////////
	// *****************   使用GD指令：Get Data,获取所需要的雷达采集数据
	printf("using GD command\n");

	// GD 指令使用之前需要确保开启“BM”指令  ，切换激光雷达工作状态
	int recv_n = 0;
	urg_sendMessage("BM", Timeout, &recv_n);                              //BM指令用来切换传感器至测量状态，开始辐射激光并且开始测量。

	for (int i = 0; i < CaptureTimes; ++i) {
		urg_captureByGD(&urg_state);                                     //
		int n = urg_receiveData(&urg_state, data, max_size);
		if (n > 0) {
			printf("% 3d: front: %ld, urg_timestamp: %ld\n",
				i, data[urg_state.area_front], urg_state.last_timestamp);

			outputData(data, n, ++total_index);
		}
	}
	printf("\n");

	/////////////////////////////////////////////////////////////////////
	// MD 僐儅儞僪傪梡偄偨僨乕僞庢摼
	printf("using MD command\n");                                      //MD指令能使传感器返回指定状态下获取到的距离数据。

	urg_captureByMD(&urg_state, CaptureTimes);
	for (int i = 0; i < CaptureTimes; ++i) {
		int n = urg_receiveData(&urg_state, data, max_size);
		if (n > 0) {
			printf("% 3d: front: %ld, urg_timestamp: %ld\n",
				i, data[urg_state.area_front], urg_state.last_timestamp);

			outputData(data, n, ++total_index);
		}
	}
	// MD 僐儅儞僪偱偺庢摼偑姰椆偡傞偲丄儗乕僓偼帺摦徚摂偡傞

	// 偨偩偟丄100 夞埲忋偺僨乕僞庢摼傪巜掕偟偨応崌偵偼丄
	// urg_captureByMD() 撪晹偱柍尷夞偺僨乕僞庢摼偵愝掕偝傟偰偄傞偺偱丄
	// QT 僐儅儞僪傪梡偄偰丄柧帵揑偵僨乕僞掆巭傪峴偆
	if (CaptureTimes >= 100) {
		int dummy;
		urg_sendMessage("QT", Timeout, &dummy);                             //QT指令切换激光雷达的工作状态，停止测量并且切为空闲状态
	}

	urg_disconnect();
	delete[] data;

	printf("end.\n");

	// 懄嵗偵廔椆偟側偄偨傔偺張棟丅晄梫側傜偽嶍彍偡傞偙偲
	getchar();
	return 0;
}
