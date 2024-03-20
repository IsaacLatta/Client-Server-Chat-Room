#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <fstream>
#include <ostream>
#include "FileTransfer.h"
//#include <Winsock2.h>
//#include <Ws2tcpip.h>
#include <stdexcept>
#include <iomanip>
#include <memory>
#include "ThreadPool.h"
#include "Util.h"
#include "User.h"

#pragma comment (lib,  "Ws2_32.lib")

enum class FileStatus : uint8_t
{
	SUCCESS = 0x01, FAILURE = 0x02, MISSING_FILE_EXTENSION = 0x03,
	OPEN_ERROR = 0x04, 
};

class Server
{

private:
	sockaddr_in server;
	WSADATA wsaData;
	SOCKET listeningSocket;
	const int LISTENING_SERVER_PORT = 50000;
	int BUFFER_SIZE = 1024;
	std::string IP = "10.50.170.173"; // For file sharing

	std::vector <std::unique_ptr<User>> users;
	std::mutex usersMutex;

	std::condition_variable shutdownCondition;
	std::mutex shutdownMutex;
	std::mutex file_mutex;
	std::string fileName;

	ThreadPool threadPool;

	void shutdown()
	{
		for (auto& user : users)
		{
			closesocket(user->getSocket());
		}

		WSACleanup();
		closesocket(listeningSocket);
		util::print("[+] Server Closed");
		std::cout << "\033[2K\r";
	}

	TransferStatus recvFile(char* buffer, int fileSize, size_t bufferSize, SOCKET sock)
	{
		if (buffer == nullptr || bufferSize < fileSize)
			return TransferStatus::SIZE_MISMATCH;

		// Receive file contents
		size_t bytesRead = 0;
		while (bytesRead < fileSize)
		{
			int res = recv(sock, buffer + bytesRead, fileSize - bytesRead, 0);
			if (res == SOCKET_ERROR)
			{
				return TransferStatus::FAILURE;
			}
			if (res == 0) // Server closed connection
			{
				if(bytesRead == fileSize)
				{ 
					return TransferStatus::SUCCESS;
				}
				return TransferStatus::CONNECTION_CLOSED;
			}

			bytesRead += res;
		}

		if (bytesRead != fileSize)
		{
			return TransferStatus::INCOMPLETE_RECV;
		}

		return TransferStatus::SUCCESS;
	}

	size_t recvFileSize(SOCKET sock)
	{
		// Get the size of the file
		int32_t netFileSize = 0;
		int res = recv(sock, (char*)&netFileSize, sizeof(netFileSize), 0);
		if (res == SOCKET_ERROR)
		{
			return 0;
		}

		size_t fileSize = ntohl(netFileSize);
		return fileSize;
	}

	FileStatus saveFile(char* buffer, size_t fileSize, std::string& filename)
	{
		if (filename.find_last_of('.') == std::string::npos || filename.find_last_of('.') == filename.length() - 1)
		{
			return FileStatus::MISSING_FILE_EXTENSION;
		}

		// Write to file
		std::ofstream file(filename, std::ios::binary);
		if (file.is_open())
		{
			file.write(buffer, fileSize);
			file.close();
		}
		else
		{
			return FileStatus::OPEN_ERROR;
		}
		return FileStatus::SUCCESS;
	}

	

	size_t getFileSize(std::string& filename)
	{
		std::ifstream file(filename, std::ios::binary || std::ios::ate);
		if(file.is_open())
		{
			size_t fileSize = file.tellg();
			file.close();
			return fileSize;
		}
		else
		{
			return 0;
		}
	}

	TransferStatus sendFile(char* buffer, size_t fileSize, size_t bufferSize, SOCKET sock)
	{
		if (buffer == nullptr || bufferSize < fileSize)
		{
			return TransferStatus::BUFFER_TOO_SMALL;
		}

		size_t bytesSent = 0;
		size_t bytesLeft = fileSize;
		while (bytesLeft > 0)
		{
			int res = send(sock, buffer + bytesSent, bytesLeft, 0); // Send the file contents to the server
			if (res == SOCKET_ERROR)
			{
				return TransferStatus::FAILURE;
			}

			bytesSent += res;
			bytesLeft -= res;
		}
		if (bytesSent != fileSize)
		{
			return TransferStatus::INCOMPLETE_SEND;
		}
		return TransferStatus::SUCCESS;
	}

public:

