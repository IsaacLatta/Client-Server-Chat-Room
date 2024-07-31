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
	std::string IP;

	std::vector <std::unique_ptr<User>> users;
	std::vector <unsigned char> public_key;
	std::vector <unsigned char> secret_key;
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

	void setIP(const std::string& IP)
	{
		this->IP = IP;
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

	void uploadFile(User* download_user)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";

			std::vector <unsigned char> download_pk = download_user->get_pk();
			FileTransfer ft(download_user->getIP(), 51000, download_pk, secret_key);
			TransferStatus upload_status = ft.upload(fileName);
			if (upload_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Tranfer Failed");
			}
			// Can check for other errors here later

			std::cout << "\033[2K\r[+] Upload Complete\n> ";
		}
		catch (std::exception& e)
		{
			std::string message = std::string(e.what());
			util::print(message);
		}
	}

	void downloadFile(User* upload_user)
	{
		size_t res;
		std::string str;

		std::cout << "\033[2K\r[+] Downloading ...";
		try
		{
			std::vector <unsigned char> peer_pk = upload_user->get_pk();
			FileTransfer ft(peer_pk, secret_key);
			TransferStatus download_status = ft.download();
			if (download_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Transfer Failed");
			}
			// Can also check here for other error codes later

			str = "[+] Download Complete";
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
					std::cout << "[+] Saved\n";
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

	std::vector<unsigned char> receive_pk(SOCKET sock)
	{
		std::vector<unsigned char> pk(crypto_box_PUBLICKEYBYTES);
		unsigned char* pk_ptr = pk.data();
		size_t bytesLeft = pk.size();

		size_t bytesRecv;
		while (bytesLeft > 0)
		{
			bytesRecv = recv(sock, reinterpret_cast<char*> (pk_ptr), bytesLeft, 0); // Do not recv more than remaining bytes
			if (bytesRecv == SOCKET_ERROR)
			{
				std::vector<unsigned char> empty_vec;
				return empty_vec;
			}
			if (bytesRecv == 0)
			{
				std::vector<unsigned char> empty_vec;
				return empty_vec;
			}
			pk_ptr += bytesRecv; // Shift pointer
			bytesLeft -= bytesRecv;
		}
		return pk;
	}

	// Will later add other encryption method for first contact (Diffie Hellman / other method)
	void send_pk(SOCKET sock)
	{
		unsigned char* pk_ptr = public_key.data();
		size_t bytesLeft = public_key.size();

		size_t bytesSent = 0, total_bytesSent = 0;
		while (bytesLeft > 0)
		{
			bytesSent = send(sock, reinterpret_cast<char*>(pk_ptr + total_bytesSent), bytesLeft, 0);
			total_bytesSent += bytesSent;
			bytesLeft -= bytesSent;
		}
	}

	User* getConnection()
	{
		SOCKET clientSock;
		struct sockaddr_in clientInfo;
		int addrSize = sizeof(clientInfo);

		clientSock = accept(listeningSocket, reinterpret_cast<struct sockaddr*>(&clientInfo),&addrSize);
		if (clientSock == SOCKET_ERROR)
			return nullptr;

		send_pk(clientSock);
		std::unique_ptr<User> newUser = std::make_unique<User>(clientSock);
		std::vector<unsigned char> client_pk = receive_pk(clientSock);
		if (client_pk.empty())
		{
			return nullptr;
		}
		newUser->set_public_key(client_pk);

		char client_IP[INET_ADDRSTRLEN];
		if (inet_ntop(AF_INET, &clientInfo.sin_addr, client_IP, sizeof(client_IP)) == NULL)
		{
			return nullptr;
		}
		
		unsigned int clientPort = ntohs(clientInfo.sin_port);
		newUser->setIP(client_IP);
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

	void set_encryption_keys(std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		public_key = pk;
		secret_key = sk;
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
		try
		{
			std::lock_guard <std::mutex> lock(usersMutex);

			for (int i = 0; i < users.size(); i++)
			{
				if (sender == *users[i] || users[i]->isTransfering)
					continue;

				sendMessage(message, users[i].get());
			}
		}
		catch (std::exception& e)
		{
			std::cerr << "\n" << "Exception: " << e.what() << "\n";
		}
	}

	void broadcastMessage(std::string& message)
	{
		std::lock_guard <std::mutex> lock(usersMutex);

		for (int i = 0; i < users.size(); i++)
		{
			if (!users[i]->isTransfering)
			{
				sendMessage(message, users[i].get());
			}
		}
	}

	// Only encrypt msg if it is leaving the server
	void sendMessage(std::string& msg, User* user)
	{
		if (user->getSocket() == listeningSocket)
		{
			util::print(msg);
		}
		else
		{
			std::vector<unsigned char> message(msg.begin(), msg.end());
			std::vector<unsigned char> public_key = user->get_pk();
			std::vector <unsigned char> encrypted_msg = util::encrypt(message, public_key, secret_key);
			send(user->getSocket(), reinterpret_cast<char*>(encrypted_msg.data()), encrypted_msg.size(), 0);
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
		
		std::vector<unsigned char> encrypted_message(buffer, buffer + res);
		if (encrypted_message.empty())
		{
			throw std::exception("[!] Received Empty Message");
		}

		std::vector<unsigned char> pk = user.get_pk();
		std::vector<unsigned char> decrypted_message = util::decrypt(encrypted_message, pk, secret_key);

		return std::string(decrypted_message.begin(), decrypted_message.end());
	}

	// Send the downloaders information to the uploader
	void sendTransferInfo(User* upload_user, User* download_user)
	{
			unsigned port = 51000;
			std::vector<unsigned char> download_pk = download_user->get_pk();
			std::vector<unsigned char> upload_pk = upload_user->get_pk();

			std::vector<unsigned char> encrypted_download_pk = util::encrypt(download_pk, upload_pk, secret_key);
			std::vector<unsigned char> encrypted_upload_pk = util::encrypt(upload_pk, download_pk, secret_key);

			// send the downloaders pub key to the uploader
			int res = send(upload_user->getSocket(), reinterpret_cast<char*>(encrypted_download_pk.data()), encrypted_download_pk.size(), 0);
			
			// send the uploader pub key to the downloader
			res = send(download_user->getSocket(), reinterpret_cast<char*>(encrypted_upload_pk.data()), encrypted_upload_pk.size(), 0);
			
			// Encrypt and send the port
			unsigned net_port = htons(port);
			std::vector <unsigned char> net_port_v = util::dataToVector(net_port);
			std::vector<unsigned char> encrypted_port = util::encrypt(net_port_v, upload_pk, secret_key);
			res = send(upload_user->getSocket(), reinterpret_cast<char*> (encrypted_port.data()), encrypted_port.size(), 0);
			
			// Send the IP to the uploader
			std::string IP = download_user->getIP();
			std::vector<unsigned char> IP_v(IP.begin(), IP.end());
			std::vector<unsigned char> encrypted_IP = util::encrypt(IP_v, upload_pk, secret_key);

			// Send the size first
			int32_t dataSize = htons(encrypted_IP.size());
			std::vector <unsigned char> bin_size = util::int32ToVector(dataSize);
			std::vector<unsigned char> encrypted_size = util::encrypt(bin_size, upload_pk, secret_key);
			res = send(upload_user->getSocket(), reinterpret_cast<char*>(encrypted_size.data()), encrypted_size.size(), 0);
			
			res = send(upload_user->getSocket(), reinterpret_cast<char*>(encrypted_IP.data()), encrypted_IP.size(), 0); // Send the IP
	}

};
#endif

