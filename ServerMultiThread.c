#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <wait.h>
#include <softPwm.h>
#include <wiringPi.h>

// mapping for wiringiPi gpio style
// right motor
#define PIN_7 7
#define PIN_11 0

// right motor software PWM
#define PIN_16 4

// left motor
#define PIN_12 1
#define PIN_13 2

// left motor software PWM
#define PIN_15 3

// speed
#define TOP_GEAR 20
#define MID_GEAR 16
#define LOW_GEAR 11

// us-015
// trigger
#define PIN_18 5

// echo
#define PIN_22 11

// safe distance mm
#define SAFE_DISTANCE 150

// timeout for taking distance
#define DURATION_TIMEOUT 100000

void forward();
void back();
void stop();
void left();
void right();
void changeGear(int gear);
int pulseIn(int pin, int level, int timeout);
int distance();

int main()
{
	// Clear all pin states
	stop();

	int server_socket, client_socket, status;
	socklen_t server_len, client_len;
	struct sockaddr_in server_sockaddr, client_sockaddr;

	char gstStartCommand[512];
	char clientIpAddr[16];
	int clientPort;

	strcpy(gstStartCommand, "raspivid -rot 180 -t 999999 -h 480 -w 640 -fps 25 -b 2000000 -o - | gst-launch-0.10 -v fdsrc fd=0 ! h264parse ! rtph264pay ! udpsink host=");
	//bool isGstreamerStreaming = false;

	// initialise wiringPi
	wiringPiSetup();

	// right motor
	pinMode(PIN_7, OUTPUT);
	pinMode(PIN_11, OUTPUT);

	// left motor
	pinMode(PIN_12, OUTPUT);
	pinMode(PIN_13, OUTPUT);

	// us-015 trigger
	pinMode(PIN_18, OUTPUT);

	// us-015 echo
	pinMode(PIN_22, INPUT);

	server_socket = socket (AF_INET, SOCK_STREAM, 0);

	if ( server_socket < 0)
	{
		perror("Socket");
		return -1;
	}

	// Socket structure
	server_sockaddr.sin_family = AF_INET;
	server_sockaddr.sin_port = htons(5000);
	server_sockaddr.sin_addr.s_addr = htonl (INADDR_ANY);

	server_len = sizeof(server_sockaddr);

	if ( ( bind ( server_socket, ( struct sockaddr* ) &server_sockaddr, server_len ) ) < 0 )
	{
		perror("Bind");
		return -1;
	}

	if ( listen(server_socket, 10) == -1 )
	{
		perror("Listen");
		exit(1);
	}

	client_len = sizeof(client_sockaddr);

	while ( 1 )
	{
		int number = 0;

		// after dc
		system("killall raspivid");

		printf("\nServer is waiting...\n");
		client_socket = accept(server_socket, (struct sockaddr* ) &client_sockaddr, &client_len);

		if( client_socket == -1)
		{
			perror("Accept");
			continue;
		}

		printf("\nAccpeted!\n");
		strcpy(clientIpAddr, inet_ntoa(client_sockaddr.sin_addr));
		clientPort = ntohs(client_sockaddr.sin_port);

		printf("CLIENT IP address is: %s\n", clientIpAddr);

		printf("CLIENT PORT is: %d\n", clientPort);

		strcat(gstStartCommand, clientIpAddr);

		strcat(gstStartCommand, " port=8554 &");

		printf("\nCMD:%s\n", gstStartCommand);

		// Create client process
		pid_t pid = fork();

		if ( pid < 0 )
		{
			perror("Fork client process.");
			exit(1);
		}

		if ( pid == 0 )
		{
			// Client
			printf("\nclient pid\n");
			close (server_socket);

			softPwmCreate(PIN_15, MID_GEAR, TOP_GEAR);
			softPwmCreate(PIN_16, MID_GEAR, TOP_GEAR);

			//softPwmWrite(PIN_15, TOP_GEAR);
			//softPwmWrite(PIN_16, TOP_GEAR);

			while( 1 )
			{
				read(client_socket, &number, sizeof(int));

				switch(number)
				{
					case 8:
						//do something
						//printf("\nForward");
						forward();
					break;
					case 4:
						//do something
						//printf("\nLeft");
						left();
					break;
					case 6:
						//do something
						//printf("\nRight");
						right();
					break;
					case 2:
						//do something
						//printf("\nBack");
						back();
					break;
					case 5:
						//do something
						//printf("\nStop");
						stop();
					break;
					case 1:
						//low gear
						//changeGear(LOW_GEAR);
						softPwmWrite(PIN_15, LOW_GEAR);
						softPwmWrite(PIN_16, LOW_GEAR);
					break;
					case 3:
						//top gear
						//changeGear(TOP_GEAR);
						softPwmWrite(PIN_15, TOP_GEAR);
						softPwmWrite(PIN_16, TOP_GEAR);
					break;
					case 7:
						//mid gear
						softPwmWrite(PIN_15, MID_GEAR);
						softPwmWrite(PIN_16, MID_GEAR);
					break;
					case 9:
						//start
						system("killall raspivid");
						delayMicroseconds(10000);
						system(gstStartCommand);
						//isGstreamerStreaming = true;
						printf("gstreamer stream enabled.");
					break;
					case 10:
						//stop
						system("killall raspivid");
						//isGstreamerStreaming = false;
						printf("gstreamer stream disabled.");
					break;

					default:
						printf("\nWrong signal number");
					break;
				}

			} // end of while == 1

		} // end of Client pid == 0
		else
		{
			// Create background job process
			pid_t pid_bg_job = fork();

			if ( pid_bg_job < 0 )
			{
				perror("Fork bg job process.");
				exit(1);
			}

			if ( pid_bg_job == 0 )
			{
				printf("\nbg job pid\n");
				int mmDistance;
				while ( 1 )
				{
					mmDistance = distance();
					printf("\nmmDistance: %d | SAFE_DISTANCE: %d\n", mmDistance, (SAFE_DISTANCE + TOP_GEAR));
					if ( mmDistance <= SAFE_DISTANCE + TOP_GEAR)
					{
						stop();
						printf(" Can not move forward! ");
					}
					else
					{
						printf(" Distance ok. ");
					}

					// 500 ms
					delayMicroseconds(250000);
				}
			} // end of pid_bg_job == 0
			else
			{

			printf("\nserver pid\n");

			// use the poll system call to be notified about socket status changes
			struct pollfd pfd;
			pfd.fd = client_socket;
			pfd.events = POLLIN | POLLHUP | POLLRDNORM;
			pfd.revents = 0;

			while ( pfd.revents == 0 )
			{
				char buffer[32];

				// send control msg
				int echo = send ( client_socket, buffer, 1, MSG_NOSIGNAL );

				// call poll with a timeout of 100 ms
				if ( poll(&pfd, 1, 100) > 0 )
				{
					// if result > 0, this means that there is either data available on the
					// socket, or the socket has been closed
					if ( ( recv ( client_socket, buffer, sizeof(buffer), MSG_PEEK | MSG_DONTWAIT ) <= 0 )
					|| ( echo == -1 ) )
					{
						// stop device to avoid crash
						stop();

						// if recv returns zero, that means the connection has been closed:
						// kill the child process

						kill(pid, SIGKILL);
						kill(pid_bg_job, SIGKILL);
						waitpid(pid, &status, WNOHANG);
						waitpid(pid_bg_job, &status, WNOHANG);
						close(client_socket);
					}
				}

			} // end of while pfd.revents == 0
			} // end of else pid_bg_job == 0

		} // end of else Client pid == 0
	} // end of while

	return 0;
}

