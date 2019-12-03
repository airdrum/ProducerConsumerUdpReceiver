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
#include <algorithm>
#include <boost/crc.hpp>
#include <chrono>
#include <ctime>
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <string>  // string
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <linux/udp.h>
#include <iostream>

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>

using namespace std;

const int BUFFER_SIZE = 2000;
#define ETH_DATA_LEN 1512
#define UDP 0x11
#define SRC_ADDR "192.168.56.20"

#define DEBUG_PACKET  	0
mutex m; 				// LOCK for main_queue_thread
mutex m_print;			// LOCK for printer_thread



bool is_producer_empty = true;
bool is_consumer_empty = true;
bool is_consume_buffer = false;
bool is_transmission_finished = false;
bool print_loss =true;

// Buffer Sizes
int new_val_consume_buffer = 0;
int loss_buffer_size = 0;
int old_val_consume_buffer = 0;

bool print_ok = false; // printing out flag and for exiting the program
struct ReceiveBufferArray {
	uint8_t buf[ETH_DATA_LEN];
	int id;
	uint16_t checksum;
	string time;
};
std::queue<ReceiveBufferArray> main_queue_buffer;

vector<int> packetSize;
vector<int> consume_buffer;
vector<int> loss_buffer;
vector<int> crc_buffer;

vector<std::time_t> time_buffer;

boost::crc_32_type  crc;
int counter = 0;
int total_packet_count = 0;
int gmSocket;

struct sockaddr_in gmClientAddr;
struct sockaddr_in gmServerAddr;

socklen_t gmClientLen = sizeof(gmServerAddr);

