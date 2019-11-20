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


using namespace std;

const int BUFFER_SIZE = 2000;
#define ETH_DATA_LEN 1512

mutex m;
mutex m_print;
condition_variable producer_cv, consumer_cv;



struct ReceiveBufferArray {
	uint8_t buf[ETH_DATA_LEN];
};
vector<int> packetSize;
vector<int> consume_buffer;
std::queue<ReceiveBufferArray> qq;
int gmSocket;

struct sockaddr_in gmClientAddr;
struct sockaddr_in gmServerAddr;

socklen_t gmClientLen = sizeof(gmServerAddr);


struct sockaddr_in source_socket_address, dest_socket_address;



int openSocket(const std::string &IpAddress, int Port)
{

    int ret;
    struct timeval timeout;
    int optval = 1;



    //gmSocket = socket(AF_INET, SOCK_DGRAM, 0);
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




    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    setsockopt(gmSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(gmSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
    setsockopt(gmSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));




    std::cout << "Socket has been opened. Ip: " << IpAddress << " - Port " << std::to_string(Port) << std::endl;



    return 0;


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

void consumer_thread()
{
	int new_id = 0;
	int last_id = 0;
	int packet_counter = 0;

	memset(&source_socket_address, 0, sizeof(source_socket_address));
	memset(&dest_socket_address, 0, sizeof(dest_socket_address));
	uint8_t ethernet_data[ETH_DATA_LEN];



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
				std::cout << "id: " << std::to_string(ntohs(ip_packet->id)) << ", Packet Number: " << std::to_string(packet_counter) <<endl;
				printf("%2X-%2X\n",ethernet_data[4],ethernet_data[5]);
				packet_counter++;
			}




			usleep(100);

		}

		usleep(1000);
		//******************* U N L O C K***************************************************************


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
			m.lock();//##################################################3
			qq.push(_rbuf);
			m.unlock();//##################################################3
		}
	}
}



int main()
{

	//thread print

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