void changeGear(int gear)
{
	softPwmWrite ( PIN_15, gear );
	softPwmWrite ( PIN_16, gear );
	gear == TOP_GEAR ? printf("\nTOP GEAR") : printf("\nLOW GEAR");
}

int distance()
{
	//printf("distance() function start\n");
        digitalWrite(PIN_18, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_18, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_18, LOW);
	//printf("distance() after digitalWrite before pulseIn\n");
	//printf("\nLOG : distance %d cm.", pulseIn(PIN_22, HIGH, DURATION_TIMEOUT));

	return ( pulseIn(PIN_22, HIGH, DURATION_TIMEOUT) * 34 / 100 ) / 2;
}

void forward()
{
	printf("\nforward");
	digitalWrite(PIN_7, HIGH);
	digitalWrite(PIN_11, LOW);
	digitalWrite(PIN_12, LOW);
	digitalWrite(PIN_13, HIGH);

}

int pulseIn(int pin, int level, int timeout)
{
	//printf("pulseIn() start\n");
	struct timeval tn, t0, t1;

	long micros;

	gettimeofday(&t0, NULL);

	micros = 0;

	//printf("pulseIn() before while\n");
	while ( digitalRead(pin) != level )
	{
		gettimeofday(&tn, NULL);

		if ( tn.tv_sec > t0.tv_sec ) micros = 1000000L; else micros = 0;
		micros += (tn.tv_usec - t0.tv_usec);

		if ( micros > timeout ) return micros;
	}

	//printf("pulseIn() after while\n");

	gettimeofday(&t1, NULL);

	while ( digitalRead(pin) == level )
	{
		gettimeofday(&tn, NULL);

		if ( tn.tv_sec > t0.tv_sec ) micros = 1000000L; else micros = 0;
		micros = micros + (tn.tv_usec - t0.tv_usec);

		if ( micros > timeout ) return micros;
	}

	if ( tn.tv_sec > t1.tv_sec ) micros = 1000000L; else micros = 0;
	micros = micros + (tn.tv_usec - t1.tv_usec);
	//printf("pulseIn() before return micros\n");

	return micros;
}

void stop()
{
	printf("\nstop");
	digitalWrite(PIN_7, LOW);
	digitalWrite(PIN_11, LOW);
	digitalWrite(PIN_12, LOW);
	digitalWrite(PIN_13, LOW);
}

void back()
{
	printf("\nback");
	digitalWrite(PIN_7, LOW);
	digitalWrite(PIN_11, HIGH);
	digitalWrite(PIN_12, HIGH);
	digitalWrite(PIN_13, LOW);
}

void left()
{
	printf("\nleft");
	digitalWrite(PIN_7, HIGH);
        digitalWrite(PIN_11, LOW);
        digitalWrite(PIN_12, LOW);
        digitalWrite(PIN_13, LOW);
}

void right()
{

	printf("\nright");
	digitalWrite(PIN_7, LOW);
        digitalWrite(PIN_11, LOW);
        digitalWrite(PIN_12, LOW);
        digitalWrite(PIN_13, HIGH);
}
