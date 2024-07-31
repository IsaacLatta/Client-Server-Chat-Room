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

	std::vector <unsigned char> server_public_key;
	std::vector <unsigned char> public_key;
	std::vector <unsigned char> secret_key;

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
			std::vector<unsigned char> encrypted_peer_pk(crypto_box_PUBLICKEYBYTES+crypto_box_MACBYTES + crypto_box_NONCEBYTES);
			int res = recv(clientSock, reinterpret_cast<char*>(encrypted_peer_pk.data()), encrypted_peer_pk.size(), 0);
			std::vector<unsigned char> peer_pk = util::decrypt(encrypted_peer_pk, server_public_key, secret_key);

			FileTransfer ft(peer_pk, secret_key);
			TransferStatus download_status = ft.download();
			if (download_status != TransferStatus::SUCCESS)
			{
				throw std::runtime_error("[-] Transfer Failed");
			}
			// Check here for other error codes later

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
			util::print(e.what());
		}
	}

	void uploadFile(std::string fileName)
	{
		try
		{
			std::cout << "\033[2K\r[+] Uploading ...";
		
			// Receive the IP address
			std::vector <unsigned char> encrypted_peer_pk(crypto_box_PUBLICKEYBYTES + crypto_box_MACBYTES + crypto_box_NONCEBYTES);
			int res = recv(clientSock, reinterpret_cast<char*>(encrypted_peer_pk.data()), encrypted_peer_pk.size(), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving public key");
			}
			std::vector<unsigned char> peer_pk = util::decrypt(encrypted_peer_pk, server_public_key, secret_key);

			// Receive the port
			std::vector<unsigned char> encrypted_net_port(sizeof(unsigned) + crypto_box_MACBYTES + crypto_box_NONCEBYTES);
			res = recv(clientSock, reinterpret_cast<char*>(encrypted_net_port.data()), encrypted_net_port.size(), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving port");
			}
			std::vector<unsigned char> net_port_v = util::decrypt(encrypted_net_port, server_public_key, secret_key);
			unsigned port = ntohs(util::vectorToData<unsigned int>(net_port_v));

			std::string peer_IP = recvIP(); // Get the IP

			port = 51000;
			FileTransfer ft(peer_IP, port, peer_pk, secret_key);
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

	std::string recvIP()
	{
		try
		{
			std::vector<unsigned char> e_bin_size(sizeof(int32_t) + crypto_box_MACBYTES + crypto_box_NONCEBYTES);
			int res = recv(clientSock, reinterpret_cast<char*>(e_bin_size.data()), e_bin_size.size(), 0);
			if (res == SOCKET_ERROR)
			{
				throw std::runtime_error("[-] Error receiving IP");
			}

			std::vector <unsigned char> bin_size = util::decrypt(e_bin_size, server_public_key, secret_key);
			int32_t net_size = util::vectorToInt32(bin_size);
			int32_t size = ntohs(net_size);
			std::vector<unsigned char> encrypted_IP(size);

			res = recv(clientSock, reinterpret_cast<char*>(encrypted_IP.data()), encrypted_IP.size(), 0);
			if (res == SOCKET_ERROR)
			{
				return "";
			}
			std::vector<unsigned char> bin_IP = util::decrypt(encrypted_IP, server_public_key, secret_key);
			std::string IP(bin_IP.begin(), bin_IP.begin() + bin_IP.size());

			return IP;
		}
		catch (std::exception& e)
		{
			return "";
		}
	}
	
	std::vector <unsigned char> get_public_key()
	{
		return public_key;
	}

	void set_encryption_keys(std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		public_key = pk;
		secret_key = sk;
	}

	void set_server_pk(std::vector<unsigned char>& server_pk)
	{
		server_public_key = server_pk;
	}

	bool recv_server_pk()
	{
		server_public_key.resize(crypto_box_PUBLICKEYBYTES);
		unsigned char* server_pk_ptr = server_public_key.data();
		size_t bytesLeft = server_public_key.size();

		size_t bytesRecv;
		while (bytesLeft > 0)
		{
			bytesRecv = recv(clientSock, reinterpret_cast<char*> (server_pk_ptr), bytesLeft, 0);
			if (bytesRecv <= 0)
			{
				return false;
			}
			bytesLeft -= bytesRecv;
			server_pk_ptr += bytesRecv;
		}

		return true;
	}

	bool send_pk()
	{
		unsigned char* pk_ptr = public_key.data();
		int res = send(clientSock, reinterpret_cast<char*>(pk_ptr), crypto_box_PUBLICKEYBYTES, 0);
		if (res == SOCKET_ERROR)
		{
			return false;
		}
		return true;
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
		std::vector <unsigned char> message(msg.begin(), msg.end());
		std::vector <unsigned char> msg_to_send = util::encrypt(message, server_public_key, secret_key);
		
		int res = send(clientSock, reinterpret_cast<char*> (msg_to_send.data()), msg_to_send.size(), 0);
		if (res == SOCKET_ERROR)
			throw std::runtime_error("[-] Error: Message not sent!");
	}

	// Receives messages
	std::string recvMessage()
	{
		try
		{
			char buffer[BUFFER_SIZE];
			memset(buffer, 0, BUFFER_SIZE); // Zero buffer before receiving response

			int res = recv(clientSock, buffer, sizeof(buffer), 0);
			if (res == SOCKET_ERROR)
				throw std::runtime_error("[-] Message not received");

			std::vector <unsigned char> encrypted_message(buffer, buffer + res);
			if (encrypted_message.empty())
			{
				throw std::runtime_error("[-] Empty Message received");
			}

			std::vector <unsigned char> decrypted_message = util::decrypt(encrypted_message, server_public_key, secret_key);
			return std::string(decrypted_message.begin(), decrypted_message.end());
		}
		catch (std::exception& e)
		{
			return "";
		}
	}

};
#endif

