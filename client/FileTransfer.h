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
	SUCCESS = 0x01, FAILURE = 0x02, HASH_FAILED = 0x03,
	CONNECTION_CLOSED = 0x04, SIZE_MISMATCH = 0x05, INCOMPLETE_RECV = 0x06,
	INCOMPLETE_SEND = 0x07, MISSING_FILE_EXTENSION = 0x08,
	BUFFER_TOO_SMALL = 0x09,
};

enum class TransferHeader : uint8_t
{
	SUCCESS = 0x01, FAILURE = 0x02, CHUNK = 0x03,
	FILE_SIZE = 0x04, HASH = 0x05, HASH_NOT_RECV = 0x06,
};

class FileTransfer
{
private:
	SOCKET sock;
	std::vector <unsigned char> buffer;
	std::vector <unsigned char> encrypted_buffer;
	unsigned port;
	std::string IP_address;
	sockaddr_in peer;
	std::vector <unsigned char> peer_public_key;
	std::vector <unsigned char> public_key;
	std::vector <unsigned char> secret_key;

	// For initial sharing of the new key pair that will be generated for the transfer
	std::vector <unsigned char> initial_pk;
	std::vector <unsigned char> initial_sk;

	size_t recvFileSize(SOCKET socket)
	{
		std::vector<unsigned char> encrypted_data(crypto_box_NONCEBYTES + crypto_box_MACBYTES + sizeof(int32_t));

		// receive the encrypted file size
		int res = recv(socket, reinterpret_cast<char*>(encrypted_data.data()), encrypted_data.size(), 0);
		if (res == SOCKET_ERROR)
		{
			return 0;
		}
		std::vector <unsigned char> decrypted_data = util::decrypt(encrypted_data, peer_public_key, secret_key);
		int32_t net_fileSize = util::vectorToInt32(decrypted_data);
		size_t fileSize = ntohl(net_fileSize);
		return fileSize;
	}

	bool sendHeader(SOCKET sock, TransferHeader header)
	{
		uint8_t x = static_cast<uint8_t>(header);
		std::vector<unsigned char> header_v(1, x);
		std::vector<unsigned char> encrypted_header = util::encrypt(header_v, peer_public_key, secret_key);

		int res = send(sock, reinterpret_cast<char*> (encrypted_header.data()), encrypted_header.size(), 0);
		if (res == SOCKET_ERROR)
		{
			return false;
		}
		return true;
	}

	bool recvHeader(SOCKET socket)
	{
		// Recv the header, check for errors
		std::vector<unsigned char> encrypted_header(crypto_box_NONCEBYTES + crypto_box_MACBYTES + sizeof(uint8_t));

		int res = recv(socket, reinterpret_cast<char*>(encrypted_header.data()), encrypted_header.size(), 0);
		std::vector <unsigned char> decrypted_header = util::decrypt(encrypted_header, peer_public_key, secret_key);
		TransferHeader header = static_cast<TransferHeader> (decrypted_header[0]);

		if (res == SOCKET_ERROR)
		{
			return false;
		}
		if (header == TransferHeader::FAILURE)
		{
			return false;
		}
		if (header == TransferHeader::SUCCESS)
		{
			return true;
		}
		else
		{
			return true;
		}
	}

	TransferStatus sendFile(SOCKET socket)
	{
		// Compute a hash
		std::vector<unsigned char> file_hash(crypto_hash_sha256_BYTES);
		crypto_hash_sha256(file_hash.data(), reinterpret_cast<unsigned char*>(buffer.data()), buffer.size());

		// Encrypt the data
		encrypted_buffer.clear();
		encrypted_buffer = util::encrypt(buffer, peer_public_key, secret_key);

		size_t bytesLeft = encrypted_buffer.size(), bytesSent = 0;
		while (bytesLeft > 0)
		{
			// Send the header
			if (!sendHeader(socket, TransferHeader::CHUNK))
			{
				sendHeader(socket, TransferHeader::FAILURE);
				return TransferStatus::FAILURE;
			}

			// Send the data
			int res = send(socket, reinterpret_cast<char*>(encrypted_buffer.data() + bytesSent), bytesLeft, 0); // Send the file contents to the server
			if (res == SOCKET_ERROR)
			{
				sendHeader(socket, TransferHeader::FAILURE);
				return TransferStatus::FAILURE;
			}

			bytesSent += res;
			bytesLeft -= res;
		}

		if (bytesSent != encrypted_buffer.size())
		{
			sendHeader(socket, TransferHeader::FAILURE);
			return TransferStatus::INCOMPLETE_SEND;
		}

		if (!confirmFileHash_send(file_hash, socket)) // Check sums match
		{
			return TransferStatus::FAILURE;
		}
		return TransferStatus::SUCCESS;
	}

