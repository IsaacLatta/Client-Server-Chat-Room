#ifndef FILETRANSFER_H
#define FILETRANSFER_H

#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <fstream>
#include <string>
#include <vector>
#include "Util.h"

enum class TransferStatus : uint8_t
{
	SUCCESS = 0x01, FAILURE = 0x02, CHECK_SUM_FAILED = 0x03,
	CONNECTION_CLOSED = 0x04, SIZE_MISMATCH = 0x05, INCOMPLETE_RECV = 0x06,
	INCOMPLETE_SEND = 0x07, MISSING_FILE_EXTENSION = 0x08,
	BUFFER_TOO_SMALL = 0x09,
};

enum class TransferHeader : uint8_t
{
	SUCCESS = 0x01, FAILURE = 0x02, CHUNK = 0x03,
	FILE_SIZE = 0x04, CHECK_SUM = 0x05, CHECK_SUM_NOT_RECV = 0x06,
};

class FileTransfer
{
private:
	SOCKET sock;
	std::vector <char> buffer;
	unsigned port;
	std::string IP_address;
	sockaddr_in peer;

	size_t recvFileSize(SOCKET socket)
	{
		int32_t netFileSize = 0;

		int res = recv(socket, (char*)&netFileSize, sizeof(netFileSize), 0);
		if (res == SOCKET_ERROR)
		{
			return 0;
		}

		size_t fileSize = ntohl(netFileSize);
		return fileSize;
	}

	TransferStatus sendFile(SOCKET socket)
	{
		size_t bytesSent = 0;
		size_t bytesLeft = buffer.size();
		while (bytesLeft > 0)
		{
			// Send the header
			TransferHeader header = TransferHeader::CHUNK;
			int res = send(socket, reinterpret_cast<char*>(&header), sizeof(header), 0);
			if (res == SOCKET_ERROR)
			{
				sendHeader(socket, TransferHeader::FAILURE);
				return TransferStatus::FAILURE;
			}

			// Send the data
			res = send(socket, buffer.data() + bytesSent, bytesLeft, 0); // Send the file contents to the server
			if (res == SOCKET_ERROR)
			{
				TransferHeader error = TransferHeader::FAILURE;
				res = send(socket, reinterpret_cast<char*>(&error), sizeof(error), 0);
				return TransferStatus::FAILURE;
			}

			bytesSent += res;
			bytesLeft -= res;
		}

		if (bytesSent != buffer.size())
		{
			sendHeader(socket, TransferHeader::FAILURE);
			return TransferStatus::INCOMPLETE_SEND;
		}

		if (!confirmCheckSum_send(buffer.data(), buffer.size())) // Check sums match
		{
			return TransferStatus::FAILURE;
		}
		return TransferStatus::SUCCESS;
	}

	bool sendHeader(SOCKET sock, TransferHeader header)
	{
		int res = send(sock, reinterpret_cast<char*> (&header), sizeof(header), 0);
		if (res == SOCKET_ERROR)
		{
			return false;
		}
		return true;
	}

	bool confirmCheckSum_send(char* buffer, size_t buffer_len)
	{
		size_t sum = util::checkSum(buffer, buffer_len);
		size_t net_check_sum = htonl(sum);
		//std::cout << "\nCheck Sum: " << sum << "\n";
		int res = send(sock, reinterpret_cast<char*>(&net_check_sum), sizeof(net_check_sum), 0);
		if (res == SOCKET_ERROR)
		{
			return false;
		}
		//std::cout << "Check sum size sent(bytes): " << res;
		TransferHeader response;
		res = recv(sock, reinterpret_cast<char*>(&response), sizeof(response), 0);
		if (res == SOCKET_ERROR)
		{
			return false;
		}
		if (response == TransferHeader::SUCCESS) // Check sums match
		{
			return true;
		}
		else // Check sums do not match
		{
			return false;
		}
	}

