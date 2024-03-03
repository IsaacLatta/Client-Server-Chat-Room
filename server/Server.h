#ifndef SERVER_H
#define SERVER_H

#include <iostream>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <stdexcept>
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
	//std::atomic <bool> shouldQuit = false;
	std::mutex shutdownMutex;

	ThreadPool threadPool;
	std::mutex sendMutex;

	void shutdown()
	{
		//shouldQuit = true;

		for (auto& user : users)
		{
			closesocket(user->getSocket());
		}

		WSACleanup();
		closesocket(listeningSocket);
		util::print("[+] Server Closed");
		std::cout << "\033[2K\r";
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

	void downloadFile(SOCKET sock)
	{
		std::cout << "[+] Downloading ...";
		Sleep(2000);
	}

	void uploadFile(SOCKET sock)
	{
		std::cout << "[+] Uploading ...";
		Sleep(2000);
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
			std::cout << "Transfering ...";
			Sleep(2000);
		}
		return;
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
			if (sender == *users[i])
				continue;

			sendMessage(message, users[i]->getSocket());
		}
	}

	void broadcastMessage(std::string& message)
	{
		std::lock_guard <std::mutex> lock(usersMutex);

		for (int i = 0; i < users.size(); i++)
		{
			sendMessage(message, users[i]->getSocket());
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