	bool confirmFileHash_send(std::vector<unsigned char>& hash, SOCKET socket)
	{
		size_t bytesLeft = hash.size(), bytesSent = 0;
		while (bytesLeft > 0)
		{
			int res = send(socket, reinterpret_cast<char*>(hash.data() + bytesSent), bytesLeft, 0);
			if (res == SOCKET_ERROR)
			{
				return false;
			}
			bytesLeft -= res;
			bytesSent += res;
		}

		if (!recvHeader(socket))
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	bool confirmFileHash_recv(std::vector<unsigned char>& hash, SOCKET socket)
	{
		std::vector<unsigned char> recv_hash(crypto_hash_sha256_BYTES);
		size_t bytesLeft = recv_hash.size(), bytesRecv = 0;
		while (bytesLeft > 0)
		{
			int res = recv(socket, reinterpret_cast<char*>(recv_hash.data() + bytesRecv), bytesLeft, 0);
			if (res == SOCKET_ERROR)
			{
				sendHeader(socket, TransferHeader::FAILURE);
				return false;
			}
			bytesLeft -= res;
			bytesRecv += res;
		}

		if (hash != recv_hash)
		{
			sendHeader(socket, TransferHeader::FAILURE);
			return false;
		}

		sendHeader(socket, TransferHeader::SUCCESS);
		return true;
	}

	bool connectToPeer()
	{
		int res = connect(sock, (sockaddr*)&peer, sizeof(peer));
		if (res == SOCKET_ERROR)
		{
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
		// Instantiate sock_addr struct for listening 
		peer.sin_family = AF_INET; // Set IPv4
		port = 51000;
		peer.sin_port = htons(port); // Set port to listen on
		peer.sin_addr.s_addr = htonl(INADDR_ANY); // Listen on any address

		// Create socket
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock == INVALID_SOCKET)
		{
			return false;
		}

		int yes = 1;
		if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes)) == SOCKET_ERROR)
		{
			closesocket(sock);
			return false;
		}

		// Bind the socket
		int res = bind(sock, reinterpret_cast<struct sockaddr*> (&peer), sizeof(peer));
		if (res == SOCKET_ERROR)
		{
			return false;
		}

		res = listen(sock, SOMAXCONN);
		if (res == SOCKET_ERROR)
		{
			return false;
		}

		return true;
	}

	std::vector<unsigned char> receive_pk(SOCKET sock)
	{
		std::vector<unsigned char> data(crypto_box_PUBLICKEYBYTES + crypto_box_NONCEBYTES + crypto_box_MACBYTES);
		unsigned char* data_ptr = data.data();
		size_t bytesLeft = data.size();

		size_t bytesRecv;
		while (bytesLeft > 0)
		{
			bytesRecv = recv(sock, reinterpret_cast<char*> (data_ptr), bytesLeft, 0); // Do not recv more than remaining bytes
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
			data_ptr += bytesRecv; // Shift pointer
			bytesLeft -= bytesRecv;
		}

		std::vector<unsigned char> pk = util::decrypt(data, initial_pk, initial_sk);
		return pk;
	}

	void send_pk(SOCKET sock, std::vector<unsigned char>& pk, std::vector <unsigned char>& sk)
	{
		std::vector <unsigned char> encrypted_data = util::encrypt(public_key, pk, sk);
		size_t bytesLeft = encrypted_data.size();

		size_t bytesSent = 0, total_bytesSent = 0;
		while (bytesLeft > 0)
		{
			bytesSent = send(sock, reinterpret_cast<char*>(encrypted_data.data() + total_bytesSent), bytesLeft, 0);
			if (bytesSent == SOCKET_ERROR || bytesSent <= 0)
			{
				return;
			}

			total_bytesSent += bytesSent;
			bytesLeft -= bytesSent;
		}
	}