	bool confirmCheckSum_recv(SOCKET socket, char* buffer, size_t buffer_len)
	{
		// Receive the check sum
		size_t net_recv_sum;
		int res = recv(socket, reinterpret_cast<char*>(&net_recv_sum), sizeof(net_recv_sum), 0);
		if (res == SOCKET_ERROR)
		{
			TransferHeader error = TransferHeader::FAILURE;
			res = send(socket, reinterpret_cast<char*>(&error), sizeof(error), 0);
			return false;
		}
		size_t recv_sum = ntohl(net_recv_sum);

		// CHeck sums do not match, send an error, can add more features here such as a re-attempt to download etc.
		size_t sum = util::checkSum(buffer, buffer_len);
		if (sum != recv_sum)
		{
			TransferHeader error = TransferHeader::FAILURE;
			res = send(socket, reinterpret_cast<char*>(&error), sizeof(error), 0);
			return false;
		}

		// Check sums match
		TransferHeader succes = TransferHeader::SUCCESS;
		res = send(socket, reinterpret_cast<char*>(&succes), sizeof(succes), 0);
		return true;
	}

	bool recvHeader(SOCKET socket)
	{
		// Recv the header, check for errors
		TransferHeader header;
		int res = recv(socket, reinterpret_cast<char*>(&header), sizeof(header), 0);
		if (header == TransferHeader::FAILURE || res == SOCKET_ERROR)
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	SOCKET connectToPeer()
	{
		int res = connect(sock, (sockaddr*)&peer, sizeof(peer));
		if (res == SOCKET_ERROR)
		{
			std::string log = "Connection to client failed";
			util::log_error(log);
			return false;
		}
		return true;
	}

	SOCKET acceptPeerConnection()
	{
		SOCKET peerSock;

		int peer_size = sizeof(peer);
		peerSock = accept(sock, (struct sockaddr*)(&peer), &peer_size);
		if (peerSock == INVALID_SOCKET) // connection failed
		{
			return INVALID_SOCKET;
		}
		return peerSock;
	}

	bool peerWaiting()
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(sock, &read_fds);

		struct timeval timeout;
		timeout.tv_sec = 30; // Wait 30 seconds
		timeout.tv_usec = 0;

		int res = select(1, &read_fds, NULL, NULL, &timeout);
		if (res > 0) // pending connection
		{
			if (FD_ISSET(sock, &read_fds)) // sock is in the set
			{
				return true;
			}
		}
		return false;
	}

	bool prepareListenSock()
	{
		std::string error = "";
		// Instantiate sock_addr struct for listening 
		peer.sin_family = AF_INET; // Set IPv4
		port = 51000;
		peer.sin_port = htons(port); // Set port to listen on
		peer.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any address

		// Create socket
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			error = "Socket Creation Failed";
			util::log_error(error);
			return false;
		}

		int yes = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
		{
			std::cerr << "setsockopt(SO_REUSEADDR) failed with error: " << WSAGetLastError() << std::endl;
			closesocket(sock);
			return false;
		}

		// Bind the socket
		int res = bind(sock, reinterpret_cast<struct sockaddr*> (&peer), sizeof(peer));
		if (res == SOCKET_ERROR)
		{
			error = "Bind Failed";
			util::log_error(error);
			return false;
		}

		res = listen(sock, SOMAXCONN);
		if (res == SOCKET_ERROR)
		{
			error = "Listen Failed";
			util::log_error(error);
			return false;
		}
	}

