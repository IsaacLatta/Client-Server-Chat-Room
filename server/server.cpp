#include <iostream>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <stdexcept>
#include "ThreadPool.h"
#include "Util.h"

#pragma comment (lib,  "Ws2_32.lib")

sockaddr_in server;
WSADATA wsaData;
SOCKET listeningSocket;
const int LISTENING_SERVER_PORT = 50000;
const int BUFFER_SIZE = 1024;

User host;
std::vector <User> users;
std::mutex usersMutex;

std::condition_variable shutdownCondition;
std::atomic <bool> shouldQuit = false;
std::mutex shutdownMutex;

ThreadPool threadPool;
std::mutex sendMutex;

void addUser(User user)
{
	std::lock_guard <std::mutex> lock(usersMutex);
	::users.push_back(user);
}

void initializeServer()
{
	// Initialize WSA 
	int res = WSAStartup(MAKEWORD(2, 2), &::wsaData);
	if (res == SOCKET_ERROR)
		throw std::exception("[-] WSA startup failed");

	// Initialize Server sockaddr_in struct
	::server.sin_family = AF_INET; // Set IPv4
	::server.sin_port = htons(LISTENING_SERVER_PORT); // Set port to listen on
	::server.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any address

	// Create and bind Socket
	::listeningSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Sets sock option to allow connection in TIME_WAIT phase
	int yes = 1;
	if (setsockopt(listeningSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
		throw std::runtime_error(" [-] setsockopt(SO_REUSEADDR) Failed");

	res = bind(::listeningSocket, (sockaddr*)&server, sizeof(server));
	if (res == SOCKET_ERROR)
		throw std::runtime_error("[-] Socket Bind Failed");

	// Set Socket to listen
	res = listen(::listeningSocket, SOMAXCONN);
	if (res == SOCKET_ERROR)
		throw std::runtime_error("[-] Socket Listen Failed");

	host.setSocket(::listeningSocket);
	host.setUsername("</HOST> ");
	addUser(host);
}

void signalShutdown()
{
	::shouldQuit = true;
	shutdownCondition.notify_one();
}

void shutdown()
{
	shouldQuit = true;

	for (auto& user : ::users)
	{
		closesocket(user.getSocket());
	}

	WSACleanup();
	closesocket(::listeningSocket);
	util::print("[+] Server Closed");
	std::cout << "\033[2K\r";
}

void removeUser(User& user)
{
	std::lock_guard <std::mutex> lock(usersMutex);

	closesocket(user.getSocket());
	User* userPointer = util::search(users, user);
	if (userPointer)
	{
		std::swap(*userPointer, users.back());
		users.pop_back();
	}
}

void listUsersCMD(User& user)
{
	std::lock_guard <std::mutex> lock(usersMutex);
	std::string usernames = "Usernames\n---------\n";
	
	for (auto& user : users)
	{
		usernames += user.getUsername() + "\n";
	}

	if (user.getUsername() == "</HOST> ")
	{
		util::print(usernames);
		return;
	}

	send(user.getSocket(), usernames.c_str(), usernames.length(), 0);
}

SOCKET findSocketByUsername(const std::string& username)
{
	std::lock_guard <std::mutex> lock(usersMutex);
	for (auto& user : users)
	{
		if (username == user.getUsername())
			return user.getSocket();
	}
	return INVALID_SOCKET;
}

void listCMDS(User& user)
{
	std::string cmds = "Commands\n--------\n";
	cmds += "/whisper: Direct message a user by username(</recipient>)\n/users: List all users\n";
	if (user.getUsername() == "</HOST> ")
	{
		cmds += "/end: Close the server\n";
		util::print(cmds);
	}
	else
	{
		cmds += "/quit: Leave the chatroom\n";
		send(user.getSocket(), cmds.c_str(), cmds.length(), 0);
	}
}

// For /whisper cmd
//</sender> /whisper </recipient> message ----> </sender> whispered: message
void whisperCMD(std::string& message, User& user)
{
	int res, pos, start, end;
	std::string recipient, sender, command;
	SOCKET tempSock;

	pos = message.find(' ');
	sender = message.substr(0, pos + 1); // </sender>

	start = message.find(' ', pos + 1);
	command = message.substr(pos + 1, start - pos); // /whisper

	end = message.find(' ', start + 1);
	recipient = message.substr(start + 1, end - start);// </recipient>

	end = message.find(' ', end);
	message.erase(0, end + 1); // message
	message = sender + "whispered: " + message; // final message

	tempSock = findSocketByUsername(recipient);
	if (tempSock == INVALID_SOCKET) // User not found
	{
		message = "[!] User Not Found";
		if(sender == "</HOST> ")
			util::print(message);
		else
			send(user.getSocket(), message.c_str(), message.length(), 0);

		return;
	}

	if (recipient == "</HOST> ")
	{
		util::print(message);
		return;
	}
	else
	{
		send(tempSock, message.c_str(), message.length(), 0);
	}
}

// Relays message to all users
void broadcastMessageExceptSender(const std::string& message,User& sender)
{
	std::lock_guard <std::mutex> lock(usersMutex);
	for (int i = 0; i < users.size(); i++)
	{
		if (sender == users[i])
			continue;
		send(users[i].getSocket(), message.c_str(), message.length(), 0);
	}
}

std::string recvMessage(User& user)
{
		char buffer[BUFFER_SIZE];
		int res = recv(user.getSocket(), buffer, sizeof(buffer), 0);

		if (res == SOCKET_ERROR)
			throw std::exception("[-] Error: Client Unresponsive!");
		return std::string(buffer, 0, res);
}

// Handles messages send from the server (</HOST>)
void handleOutgoingMessages(std::string& message, User& user) 
{
	// Check for commands
	if (message.find("/end") != std::string::npos)
	{
		broadcastMessageExceptSender(message, user);
		signalShutdown();
		return;
	}
	if (message.find("/commands") != std::string::npos)
	{
		listCMDS(user);
		return;
	}
	if (message.find("/users") != std::string::npos)
	{
		listUsersCMD(user);
		return;
	}
	if (message.find("/whisper") != std::string::npos)
	{
		whisperCMD(message, user);
		return;
	}
	broadcastMessageExceptSender(message, user);
}

void handleIncomingMessages(std::string& message, User& user)
{
	// Check for commands
	if (message.find("/quit") != std::string::npos)
	{
		removeUser(user);
		return;
	}
	if (message.find("/users") != std::string::npos)
	{
		listUsersCMD(user);
		return;
	}
	if (message.find("/whisper") != std::string::npos)
	{
		whisperCMD(message, user);
		return;
	}
	if (message.find("/commands") != std::string::npos)
	{
		listCMDS(user);
		return;
	}
	broadcastMessageExceptSender(message, user);
	util::print(message);
}

// Handles client relay
void handleClient(User& user)
{
	try
	{
		std::string message;
		while (!shouldQuit)
		{
			message = recvMessage(user);
			handleIncomingMessages(message, user);
		}
	}
	catch (std::exception& e)
	{
		if (::shouldQuit)
			return;
		if (!util::search(users, user))
		{
			util::print("[!] Client Closed Connection");
			return;
		}
		std::cout << e.what() << std::endl;
	}
}

// Loop for sending messages from the server(</HOST>)
void sendMessageLoop()
{
	std::string message;
	int res;

	while (!::shouldQuit)
	{
		std::getline(std::cin, message);
		std::cout << "> ";
		message = host.getUsername() + message;
		handleOutgoingMessages(message, ::host);
	}
}

void getConnections()
{
	SOCKET clientSock;
	std::string user_name;

	util::print("[+] Listening for connections ...");
	while (!::shouldQuit)
	{
		clientSock = accept(::listeningSocket, NULL, NULL);
		if (::shouldQuit)
			return;

		if (clientSock == SOCKET_ERROR)
		{
			std::cerr << "[-] Client Socket Creation Failed" << std::endl;
			continue;
		}

		util::print("[+] Client Connected");

		// Initialize the User struct for the client
		User user(clientSock, "</Anon> "); 
		user_name = recvMessage(user);
		user.setUsername(user_name);
		addUser(user);
		::threadPool.pushTask(handleClient, user); 
	}
}

int main()
{
	try {
		std::unique_lock <std::mutex> lock(shutdownMutex);
		initializeServer();

		::threadPool.pushTask(getConnections);
		::threadPool.pushTask(sendMessageLoop);

		shutdownCondition.wait(lock, [] {return shouldQuit.load();  });
		shutdown();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		std::cerr << "[-] Error: " << WSAGetLastError() << std::endl;
	}
	return 0;
}