	SOCKET getListenSocket()
	{
		return listeningSocket;
	}

	unsigned getListenPort()
	{
		return LISTENING_SERVER_PORT;
	}

	std::string getIP()
	{
		return IP;
	}

	void initializeServer()
	{
		// Initialize WSA 
		int res = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (res == SOCKET_ERROR)
			throw std::exception("[-] WSA startup failed");

		// Initialize Server sockaddr_in struct
		server.sin_family = AF_INET; // Set IPv4
		server.sin_port = htons(LISTENING_SERVER_PORT); // Set port to listen on
		server.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any address

		// Create and bind Socket
		listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		// Sets sock option to allow connection in TIME_WAIT phase
		int yes = 1;
		if (setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
			throw std::runtime_error(" [-] setsockopt(SO_REUSEADDR) Failed");

		res = bind(listeningSocket, (sockaddr*)&server, sizeof(server));
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Socket Bind Failed");

		// Set Socket to listen
		res = listen(listeningSocket, SOMAXCONN);
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Socket Listen Failed");	
	}

	void setFile(std::string fileName)
	{
		std::lock_guard <std::mutex> lock(file_mutex);
		this->fileName = fileName;
	}

	void uploadFile(SOCKET sock)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";

			User* download_user = findUserBySocket(sock);
			//FileTransfer ft(download_user->getIP(), download_user->getPort());
			FileTransfer ft(download_user->getIP(), 51000);
			TransferStatus upload_status = ft.upload(fileName);
			if (upload_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Tranfer Failed");
			}
			// Can check for other errors here later

			std::cout << "\033[2K\r[+] Upload Complete\n";
		}
		catch (std::exception& e)
		{
			std::string message = std::string(e.what());
			util::print(message);
		}
	}


	void downloadFile()
	{
		size_t res;
		std::string str;

		std::cout << "\033[2K\r[+] Downloading ...\n";
		try
		{
			FileTransfer ft;
			TransferStatus download_status = ft.download();
			if (download_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Transfer Failed");
			}
			// Can also check here for other error codes later

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
					if (str != "y" || str != "Y")
					{
						str = "[-] File Not Saved";
						util::print(str);
						break;
					}
				}
				else
				{
					str = "[+] Saved";
					util::print(str);
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

	void removeUser(User& user)
	{
		std::lock_guard <std::mutex> lock(usersMutex);

		for (auto it = users.begin(); it != users.end(); ++it)
		{
			if (it->get()->getSocket() == user.getSocket())
			{
				closesocket(user.getSocket());
				users.erase(it);
				return;
			}
		}
	}

	bool pendingConnection()
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(listeningSocket, &read_fds);

		// Set timeout to 3s
		struct timeval timeout;
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;

		// Check to see if any sockets are ready for reading
		// If listeningSocket is ready, it means a new connection is pending
		if (select(1, &read_fds, NULL, NULL, &timeout) > 0)
		{
			// Check if listeningSocket is in the set
			if (FD_ISSET(listeningSocket, &read_fds))
			{
				return true; // New connection is pending
			}
		}

		// No pending connections or select error
		return false;
	}

	void addUser(std::unique_ptr<User>& user)
	{
		std::lock_guard <std::mutex> lock(usersMutex);
		users.push_back(std::move(user));
	}

	User* getConnection()
	{
		SOCKET clientSock;
		struct sockaddr_in clientInfo;
		int addrSize = sizeof(clientInfo);

		clientSock = accept(listeningSocket, reinterpret_cast<struct sockaddr*>(&clientInfo),&addrSize);
		if (clientSock == SOCKET_ERROR)
			return nullptr;

		std::unique_ptr<User> newUser = std::make_unique<User>(clientSock);

		char client_IP[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &clientInfo.sin_addr, client_IP, sizeof(client_IP)) == NULL)
		{
			return nullptr;
		}
		
		unsigned int clientPort = ntohs(clientInfo.sin_port);

		//std::string log = "IP connected: " + std::string(client_IP) + " Port connected: " + std::to_string(clientPort);
		//util::debug_log(log);

		newUser->setIP(client_IP);
		//newUser->setPort(clientPort);
		newUser->setPort(51000);
		User* user = newUser.get();

		addUser(newUser);
		return user;
	}

	std::string getUsers_str()
	{
		std::lock_guard <std::mutex> lock(usersMutex);
		std::string usernames = "";

		for (auto& user : users)
		{
			usernames += user->getUsername() + "\n";
		}

		return usernames;
	}

	// Only checks based off of username
	bool userExists(std::string username)
	{
		std::lock_guard< std::mutex> lock(usersMutex);
		for (auto& user_ : users)
		{
			if (user_->getUsername() == username)
				return true;
		}
		return false;
	}

	User* findUserBySocket(SOCKET sock)
	{
		std::lock_guard <std::mutex> lock(usersMutex);
		for (auto& user : users)
		{
			if (sock == user->getSocket())
			{
				return user.get();
			}
		}
		return nullptr;
	}

	User* findUserByUsername(const std::string& username)
	{
		std::lock_guard <std::mutex> lock(usersMutex);
		for (auto& user : users)
		{
			if (username == user->getUsername())
			{
				return user.get();
			}
		}
		return nullptr;
	}

	SOCKET findSocketByUsername(const std::string& username)
	{
		std::lock_guard <std::mutex> lock(usersMutex); 
		for (auto& user : users)
		{
			if (username == user->getUsername())
				return user->getSocket();
		}
		return INVALID_SOCKET;
	}

	void broadcastMessageExceptSender(std::string& message, User& sender)
	{
		std::lock_guard <std::mutex> lock(usersMutex);

		for (int i = 0; i < users.size(); i++)
		{
			if (sender == *users[i] || users[i]->isTransfering)
				continue;

			sendMessage(message, users[i]->getSocket());
		}
	}

	void broadcastMessage(std::string& message)
	{
		std::lock_guard <std::mutex> lock(usersMutex);

		for (int i = 0; i < users.size(); i++)
		{
			if (!users[i]->isTransfering)
			{
				sendMessage(message, users[i]->getSocket());
			}
		}
	}

	void sendMessage(std::string& message, SOCKET sock)
	{
		if (sock == listeningSocket)
		{
			util::print(message);
		}
		else
		{
			send(sock, message.c_str(), message.length(), 0);
		}
	}

	std::string recvMessage(SOCKET sock)
	{
		char buffer[1024];
		int res = recv(sock, buffer, sizeof(buffer), 0);

		if (res == SOCKET_ERROR)
		{
			throw std::exception("[-] Error: Client Unresponsive!");
		}
			
		return std::string(buffer, 0, res);
	}

	std::string recvMessage(User& user)
	{
		char buffer[1024];
		int res = recv(user.getSocket(), buffer, sizeof(buffer), 0);

		if (res == SOCKET_ERROR)
		{
			throw std::exception("[-] Error: Client Unresponsive!");
		}

		return std::string(buffer, 0, res);
	}

	// Send the downloaders information to the uploader
	void sendTransferInfo(SOCKET uploader, SOCKET downloader)
	{
		User* download_user = findUserBySocket(downloader);

		std::string ip = download_user->getIP();
		unsigned port = 51000;
		//unsigned port = download_user->getPort();

		// Send the port
		unsigned net_port = htons(port);
		int res = send(uploader, reinterpret_cast<char*> (&port), sizeof(port), 0);
		if (res == SOCKET_ERROR)
		{
			// handle error
		}

		//std::string log = "Sending IP: " + ip + " Sending Port: " + std::to_string(port);
		//util::debug_log(log);

		// Send the IP
		res = send(uploader,ip.c_str(), ip.length(), 0);
		if (res == SOCKET_ERROR)
		{
			// handle error
		}
	}

};
#endif

