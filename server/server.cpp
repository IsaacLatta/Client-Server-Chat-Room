#include "ChatRoom.h"


std::string getSender(const std::string& message)
{
	int pos = message.find(' ');
	std::string sender = message.substr(0, pos + 1); // </sender>
	return sender;
}

std::string getCommand(const std::string& message)
{
	int pos = message.find(' ');
	int start = message.find(' ', pos + 1);
	std::string command = message.substr(pos + 1, start - pos); // /command
	return command;
}

std::string getRecipient(const std::string& message)
{
	int pos = message.find(' ');
	int start = message.find(' ', pos + 1);
	int end = message.find(' ', start + 1);
	std::string recipient = message.substr(start + 1, end - start);// </recipient>
	return recipient;
}

std::string getMessage(const std::string& message)
{
	std::string str = message;
	int pos = message.find(' ');
	int start = message.find(' ', pos + 1);
	int end = message.find(' ', start + 1);
	end = message.find(' ', end);
	str.erase(0, end + 1); // message
	return str;
}

int main()
{
	ChatRoom chat_room;
	chat_room.run_chat_room();

	/*
	std::string message = "</sender> /upload </recipient> filename";
	int res;

	if (getCommand(message) == "/upload ")
	{
		std::cout << "[*] " << getSender(message) << "Requested File Transfer: " << getMessage(message);
		std::cout << "\n[*] Press 1 to approve: ";
		std::cin >> res;
		if (res == 1)
		{
			std::cout << "[*] Downloading file ...";
			Sleep(2000);
			std::cout << "\n[+] Downloaded!";
		}
		else
			std::cout << "[+] File Transfer Denied!";
			
	}
	*/
	//std::cout << message;
	
	return 0;
}