public:

	FileTransfer() {}

	~FileTransfer()
	{
		closesocket(sock);
	}

	// Will be called when uploading(***Act as a client***)
	FileTransfer(const std::string ip, unsigned port)
	{
		this->IP_address = ip;
		this->port = port;

		// Prepare peer addr struct for connection
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		peer.sin_family = AF_INET;
		peer.sin_port = htons(port);
		inet_pton(AF_INET, IP_address.c_str(), &peer.sin_addr);
	}

	TransferStatus download()
	{
		if (!prepareListenSock())
		{
			return TransferStatus::FAILURE;
		}

		// Create socket for receiving file contents
		SOCKET peerSock;
		if (!peerWaiting())
		{
			return TransferStatus::FAILURE;
		}
		peerSock = acceptPeerConnection();

		// Receive the file size
		size_t fileSize = recvFileSize(peerSock);
		if (fileSize == 0)
		{
			closesocket(peerSock);
			return TransferStatus::FAILURE;
		}
		buffer.clear(); // Reset the buffer
		buffer.resize(fileSize);

		// Receive file contents
		size_t bytesRead = 0;
		int res;
		while (bytesRead < fileSize)
		{
			// Check header for an error
			if (!recvHeader(peerSock))
			{
				closesocket(peerSock);
				return TransferStatus::FAILURE;
			}

			// Receive file contents
			res = recv(peerSock, buffer.data() + bytesRead, fileSize - bytesRead, 0);
			if (res == SOCKET_ERROR)
			{
				// send error 
				TransferHeader error = TransferHeader::FAILURE;
				sendHeader(peerSock, error);
				closesocket(peerSock);
				return TransferStatus::FAILURE;
			}
			if (res == 0) // Server closed connection
			{
				if (bytesRead == fileSize)
				{
					break;
				}
				TransferHeader error = TransferHeader::FAILURE;
				sendHeader(peerSock, error);
				closesocket(peerSock);
				return TransferStatus::CONNECTION_CLOSED;
			}

			bytesRead += res;
		}

		if (bytesRead != fileSize) // Not all bytes received
		{
			TransferHeader error = TransferHeader::FAILURE;
			sendHeader(peerSock, error);
			closesocket(peerSock);
			return TransferStatus::INCOMPLETE_RECV;
		}

		// Ensure correct data
		if (confirmCheckSum_recv(peerSock, buffer.data(), buffer.size()))
		{
			closesocket(peerSock);
			return TransferStatus::SUCCESS;
		}
		else
		{
			closesocket(peerSock);
			return TransferStatus::CHECK_SUM_FAILED;
		}
	}

	TransferStatus upload(std::string& fileName)
	{
		if (fileName.find_last_of('.') == std::string::npos || fileName.find_last_of('.') == fileName.length() - 1)
		{
			return TransferStatus::MISSING_FILE_EXTENSION;
		}

		if (!connectToPeer()) // Connect to the peer
		{
			return TransferStatus::FAILURE;
		}

		std::ifstream file(fileName, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			sendHeader(sock, TransferHeader::FAILURE);
			return TransferStatus::FAILURE;
		}
		int32_t fileSize = file.tellg(); // Get the file size
		buffer.clear(); // Reset the buffer
		buffer.resize(fileSize); // Set buffer to correct size

		// Send the file size
		int32_t netFileSize = htonl(fileSize); // Convert file to network byte order
		int res = send(sock, reinterpret_cast<char*>(&netFileSize), sizeof(netFileSize), 0);
		if (res == SOCKET_ERROR)
		{
			sendHeader(sock, TransferHeader::FAILURE);
			return TransferStatus::FAILURE;
		}

		// Read the contents of the file into buffer
		file.seekg(0, std::ios::beg);
		if (!file.read(buffer.data(), fileSize))
		{
			file.close();
			sendHeader(sock, TransferHeader::FAILURE);
			return TransferStatus::FAILURE;
		}
		file.close();

		return sendFile(sock); // Send the file contents
	}

	TransferStatus saveFile(std::string& filename)
	{
		if (filename.find_last_of('.') == std::string::npos || filename.find_last_of('.') == filename.length() - 1)
		{
			return TransferStatus::MISSING_FILE_EXTENSION;
		}

		std::ofstream file(filename, std::ios::binary);
		if (file.is_open())
		{
			file.write(buffer.data(), buffer.size());
			file.close();
		}
		else
		{
			return TransferStatus::FAILURE;
		}
		return TransferStatus::SUCCESS;
	}
};
#endif



