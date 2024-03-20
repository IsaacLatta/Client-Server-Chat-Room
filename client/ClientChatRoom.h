#ifndef CLIENTCHATROOM_H
#define CLIENTCHATROOM_H

#include "ThreadPool.h"
#include "Client.h"

class ClientChatRoom
{
private:

	Client client;

	std::string uploadFile;
	std::vector <std::string> commands = { "/sys","/upload","/whisper","/commands", "/users", "/quit"};

	std::mutex shutdownMutex;
	std::mutex transfer_mutex;
	std::mutex transfer_complete_mutex;
	std::mutex upload_file_mutex;

	std::atomic <bool> shouldQuit = false;
	std::atomic <bool> fileTransfer = false;
	std::atomic <bool> transferComplete = false;
	std::atomic <bool> decided = false;

	std::condition_variable shutdownCondition;
	std::condition_variable transfer_condition;
	std::condition_variable transfer_complete_condition;

	ThreadPool threadPool;

	void signalShutdown()
	{
		shouldQuit = true;
		shutdownCondition.notify_one();
	}

	void setUploadFileName(std::string fileName)
	{
		std::lock_guard <std::mutex> lock(upload_file_mutex);
		uploadFile = fileName;
	}

	std::string getUploadFileName()
	{
		std::lock_guard <std::mutex> lock(upload_file_mutex);
		return uploadFile;
	}

	// message: /sys cmd, ex.) /sys cls
	void systemCMD(std::string& message)
	{
		int pos = message.find(' ');
		std::string cmd = message.substr(pos + 1, message.size() - 1);
		std::cout << "\n";
		system(cmd.c_str());
		std::cout << "\n> ";
	}

	// Handles outgoing messages
	void handleOutgoingMessage(std::string& message)
	{
		try
		{
			if (message.find("/quit") != std::string::npos)
			{
				client.sendMessage(message);
				signalShutdown();
				return;
			}
			if (message.find("/sys") != std::string::npos)
			{
				systemCMD(message);
				return;
			}
			if (message.find("/upload") != std::string::npos)
			{
				if (message.find_last_of('.') == std::string::npos || message.find_last_of('.') == message.length() - 1)
				{
					std::string str = "[!] Missing File Extension";
					util::print(str);
					return;
				}
				std::string str = client.getUsername() + message;
				std::string filename = util::getMessage(str);
				str = message + util::getFileSize(filename);
				client.sendMessage(str);
				send_fileTransfer(message);
			}
			else
			{
				std::string str = client.getUsername() + message;
				str = util::getCommand(str);
				if (str.find("/") != std::string::npos && util::search(commands, str) == nullptr)
				{
					str = "[!] Command Not Found";
					util::print(str);
					return;
				}
				client.sendMessage(message);
			}
			
		}
		catch (std::exception& e)
		{
			if (shouldQuit)
				return;
			else
				util::print(e.what());
		}
	}

	void send_fileTransfer(std::string& message)
	{
		std::unique_lock <std::mutex> lock(transfer_mutex);

		fileTransfer = true;
		message = client.getUsername() + message;
		setUploadFileName(util::getMessage(message));
		transfer_condition.wait(lock, [this] {return decided.load(); }); // Wait for transfer to complete
		decided = false;

		if (fileTransfer)
		{
			client.uploadFile(getUploadFileName());
			fileTransfer = false;
		}
		
		transferComplete = true;
		transfer_complete_condition.notify_one();
	}

	void receive_fileTransfer(const std::string& message)
	{
		std::unique_lock <std::mutex> lock(transfer_mutex);

		util::print(message);
		fileTransfer = true;
		transfer_condition.wait(lock, [this] {return decided.load(); }); // Wait for decision
		decided = false;

		if (fileTransfer) // Transfer approved
		{
			std::string response = "$Yes";
			client.sendMessage(response);

			response = client.recvMessage();
			util::print(response);
			client.downloadFile();
		}
		else 
		{
			std::string response = "$No";
			client.sendMessage(response);
		}

		// Unblock sendMessageLoop
		transferComplete = true;
		transfer_complete_condition.notify_one();
	}

