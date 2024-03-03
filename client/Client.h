#ifndef CLIENT_H
#define CLIENT_H

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <string>

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

	// Close the clients connections
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
		std::cout << "[+] Downloading ...";
		Sleep(2000);
	}

	void uploadFile(std::string fileName)
	{
		std::cout << "[+] Uploading ...";
		Sleep(2000);
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

