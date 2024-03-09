#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <fstream>
#include <ostream>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <stdexcept>
#include <iomanip>
#include "ThreadPool.h"
#include "Util.h"
#include "User.h"

#pragma comment (lib,  "Ws2_32.lib")


class Server
{

private:
	sockaddr_in server;
	WSADATA wsaData;
	SOCKET listeningSocket;
	const int LISTENING_SERVER_PORT = 50000;
	int BUFFER_SIZE = 1024;

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

	void forwardFile(SOCKET sender, SOCKET receiver)
	{
		try
		{
			std::string message = "[*] Transfering File ...";
			util::print(message);

			// Get the size of the file
			int32_t netFileSize = 0;
			int res = recv(sender, (char*)&netFileSize, sizeof(netFileSize), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving file size");
			}

			size_t fileSize = ntohl(netFileSize); // convert file size to host byte order

			// Receive file contents
			size_t bytesRead = 0;
			char* buffer = new char[fileSize];
			while (bytesRead < fileSize)
			{
				res = recv(sender, buffer + bytesRead, fileSize - bytesRead, 0);
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

			// File contents now in buffer, forward to the receiver
			// Send the file size
			res = send(receiver, reinterpret_cast<char*>(&netFileSize), sizeof(netFileSize), 0);
			if (res == SOCKET_ERROR)
			{
				delete[] buffer;
				throw std::runtime_error("[-] Failed to send file size");
			}

			// Send the file contents
			size_t bytesSent = 0;
			size_t bytesLeft = fileSize;
			while (bytesLeft > 0)
			{
				res = send(receiver, buffer + bytesSent, bytesLeft, 0); 
				if (res == SOCKET_ERROR)
				{
					delete[] buffer;
					throw std::runtime_error("[-] Failed to send file contents");
				}

				bytesSent += res;
				bytesLeft -= res;
			}

			delete[] buffer;
			message = "[+] Transfer Complete";
			util::print(message);
		}
		catch (std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);
			// Send an error code here
		}
	}


	void downloadFile(SOCKET sock)
	{
		size_t res;
		std::string str;

		std::cout << "\033[2K\r[+] Downloading ...\n";
		try
		{
			// Get the size of the file
			int32_t netFileSize = 0;

			res = recv(sock, (char*)&netFileSize, sizeof(netFileSize), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving file size");
			}

			size_t fileSize = ntohl(netFileSize);

			// Receive file contents
			size_t bytesRead = 0;
			char* buffer = new char[fileSize];

			while (bytesRead < fileSize)
			{
				res = recv(sock, buffer + bytesRead, fileSize - bytesRead, 0);
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

			// Write to file
			std::ofstream file(filename, std::ios::binary);
			if (file.is_open())
			{
				file.write(buffer, bytesRead);
				file.close();
			}
			else
			{
				delete[] buffer;
				throw std::runtime_error("[-] File Failed to open");
			}
			str = "[+] Saved";
			util::print(str);
			delete[] buffer;
		}
		catch (std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);

		}
	}

	void uploadFile(SOCKET sock)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";

			std::ifstream file(fileName, std::ios::binary | std::ios::ate);
			if (file.is_open())
			{
				int32_t fileSize = file.tellg(); // Get the file size

				// Send the file size
				int32_t netFileSize = htonl(fileSize); // Convert file to network byte order

				int res = send(sock, reinterpret_cast<char*>(&netFileSize), sizeof(netFileSize), 0);
				if (res == SOCKET_ERROR)
					throw std::runtime_error("[-] Failed to send file size");

				char* buffer = new char[fileSize];
				file.seekg(0, std::ios::beg);
				file.read(buffer, fileSize);  // Read the contents of the file into buffer 

				size_t bytesSent = 0;
				size_t bytesLeft = fileSize;
				while (bytesLeft > 0)
				{
					res = send(sock, buffer + bytesSent, bytesLeft, 0); // Send the file contents to the server
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
		catch (std::exception& e)
		{
			std::string message = "[-] File Transfer Failed\n" + std::string(e.what());
			util::print(message);
		}
		std::string message = "[+] Upload Complete";
		util::print(message);
	}

public:

	SOCKET getListenSocket()
	{
		return listeningSocket;
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

	void transferFile(SOCKET sender, SOCKET receiver)
	{
		if (sender == listeningSocket)
		{
			uploadFile(receiver);
		}
		else if (receiver == listeningSocket)
		{
			downloadFile(sender);
		}
		else
		{
			forwardFile(sender, receiver);
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

	SOCKET getConnection()
	{
		SOCKET clientSock;
		clientSock = accept(listeningSocket, NULL, NULL);
		if (clientSock == SOCKET_ERROR)
			return INVALID_SOCKET;

		return clientSock;
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

};
#endif