	// Handles incoming messages
	void handleIncomingMessage(const std::string& message)
	{
		if (shouldQuit) // Connection closed for other reason
			return;
		if (message.find("/end") != std::string::npos) // Host closed server
		{
			util::print("[!] Server has been Closed!");
			signalShutdown();
			return;
		}
		if (message.find("File Transfer Request:") != std::string::npos)
		{
			receive_fileTransfer(message);
			return;
		}
		if (fileTransfer && message.find("[-] Transfer Denied"))
		{
			
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
			while (!shouldQuit)
			{
				message = client.recvMessage();
				if (fileTransfer)
				{
					std::unique_lock <std::mutex> lock(transfer_complete_mutex);
					util::print(message);
					if (message.find("[+] Transfer Approved") != std::string::npos)
					{
						decided = true;
						fileTransfer = true;
						transfer_condition.notify_one();
						transfer_complete_condition.wait(lock, [this] { return transferComplete.load(); }); // wait till transfer is complete
						transferComplete = false;
					}
					else
					{
						decided = true;
						fileTransfer = false;
						transfer_condition.notify_one();
						transfer_complete_condition.wait(lock, [this] { return transferComplete.load(); }); // wait till transfer is complete
						transferComplete = false;
					}
					continue;
				}
				
				handleIncomingMessage(message);
			}
		}
		catch (std::exception& e)
		{
			if (shouldQuit)
				return;
			std::cerr << e.what() << std::endl;
		}
	}

	// Sends chats to server
	void sendMessageLoop()
	{
		std::string message = " ";
		while (!shouldQuit)
		{
			std::getline(std::cin, message); // When connection closed from server side getline will require a key press to close.
			std::cout << "> ";

			if (message.empty())
			{
				continue;
			}
			
			if (fileTransfer)
			{
				decided = true;
				if (message.find("y") != std::string::npos) // Transfer approved
				{
					transfer_condition.notify_one();
					
					// Wait until transfer complete
					std::unique_lock <std::mutex> lock(transfer_complete_mutex);
					transfer_complete_condition.wait(lock, [this] { return transferComplete.load(); });
				}
				else
				{
					fileTransfer = false;
					transfer_condition.notify_one();
				}

				// Reset transfer flags
				transferComplete = false;
				fileTransfer = false;
				continue;
			}
			
			handleOutgoingMessage(message);
		}
		std::cout << "\033[2K\r"; // Remove "> " from terminal
	}

	void login()
	{
		std::string server_ip = ""; 
		std::string username, response;
		
		std::cout << "[*] Enter Servers IP: ";
		std::cin >> server_ip;

		std::cout << "[+] Connecting to server ...\n";
		client.initializeClient();
		client.connectToServer(server_ip);
		std::cout << "[+] Connected to Server!\n";

		while (true)
		{
			std::cout << "[*] Enter Username: ";
			std::cin >> username;
			username = "</" + username + "> ";

			client.setUsername(username);
			client.sendMessage("");
			response = client.recvMessage();
			if (response == "$Valid")
			{	
				break;
			}
			else
			{
				std::cout << response;
			}
		}
	}

public:

	void run_chat_room()
	{
		try
		{
			std::unique_lock <std::mutex> lock(shutdownMutex);
			login();

			threadPool.pushTask(&ClientChatRoom::sendMessageLoop, this);
			threadPool.pushTask(&ClientChatRoom::recvMessageLoop, this);

			shutdownCondition.wait(lock, [this] { return this->shouldQuit.load(); });
			client.closeClient();
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << "\n";
			std::cerr << "[-] Error: " << WSAGetLastError() << std::endl;
		}
	}
};
#endif

