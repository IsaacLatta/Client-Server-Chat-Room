#ifndef CLIENT_H
#define CLIENT_H

#include "FileTransfer.h"
#include <iomanip>
#include "Util.h"

#pragma comment (lib,  "Ws2_32.lib")

class Client
{
private:
	int SERVER_LISTENING_PORT = 50000;
	static const int BUFFER_SIZE = 1024;
	std::string username;

	WSADATA wsaData;
	sockaddr_in server;
	SOCKET clientSock;

public:

	// Close the clients connection
	void closeClient()
	{
		closesocket(clientSock);
		WSACleanup();
		util::print("[+] Connection Closed.");
		std::cout << "\033[2K\r"; // Remove "> " from terminal
	}

	void initializeClient()
	{
		// Initialize WSA 
		int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (res == SOCKET_ERROR)
			throw std::exception("[-] WSA startup failed");

		// Initialize socket
		clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (clientSock == INVALID_SOCKET)
			throw std::runtime_error("[-] Client Socket Creation Failed!");
	}

	void downloadFile()
	{
		size_t res;
		std::string str;
		std::cout << "\033[2K\r[+] Downloading ...";
		try
		{
			FileTransfer ft;
			TransferStatus download_status = ft.download();
			if (download_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Transfer Failed");
			}
			// Check here for other error codes later

			str = "[+] Downloaded Complete";
			util::print(str);

			std::string filename;
			while (true)
			{
				std::cout << "\033[2K\r[*] Save as: ";
				std::cin >> filename;

				TransferStatus file_status = ft.saveFile(filename);
				if (file_status == TransferStatus::MISSING_FILE_EXTENSION)
				{
					std::cout << "[!] Missing File Extension\n";
				}
				else if (file_status != TransferStatus::SUCCESS)
				{
					std::cout << "[-] Error Occured\n";
					std::cout << "[*] Try again (y/n): ";
					std::cin >> str;
					if (str == "n" || str == "N")
					{
						str = "[-] File Not Saved";
						util::print(str);
						break;
					}
				}
				else
				{
					std::cout << "\033[2K\r[+] Saved\n";
					break;
				}
			}
		}
		catch (std::exception& e)
		{
			std::string message = std::string(e.what());
			util::print(message);
		}
	}

	void uploadFile(std::string fileName)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";
		
			// Get the IP, and port of the receiving client
			unsigned net_port;
			int res = recv(clientSock, reinterpret_cast<char*>(&net_port), sizeof(net_port), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving port");
			}
			unsigned port = ntohs(net_port);

			std::string peer_IP = recvIP();
			//std::string log = "IP Recv: " + peer_IP;
			//util::debug_log(log);
			
			// Debugging
			//log =  "Port Recv: " + std::to_string(port);
			//util::debug_log(log);

			port = 51000;
			FileTransfer ft(peer_IP, port);
			//FileTransfer ft(peer_IP, 51000);
			TransferStatus upload_status = ft.upload(fileName);
			if (upload_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Transfer Failed");
			}
			std::string message = "[+] Upload Complete";
			util::print(message);
		}
		catch(std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);
		}
	}

	void connectToServer(std::string& ip_address)
	{
		// Initialize Server sockaddr_in struct
		server.sin_family = AF_INET; // Set IPv4
		server.sin_port = htons(SERVER_LISTENING_PORT); // Set server port to connect to
		inet_pton(AF_INET, ip_address.c_str(), &server.sin_addr); // Convert SERVER_IP into network byte order for transmission

		// Connect to server
		int res = connect(clientSock, (sockaddr*)&server, sizeof(server));
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Connection Failed");
	}

	// Receives messages
	std::string recvMessage()
	{
		char buffer[BUFFER_SIZE];
		memset(buffer, 0, BUFFER_SIZE); // Zero buffer before receiving response

		int res = recv(clientSock, buffer, sizeof(buffer), 0);
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Message not received! ");

		return std::string(buffer, 0, res);
	}

	std::string recvIP()
	{
		char IP[INET_ADDRSTRLEN];

		int res = recv(clientSock, IP, INET_ADDRSTRLEN, 0);
		if (res == SOCKET_ERROR)
		{
			return "";
		}

		return std::string(IP, 0, res);
	}

	void setUsername(const std::string& username)
	{
		this -> username = username;
	}

	std::string getUsername()
	{
		return username;
	}

	void sendMessage(std::string msg)
	{
		msg = username + msg;
		
		int res = send(clientSock, msg.c_str(), msg.length(), 0);
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Error: Message not sent!");
	}

};
#endif

