#include <iostream>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include "ThreadPool.h"
#include "Util.h"

#pragma comment (lib,  "Ws2_32.lib")

int LISTENING_SERVER_PORT = 50000;
const int BUFFER_SIZE = 1024;

WSADATA wsaData;
sockaddr_in server;
SOCKET clientSock;

std::mutex shutdownMutex;
std::atomic <bool> shouldQuit = false;
std::condition_variable shutdownCondition;

std::string username;
ThreadPool threadPool;

void signalShutdown()
{
	::shouldQuit = true;
	shutdownCondition.notify_one();
}

void sendMessage(std::string msg)
{
	msg = ::username + msg;
	int res = send(::clientSock, msg.c_str(), msg.length(), 0);
	if (res == SOCKET_ERROR)
		throw std::runtime_error("[-] Error: Message not sent!");
}

void initializeClient()
{
	// Initialize WSA 
	int res = WSAStartup(MAKEWORD(2, 2), &::wsaData);
	if (res == SOCKET_ERROR)
		throw std::exception("[-] WSA startup failed");

	// Initialize socket
	clientSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientSock == INVALID_SOCKET)
		throw std::runtime_error("[-] Socket Creation Failed!");
}

void connectToServer(std::string& ip_address)
{
	// Initialize Server sockaddr_in struct
	::server.sin_family = AF_INET; // Set IPv4
	::server.sin_port = htons(LISTENING_SERVER_PORT); // Set server port to connect to
	inet_pton(AF_INET, ip_address.c_str(), &server.sin_addr); // Convert SERVER_IP into network byte order for transmission

	// Connect to server
	int res = connect(clientSock, (sockaddr*)&server, sizeof(server));
	if (res == SOCKET_ERROR)
		throw std::runtime_error("[-] Connection Failed");
}

// Close the clients connections
void shutdown()
{
	closesocket(clientSock);
	WSACleanup();
	util::print("[+] Connection Closed.");
	std::cout << "\033[2K\r"; // Remove "> " from terminal
}

// Receives messages
std::string recvMessage()
{
	char buffer[BUFFER_SIZE];
	memset(buffer, 0, BUFFER_SIZE); // Zero buffer before receiving response
	
	int res = recv(::clientSock, buffer, sizeof(buffer), 0);
	if (res == SOCKET_ERROR)
		throw std::runtime_error("[-] Message not received! ");

	return std::string(buffer, 0, res);
}

// Handles outgoing messages
void handleOutgoingMessage(const std::string& message)
{
	try 
	{
		if (message.find("/quit")!= std::string::npos)
		{
			sendMessage(message);
			signalShutdown();
			return;
		}
		sendMessage(message);
	}
	catch (std::exception& e)
	{
		if (::shouldQuit)
			return;
		else
			util::print(e.what());
	}
}

// Handles incoming messages
void handleIncomingMessage(const std::string& message)
{
		if (::shouldQuit) // Connection closed for other reason
			return;
		if (message.find("/end")!= std::string::npos) // Host closed server
		{
			util::print("[!] Server has been Closed!");
			signalShutdown();
			return;
		}
		else
			util::print(message);
}

// Receive chats
void recvMessageLoop()
{
	try
	{
		std::string message;
		while (!::shouldQuit)
		{
			message = recvMessage();
			handleIncomingMessage(message);
		}
	}
	catch (std::exception& e)
	{
		if (::shouldQuit)
			return;
		std::cerr << e.what() << std::endl;
	}
}

// Sends chats to server
void sendMessageLoop()
{
	std::string message;;
	while (!::shouldQuit)
	{
			std::getline(std::cin, message); // When connection closed from server side getline will require a key press to close.
			std::cout << "> ";
			handleOutgoingMessage(message);			
	}
	std::cout << "\033[2K\r"; // Remove "> " from terminal
}

void login()
{
	std::string server_ip;
	
	while(true)
	{
		std::cout << "[*] Enter Username: ";
		std::cin >> ::username;
		if (::username == "HOST")
		{
			std::cout << "[!] Invalid Username" << std::endl;
			continue;
		}
		::username = "</" + username + "> ";
		break;
	} 

	std::cout << "[*] Enter Servers IP: ";
	std::cin >> server_ip;

	std::cout << "[+] Connecting to server ...\n";
	initializeClient();
	connectToServer(server_ip);
	std::cout << "[+] Connected to Server!\n";
}

int main()
{
	try 
	{
		std::unique_lock <std::mutex> lock(shutdownMutex);
		login();

		threadPool.pushTask(sendMessageLoop);
		threadPool.pushTask(recvMessageLoop);

		shutdownCondition.wait(lock, [] { return shouldQuit.load(); });
		shutdown();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		std::cerr << "[-] Error: " << WSAGetLastError() << std::endl;
	}
	return 0;
}