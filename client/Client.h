#ifndef CLIENT_H
#define CLIENT_H

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <fstream>
#include <string>
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
			throw std::runtime_error("[-] Socket Creation Failed!");
	}

	void downloadFile()
	{
		std::string str = "[*] Downloading ...";
		util::print(str);
		size_t res;

		try
		{
			// Get the size of the file
			int32_t netFileSize = 0;
			//std::cout << "\n[*] Before recv (netFileSize): " << std::hex << netFileSize;
			res = recv(clientSock, (char*)&netFileSize, sizeof(netFileSize), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving file size");
			}
			//std::cout << "\n[*] After recv (netFileSize): " << std::hex << netFileSize;
			size_t fileSize = ntohl(netFileSize);
			//std::cout << "\n[*] After recv (fileSize): " << std::dec << fileSize;
			//std::cout << "\n[*] Bytes received: " << std::dec << res;

			//std::cout << "\n[*] Bytes received: ";
			/*
			for (int i = 0; i < sizeof(netFileSize); i++)
			{
				unsigned char byte = (netFileSize >> (8 * i)) & 0xFF;
				std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
			}
			std::cout << std::dec << std::endl;
			*/

			// Receive file contents
			size_t bytesRead = 0;
			char* buffer = new char[fileSize];

			while (bytesRead < fileSize)
			{
				res = recv(clientSock, buffer + bytesRead, fileSize - bytesRead, 0);
				if (res == SOCKET_ERROR)
				{
					delete[] buffer;
					throw std::runtime_error("[-] Error receiving data");
				}
				if (res == 0) // Server closed connection
				{
					delete[] buffer;
					break;
				}

				bytesRead += res;
			}

			if (bytesRead != fileSize)
			{
				delete[] buffer;
				throw std::runtime_error("[-] Full file not received");
			}

			// Create file
			str = "[+] Downloaded Complete";
			util::print(str);
			std::string filename;
			while (true)
			{
				std::cout << "\033[2K\r[*] Save as: ";
				std::cin >> filename;
				if (filename.find_last_of('.') == std::string::npos || filename.find_last_of('.') == filename.length() - 1)
				{
					std::cout << "[!] Missing File Extension\n";
				}
				else
				{
					break;
				}
			}
			std::ofstream file(filename, std::ios::binary);
			if (file.is_open())
			{
				file.write(buffer, bytesRead);
				file.close();
			}
			else
			{
				throw std::runtime_error("[-] File Failed to open");
			}
			str = "[+] Saved!";
			util::print(str);
			delete[] buffer;
		}
		catch (std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);

		}
	}

	// Works, dont touch
	void uploadFile(std::string fileName)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";

			std::ifstream file(fileName, std::ios::binary | std::ios::ate);
			
			if (file.is_open())
			{
				//file.seekg(0, std::ios::end);  
				size_t fileSize = file.tellg(); // Get the file size
				//std::cout << "\n[*] Before send (fileSize): " << std::dec << fileSize;

				// Send the file size
				int32_t netFileSize = htonl(fileSize); // Convert file to network byte order
				//std::cout << "\n[*] Before send (netFileSize): " << std::hex << netFileSize;

				int res = send(clientSock, reinterpret_cast<char*>(&netFileSize),sizeof(netFileSize), 0);
				if (res == SOCKET_ERROR)
					throw std::runtime_error("[-] Failed to send file size");

				//std::cout << "\n[*] After send (netFileSize): " << std::hex << netFileSize;
				//std::cout << "\n[*] Bytes sent: " << std::dec << res;

				char* buffer = new char[fileSize];
				file.seekg(0, std::ios::beg);
				file.read(buffer, fileSize);  // Read the contents of the file into buffer 
				
				size_t bytesSent = 0;
				size_t bytesLeft = fileSize;
				while (bytesLeft > 0)
				{
					res = send(clientSock, buffer + bytesSent, bytesLeft, 0); // Send the file contents to the server
					if (res == SOCKET_ERROR)
					{
						delete[] buffer;
						file.close();
						throw std::runtime_error("[-] Failed to send file contents");
					}

					bytesSent += res;
					bytesLeft -= res;
				}
				
				file.close();
				delete[] buffer;
			}
			else
			{
				throw std::runtime_error("[-] Failed to open file");
			}
		}
		catch(std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);
			// send an error code here
		}
		std::string message = "[+] Upload Complete";
		util::print(message);
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

