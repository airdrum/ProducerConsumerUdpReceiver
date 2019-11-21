//============================================================================
// Name        : Producer_Consumer_Socket.cpp
// Author      : Samet Yıldız
// Version     :
// Copyright   : Your copyright notice
// Description : Hello World in C++, Ansi-style
//============================================================================
#include <iostream>
#include <thread>
#include <array>
#include <vector>
#include <mutex>
#include <string>
#include <unistd.h>
#include <condition_variable>
#include <queue>
#include <algorithm>
#include <string.h>
#include <netinet/ip.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <chrono>
#include <sys/time.h>
#include <ctime>
#include <numeric>
using namespace std;

const int BUFFER_SIZE = 2000;
#define ETH_DATA_LEN 1512

mutex m;
mutex m_print;

bool is_qq_empty = true;

struct ReceiveBufferArray {
	uint8_t buf[ETH_DATA_LEN];
	int id;
	time_t time;
};
vector<int> packetSize;
vector<int> consume_buffer;
vector<int> loss_buffer;
vector<std::time_t> time_buffer;
std::queue<ReceiveBufferArray> qq;
int gmSocket;

struct sockaddr_in gmClientAddr;
struct sockaddr_in gmServerAddr;

socklen_t gmClientLen = sizeof(gmServerAddr);

int openSocket(const std::string &IpAddress, int Port)
{

	int ret;
	struct timeval timeout;
	int optval = 1;

	gmSocket = socket(AF_INET, SOCK_RAW, IPPROTO_UDP);
	if (gmSocket < 0)
	{
		std::cout << "cannot Open datagram socket!! Ip: " << IpAddress << " - Port " << std::to_string(Port) << std::endl;

		return -1;
	}
	/* Bind our local address so that the client can send to us */
	gmServerAddr.sin_family = AF_INET;
	gmServerAddr.sin_addr.s_addr =INADDR_ANY;
	gmServerAddr.sin_port = htons(Port);

	timeout.tv_sec = 3;
	timeout.tv_usec = 0;
	setsockopt(gmSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(gmSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	setsockopt(gmSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	std::cout << "Socket has been opened. Ip: " << IpAddress << " - Port " << std::to_string(Port) << std::endl;
	return 0;
}


void sleep_nanoseconds(long sec, long nanosec)
{
	struct timespec ts;
	ts.tv_sec  = sec;
	ts.tv_nsec = nanosec;

	if(nanosleep(&ts, NULL) == -1)
	{
		perror("nanosleep");
		exit(EXIT_FAILURE);
	}
}


void print_thread(){
	while(true){
		m_print.lock();//##################################################3
		if(!consume_buffer.empty())
			printf("%d\n",consume_buffer.size());
		m_print.unlock();//##################################################3
		usleep(100);
	}
}

void loss_calculator(vector<int> &vec, int old_val, int new_val){
	// only the old val will be added as number zero : successful packet. the new val will be handled in the next loops
	// after this function; do: -> old_val = new_val;
	int numel = new_val - old_val;
	vec.push_back(1);// to comply the first old_val packet as 1
	if (numel==1)
		vec.push_back(numel);
	else if(numel > 0)
		for(int j=1;j<numel;j++)
			vec.push_back(0);
	else if(numel<0){
		for(int j=old_val;j<65535;j++)
			vec.push_back(0);
		for(int j=1;j<new_val;j++)
			vec.push_back(0);
	}

	if(new_val==1){
		vec.push_back(1);//the packet identification number 1
	}

}

void consumer_thread()
{
	int new_id = 0;
	int last_id = 0;
	int packet_counter = 0;
	struct sockaddr_in source_socket_address, dest_socket_address;
	memset(&source_socket_address, 0, sizeof(source_socket_address));
	memset(&dest_socket_address, 0, sizeof(dest_socket_address));
	uint8_t ethernet_data[ETH_DATA_LEN];

	int old_val;

	while (true)
	{
		//m.lock();//******************* L O C K***************************************************************
		//##################################################3
		if (!qq.empty())
		{
			m.lock();

			for(int i=0; i < ETH_DATA_LEN;i++ )
				ethernet_data[i] = qq.front().buf[i];

			qq.pop();

			m.unlock();
			//##################################################3
			struct iphdr *ip_packet = (struct iphdr *)ethernet_data;

			if(ip_packet->saddr == inet_addr("192.168.2.20"))
			{
				consume_buffer.push_back(ntohs(ip_packet->id));
				loss_calculator(loss_buffer, old_val, ntohs(ip_packet->id));
				old_val = ntohs(ip_packet->id);
				std::cout << "id: " << std::to_string(ntohs(ip_packet->id)) << ", Packet Number: " << std::to_string(consume_buffer.size()) <<endl;
				packet_counter++;
			}
			usleep(1);

		}else if(qq.empty() && is_qq_empty){
		//	m_print.lock();
			if(consume_buffer.size()>0)
				consume_buffer.clear();
			//m_print.unlock();
			//cout << accumulate(loss_buffer.begin(),loss_buffer.end(),0) <<endl; // ???????
			usleep(1);
			//******************* U N L O C K***************************************************************


		}
	}
}
void producer_thread()
{
	int packet_size;

	openSocket("192.168.2.20",5001);
	ReceiveBufferArray _rbuf;

	while (true)
	{


		packet_size = recvfrom(gmSocket , _rbuf.buf , ETH_DATA_LEN , 0 , NULL, NULL);

		if (packet_size > 0)
		{
			is_qq_empty = false;
			m.lock();//##################################################3
			qq.push(_rbuf);
			m.unlock();//##################################################3
		}else if (packet_size < 0){
			is_qq_empty = true;
			//packet_counter = 0;

			//cout << "###packet_counter=" << packet_counter << endl;
		}
	}
}



int main()
{

	//thread print
	/* LOSS calculation
	loss_calculator(loss_buffer, 20, 24);
	loss_calculator(loss_buffer, 65530, 1);
	loss_calculator(loss_buffer, 1, 18);
	for (int i =0;i<loss_buffer.size();i++)
		printf("%d,",loss_buffer.at(i));
	*/
	setpriority(PRIO_PROCESS, 0, -20);
	thread cons(consumer_thread);
	thread prod(producer_thread);
	cons.join();
	prod.join();
	while(1);
	//producer_thread();

	system("pause");
	return 0;
}