public:

	FileTransfer(std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		this->initial_pk = pk;
		this->initial_sk = sk;
	}

	// Will be called when uploading(***Act as a client***)
	FileTransfer(std::string ip, unsigned port, std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		this->IP_address = ip;
		this->port = port;
		this->initial_pk = pk;
		this->initial_sk = sk;

		// Prepare peer addr struct for connection
		sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		peer.sin_family = AF_INET;
		peer.sin_port = htons(port);
		inet_pton(AF_INET, IP_address.c_str(), &peer.sin_addr);
	}

	~FileTransfer()
	{
		closesocket(sock);
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

		// Generate keys, and exchange pub keys
		std::pair<std::vector<unsigned char>, std::vector<unsigned char>> key_pair = util::generate_key_pair();
		public_key = key_pair.first;
		secret_key = key_pair.second;
		peer_public_key = receive_pk(peerSock);
		if (peer_public_key.empty())
		{
			closesocket(peerSock);
			return TransferStatus::FAILURE;
		}
		send_pk(peerSock, initial_pk, initial_sk);

		// Receive the file size
		size_t fileSize = recvFileSize(peerSock);
		if (fileSize == 0)
		{
			closesocket(peerSock);
			return TransferStatus::FAILURE;
		}
		encrypted_buffer.clear(); // Reset the buffer
		encrypted_buffer.resize(fileSize + crypto_box_MACBYTES + crypto_box_NONCEBYTES);
		fileSize = encrypted_buffer.size();

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
			res = recv(peerSock, reinterpret_cast<char*>(encrypted_buffer.data() + bytesRead), fileSize - bytesRead, 0);
			if (res == SOCKET_ERROR)
			{
				// send error 
				sendHeader(peerSock, TransferHeader::FAILURE);
				closesocket(peerSock);
				return TransferStatus::FAILURE;
			}
			if (res == 0) // Server closed connection
			{
				if (bytesRead == fileSize)
				{
					break;
				}
				sendHeader(peerSock, TransferHeader::FAILURE);
				closesocket(peerSock);
				return TransferStatus::CONNECTION_CLOSED;
			}

			bytesRead += res;
		}

		if (bytesRead != fileSize) // Not all bytes received
		{
			sendHeader(peerSock, TransferHeader::FAILURE);
			closesocket(peerSock);
			return TransferStatus::INCOMPLETE_RECV;
		}

		// Decrypt the file
		buffer.clear();
		buffer = util::decrypt(encrypted_buffer, peer_public_key, secret_key);

		// Compute a hash
		std::vector<unsigned char> file_hash(crypto_hash_sha256_BYTES);
		crypto_hash_sha256(file_hash.data(), reinterpret_cast<unsigned char*> (buffer.data()), buffer.size());

		// Ensure correct data
		if (confirmFileHash_recv(file_hash, peerSock))
		{
			closesocket(peerSock);
			return TransferStatus::SUCCESS;
		}
		else
		{
			closesocket(peerSock);
			return TransferStatus::HASH_FAILED;
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

		// Generate keys, and exchange pub keys
		std::pair<std::vector<unsigned char>, std::vector<unsigned char>> key_pair = util::generate_key_pair();
		public_key = key_pair.first;
		secret_key = key_pair.second;
		send_pk(sock, initial_pk, initial_sk);

		peer_public_key = receive_pk(sock);
		if (peer_public_key.empty())
		{
			sendHeader(sock, TransferHeader::FAILURE);
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
		std::vector<unsigned char> fileSizeVector = util::int32ToVector(netFileSize);
		std::vector<unsigned char> encrypted_fileSize = util::encrypt(fileSizeVector, peer_public_key, secret_key);

		int res = send(sock, reinterpret_cast<char*>(encrypted_fileSize.data()), encrypted_fileSize.size(), 0);
		if (res == SOCKET_ERROR)
		{
			sendHeader(sock, TransferHeader::FAILURE);
			return TransferStatus::FAILURE;
		}

		// Read the contents of the file into buffer
		file.seekg(0, std::ios::beg);
		if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize))
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
			file.write(reinterpret_cast<char*>(buffer.data()), buffer.size());
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















