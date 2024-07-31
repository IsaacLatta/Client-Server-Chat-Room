#include "ChatRoom.h"

int main()
{
	//std::string ip = "192.168.1.69";
	//std::vector <unsigned char> bin_ip = util::strToBin_IP(ip);
	//std::string new_ip = util::binToStr_IP(bin_ip);


	//std::cout << "[*] Original IP: " << ip;
	//std::cout << "\n[*] New IP: " << new_ip;

	ChatRoom chat_room;
	chat_room.run_chat_room();

	return 0;
}