std::string getCurrentTimeStamp(){
	using std::chrono::system_clock;
	auto currentTime = std::chrono::system_clock::now();
	char buffer[80];

	auto transformed = currentTime.time_since_epoch().count()/1000000;

	auto millis = transformed % 1000;

	std::time_t tt;

	tt = system_clock::to_time_t(currentTime);
	auto timeinfo = localtime(&tt);
	strftime(buffer,80,"%F %H:%M:%S",timeinfo);
	sprintf(buffer,"%s:%03d",buffer,(int)millis);
	return std::string(buffer);
}

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

	timeout.tv_sec = 1;
	timeout.tv_usec = 0;
	setsockopt(gmSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(gmSocket, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	setsockopt(gmSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	std::cout << "Socket has been opened. Ip: " << IpAddress << " - Port " << std::to_string(Port) << std::endl;
	return 0;
}


std::string return_current_time_and_date()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
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

void loss_calculator(vector<int> &vec, int old_val, int new_val){
	// only the old val will be added as number zero : successful packet. the new val will be handled in the next loops
	// after this function; do: -> old_val = new_val;
	int numel = new_val - old_val;
	//vec.push_back(1);// to comply the first old_val packet as 1
	if (numel==1)
		vec.push_back(0);
	else if(numel > 1){
		vec.push_back(0);
		for(int j=1;j<numel;j++)
			vec.push_back(1);
	}
	else if(numel<1){
		vec.push_back(0);
		for(int j=old_val;j<65535;j++)
			vec.push_back(1);
		for(int j=1;j<new_val;j++)
			vec.push_back(1);
	}


	//sleep_nanoseconds(0,10);
}

void consumer_thread()
{
	int packet_counter = 0;
	struct sockaddr_in source_socket_address, dest_socket_address;
	memset(&source_socket_address, 0, sizeof(source_socket_address));
	memset(&dest_socket_address, 0, sizeof(dest_socket_address));

	uint8_t ethernet_data[ETH_DATA_LEN];
	int old_val = 99999;
	while (!is_transmission_finished)
	{
		if (!main_queue_buffer.empty())
		{
			is_consume_buffer = true;

			// LOCK main_queue_buffer producer THREAD
			m.lock();

			std::copy(std::begin(main_queue_buffer.front().buf),std::end(main_queue_buffer.front().buf), std::begin(ethernet_data));
			uint16_t check_val = main_queue_buffer.front().checksum;
			main_queue_buffer.pop();

			// UNLOCK main_queue_buffer producer THREAD
			m.unlock();
			struct iphdr *ip_packet = (struct iphdr *)ethernet_data;
			if( DEBUG_PACKET)
			{
				
				//unsigned char * data = (ethernet_data + iphdrlen + sizeof(struct ethhdr) + sizeof(struct udphdr));

				struct ethhdr *eth = (struct ethhdr *)(ethernet_data);
				unsigned short iphdrlen;
				struct iphdr *ip = (struct iphdr *)( ethernet_data + sizeof(struct ethhdr) );
				/* getting actual size of IP header*/
				iphdrlen = ip->ihl*4;
				/* getting pointer to udp header*/
				struct udphdr *udp=(struct udphdr*)(ethernet_data + iphdrlen + sizeof(struct ethhdr));
				unsigned char *data = (ethernet_data + iphdrlen + sizeof(struct ethhdr) + sizeof(struct udphdr));
				int remaining_data = ETH_DATA_LEN - (iphdrlen + sizeof(struct ethhdr) + sizeof(struct udphdr));
	
				for(int i=0;i<remaining_data;i++)
				{
					
						printf("%.2X ",data[i]);
						
				}
				cout << endl;
				cout << endl;

			}
			// get packets coming from SRC_ADDR and UDP protocol
			if((ip_packet->saddr == inet_addr(SRC_ADDR)) && (ip_packet->protocol == UDP))
			{
				// LOCK PRINTER THREAD
				m_print.lock();

				consume_buffer.push_back(ntohs(ip_packet->id));
				if(old_val>65535)
					loss_calculator(loss_buffer, old_val, old_val+1);// for the first packet
				else
					loss_calculator(loss_buffer, old_val, ntohs(ip_packet->id));
				old_val = ntohs(ip_packet->id);

				// UNLOCK PRINTER THREAD
				m_print.unlock();
			}
			sleep_nanoseconds(0,1);

		}else if(main_queue_buffer.empty() && is_producer_empty && print_ok){
			// LOCK PRINTER THREAD
			m_print.lock();
			// Check if consume_buffer is empty
			if(consume_buffer.size()==0 && print_loss){ 
				// Print the final loss rate statistics
				
				std::cout <<"Total Packet: "<< total_packet_count
						 << ", The loss rate is : " 
						  << to_string((double)(loss_buffer_size - new_val_consume_buffer)/loss_buffer_size) 
						  << endl;
				print_loss = false;
				new_val_consume_buffer = 0;
				old_val_consume_buffer = 0;
				total_packet_count = 0;

			}
			// UNLOCK PRINTER THREAD
			m_print.unlock();
			//exit(0);
		}
		sleep_nanoseconds(0,1);
		is_consume_buffer = false;
	}
}
void producer_thread()
{
	int packet_size;

	openSocket(SRC_ADDR,5001);
	ReceiveBufferArray _rbuf;

	while (!is_transmission_finished)
	{
		packet_size = recvfrom(gmSocket , _rbuf.buf , ETH_DATA_LEN , 0 , NULL, NULL);
		// ADD CRC32 to ethernet data
		//_rbuf.checksum = checksum(_rbuf.buf,ETH_DATA_LEN);

		_rbuf.time = getCurrentTimeStamp();

		if (packet_size > 0)
		{
			is_producer_empty = false;
			// LOCK MAIN QUEUE THREAD
			m.lock();

			// Push ReceiverBuffer struct into --> main_queue_buffer
			main_queue_buffer.push(_rbuf);

			// UNLOCK MAIN QUEUE THREAD
			m.unlock();

		}else if (packet_size < 0){
			is_producer_empty = true;
		}
	}
}


void printer_thread()
{
	
// MONGODB database global variables-----------
mongocxx::instance inst{};
mongocxx::client conn{mongocxx::uri("mongodb://localhost:27017")};

bsoncxx::builder::stream::document document{};

auto collection = conn["testdb"]["testcollection"];
// MONGODB database global variables------------

	
	while(!is_transmission_finished){
		if(is_consume_buffer){

			// LOCK PRINTER THREAD
			m_print.lock();
			
			new_val_consume_buffer = consume_buffer.size();
			loss_buffer_size = loss_buffer.size();

			// UNLOCK PRINTER THREAD
			m_print.unlock();

			// Check if new_val_consume_buffer is still not 0 (meaning consume_buffer.size() gives something)
			// Then printout what is received.
			if(!(new_val_consume_buffer == 0)){
				
				//Enable print_ok flag for exiting the program
				print_ok = true;
				print_loss =true;
				// Printout the received UDP DATA statistics.
				if(counter >= 0){

					

					collection.insert_one(document.view());

					total_packet_count = total_packet_count + (new_val_consume_buffer - old_val_consume_buffer);
					document.clear();
					document << "time" << return_current_time_and_date();
					document << "datarate" << std::to_string((double)(ETH_DATA_LEN * (new_val_consume_buffer-old_val_consume_buffer)*8.0/1024.0/1024.0));
					document << "totalpacket" << total_packet_count;
					std::cout   << "Time: " << return_current_time_and_date()
								<<"," << counter<< " sec - capturedPacket: "
								<< (new_val_consume_buffer - old_val_consume_buffer)
								<< ", speed:"
								<< std::to_string((double)(ETH_DATA_LEN * (new_val_consume_buffer-old_val_consume_buffer)*8.0/1024.0/1024.0))
								<< " Mbits/sec, Loss number: "
								<< std::to_string(loss_buffer_size - new_val_consume_buffer)
								<< endl;
				}
				
				// There will be a portion to push this UDP data statistics into nosql database server.
				// ...... 
				// /////////////////////////////////////////////////////////////////////////////

				// make new_val_consume_buffer as the old_val_consume_buffer for next packet number calculation statistics
				old_val_consume_buffer = new_val_consume_buffer;
			}
			
			counter++;
			sleep(1);
		}else if (is_producer_empty){

			m_print.lock();
			
			// Clear consume_buffer
			consume_buffer.clear();
			// Clear loss_buffer
			loss_buffer.clear();

			// UNLOCK PRINTER THREAD
			m_print.unlock();
			counter = 0;
		}
	}



}

int main()
{
	setpriority(PRIO_PROCESS, 0, -20);
	thread cons(consumer_thread);
	thread prod(producer_thread);
	thread printer(printer_thread);

	prod.join();
	cons.join();
	printer.join();
	return 0;
}
