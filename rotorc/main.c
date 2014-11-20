/* Code from Magnus Jonsson/SA2BRJ <fot@fot.nu> */
/* Code is available under the GPL license      */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/select.h>

#define BAUD B1200
#define DEVICE "/dev/ttyUSB0"
#define _POSIX_SOURCE 1 
#define FALSE 0
#define TRUE 1

#define BASE "/var/run/lock/rotor."

#define CURR BASE "curr"
#define AIM  BASE "aim"

volatile int STOP=FALSE; 

int readStatus(int fd) {
	char buf[255];
	int i,res,cpos=0;
	fd_set readfs;
	struct timeval Timeout;

	bzero(&readfs,sizeof(readfs));
	
	while (1) {       /* loop for input */
		FD_SET(fd, &readfs);  /* set testing for source 1 */
		Timeout.tv_usec = 0;  /* milliseconds */
		Timeout.tv_sec  = 2;  /* seconds */
		res = select(fd+1, &readfs, NULL, NULL, &Timeout);
		if(FD_ISSET(fd, &readfs)) {
			res = read(fd,&buf[cpos],255);
			if(res > 0) {
				cpos+=res;
				for(int i=4;i<res+cpos;i++) {
					if(buf[i] == 0x20) {
						int pos = buf[i-3]*100 + buf[i-2]*10 + buf[i-1];
						tcflush(fd, TCIFLUSH);
						return pos;
					}
				}
			}
		} else {
			tcflush(fd, TCIFLUSH);
			return -1;
		}
	}
	tcflush(fd, TCIFLUSH);
	return -1;
}

int slowWrite(int fd,char *buf,int size) {
	int res = 0;
	for(int i=0;i<size;i++) {
		int t = write(fd,&buf[i],1);
		if(t<0) {
			return t;
		}
		res += t;
		usleep(10*1000);
	}
	return res;
}

int writeStatus(int fd) {
	char buf[14] = { 'W', 0,0,0,0,0, 0,0,0,0,0, 0x1F,0x20 };
	tcflush(fd, TCIFLUSH);
	int res = slowWrite(fd,buf,13);
	return res == 13;
}

int writeStop(int fd) {
	char buf[14] = { 'W', 0,0,0,0,0, 0,0,0,0,0, 0x0F,0x20 };
	tcflush(fd, TCIFLUSH);
	int res = slowWrite(fd,buf,13);
	return res == 13;
}

int writeDeg(int fd, int deg) {
	char buf[14] = { 'W', 0,0,0,'0',1, 0,0,0,0,0, 0x2F,0x20 };
	buf[1] = ((deg / 100) % 10) + '0';
	buf[2] = ((deg / 10) % 10) + '0';
	buf[3] = (deg % 10) + '0';
	tcflush(fd, TCIOFLUSH);
	int res = slowWrite(fd,buf,13);
	return res == 13;
}

#define ABS(x) ((x)<0?-(x):(x))

int getAim() {
	FILE *in;
	in = fopen(AIM,"r");
	if(!in) {
		return -1;
	}
	char buff[1024];
	if(!fgets(buff,sizeof(buff),in)) {
		fclose(in);
		return -1;
	}
	fclose(in);
	if(strcmp(buff,"stop") == 0) {
		return -1;
	}
	for(int i=0;i<sizeof(buff);i++) {
		if(buff[i] == 0 || buff[i] == '\r' || buff[i] == '\n') {
			buff[i] = 0;
			return atoi(buff);
		}
	}
	return -1;
}

void writeCurr(int curr,int aim) {
	FILE *out;
	out = fopen(CURR ".tmp","w");
	if(!out) {
		return;
	}
	fprintf(out,"%d:%d",curr,aim);
	fclose(out);
	rename(CURR ".tmp",CURR);
}


int main(int argc, char *argv[])
{
	int fd,c, res,i;
	struct termios oldtio,tio;
	char buf[255];

	fd = open(DEVICE, O_RDWR | O_NOCTTY | O_NONBLOCK); 
	if (fd <0) {perror(DEVICE); exit(-1); }

	tcgetattr(fd,&oldtio); /* save current port settings */

	bzero(&tio, sizeof(tio));
	tio.c_iflag = IGNPAR;
	tio.c_cflag = BAUD | CLOCAL | CS8 | CREAD;
	tio.c_oflag = 0;

	tio.c_cc[VMIN]     = 5;   /* blocking read until 5 chars received */

	tcflush(fd, TCIOFLUSH);
	tcsetattr(fd,TCSANOW,&tio);

	if(argc > 1) {
		writeStop(fd);
		printf("StopStatus: %d\n",readStatus(fd));
		return 0;
	}

	writeStop(fd);
	int start = readStatus(fd);
	if(start == -1) {
		printf("Failed to stop\n");
		return 1;
	}
	writeCurr(start,start);
	while(1) {
		int aim = getAim();
		if(aim == -1 || aim == start) { // Nothing new to do :-(
			sleep(1);
			continue;
		}
		writeCurr(start,aim);
		int small_aim = aim;
		if(ABS(start-aim) > 45) {
			if(start > aim) {
				small_aim = start - 45;
			} else {
				small_aim = start + 45;
			}
		}
		printf("Start: %d\n",start);
		printf("Real Aim: %d\n",aim);
		printf("Small Aim: %d\n",small_aim);
		writeDeg(fd,small_aim);
		int curr = -1;
		int last = -1;
		int stop = 0;
		while(small_aim != curr) {
			sleep(1);
			if(getAim() == -1) {
				printf("stop\n");
				stop = 1;
				break;
			}
			writeStatus(fd);
			curr = readStatus(fd);
			writeCurr(curr,aim);
			printf("curr: %d (%d > %d == bad)\n",curr, ABS(curr-aim) , ABS(start-aim) );
			if(ABS(curr-aim) > ABS(start-aim)) {
				printf("Wrong dir... exit\n");
				break;
			}
			int c = 2;
			while(curr == last) {
				if(c-- > 0) {
					printf("not turning... retry\n");
					sleep(1);
					writeStatus(fd);
					curr = readStatus(fd);
					writeCurr(curr,aim);
					if(curr != last) {
						break;
					}
				}
				break;
			}
			if(curr == last) {
				break;
			}
			last = curr;
		}
		sleep(1);
		writeStop(fd);
		sleep(1);
		start = readStatus(fd);
		if(stop) {
			aim = start;
		}
		writeCurr(start,aim);
		printf("StopStatus: %d\n",start);
	}

	tcsetattr(fd,TCSANOW,&oldtio);
	return 0;
}
