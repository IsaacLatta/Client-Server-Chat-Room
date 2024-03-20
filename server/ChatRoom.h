#ifndef CHATROOM_H
#define CHATROOM_H

#include "Server.h"

class ChatRoom
{
private:
	Server server;
	std::atomic <bool> shouldQuit = false;
	std::condition_variable shutdownCondition;
	std::mutex shutdownMutex;
	User host;
	ThreadPool thread_pool;
	std::vector <std::string> commands = { "/sys","/upload","/whisper","/commands", "/users", "/end" };
	// message: /sys cmd, ex.) /sys cls
	void systemCMD(std::string& message)
	{
		int pos = message.find(' ');
		std::string cmd = message.substr(pos + 1, message.size() - 1);
		std::cout << "\n";
		system(cmd.c_str());
		std::cout << "\n> ";
	}

	void listUsersCMD(User& user)
	{
		std::string usernames = "Usernames\n---------\n";
		
		usernames += server.getUsers_str();
		
		if (user.getSocket() == server.getListenSocket())
		{	
			util::print(usernames);
			return;
		}

		send(user.getSocket(), usernames.c_str(), usernames.length(), 0);
	}

	// For /whisper cmd
	//</sender> /whisper </recipient> message ----> </sender> whispered: message
	void whisperCMD(std::string& message, User& user)
	{
		std::string recipient = util::getRecipient(message);

		SOCKET recipientSock = server.findSocketByUsername(recipient);
		if (recipientSock == INVALID_SOCKET) // User not found
		{
			message = "[!] User Not Found";
			if (user.getSocket() == server.getListenSocket())
				util::print(message);
			else
				send(user.getSocket(), message.c_str(), message.length(), 0);

			return;
		}

		message = util::getSender(message) + "whispered: " + util::getMessage(message);

		server.sendMessage(message, recipientSock);
	}

	void listCMDS(User& user)
	{
		std::string cmds = "Commands\n--------\n";
		cmds += "/whisper: Direct message a user by username(</recipient>)\n/users: List all users\n";
		cmds += "/sys: Execute an OS command(ex. /sys cmd)\n/upload: Upload file to another user\n";
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

	void fileTransfer(std::string& message, User& user)
	{
		std::string msg_to_send;
		std::string recipient = util::getRecipient(message);
		std::string fileName = util::getMessage(message);
		
		User* recipUser = server.findUserByUsername(recipient);
		
		if (recipUser == nullptr || *recipUser == user) // User DNE, cannot send transfer to self
		{
			msg_to_send = "[!] Invalid Username";
			server.sendMessage(msg_to_send, user.getSocket());
			return;
		}

		// Set transfer flags
		recipUser->signalStartOfTransfer();
		user.signalStartOfTransfer();
		
		msg_to_send = user.getUsername() + "File Transfer Request: " + fileName;
		if (user.getSocket() == server.getListenSocket())
		{
			msg_to_send += util::getFileSize(fileName); // Attach file size if the server is uploading
		}

		msg_to_send += "\n[*] Accept Transfer (y/n): ";
		try
		{
			server.sendMessage(msg_to_send, recipUser->getSocket());
			if (recipUser->awaitDecision())
			{
				msg_to_send = "[+] Transfer Approved";
				server.sendMessage(msg_to_send, user.getSocket());
				server.sendMessage(msg_to_send, recipUser->getSocket());

				server.sendTransferInfo(user.getSocket(), recipUser->getSocket()); // Send the ip and port to the downloader

				if (user.getSocket() == server.getListenSocket()) // Server is uploading
				{
					server.setFile(fileName);
					server.uploadFile(recipUser->getSocket());
				}
				else if (recipUser->getSocket() == server.getListenSocket()) // Server is downloading
				{
					server.downloadFile();
				}

				// Should maybe wait here for conformation from both parties
			}
			else
			{
				msg_to_send = "[-] Transfer Denied";
				server.sendMessage(msg_to_send, user.getSocket());
				server.sendMessage(msg_to_send, recipUser->getSocket());
			}
		}
		catch (std::exception& e)
		{
			std::string error_message = "[-] Transfer Failed";
			server.sendMessage(error_message, recipUser->getSocket());
			server.sendMessage(error_message, user.getSocket());
			util::print("[-] FAILED TRANSFER");
			error_message = "[-] Error: " + GetLastError();
			error_message += "\n[-] Exception: " + std::string(e.what());
			util::print(error_message);
		}

		// Reset transfer flags
		recipUser->signalEndOfTransfer();
		user.signalEndOfTransfer();
	}

	// Handles messages sent from the host
	void handleOutgoingMessages(std::string& message, User& user)
	{
		// Check for commands
		if (message.find("/sys") != std::string::npos)
		{
			systemCMD(message);
			return;
		}
		if (message.find("/end") != std::string::npos)
		{
			server.broadcastMessageExceptSender(message, user);
			shouldQuit = true;
			shutdownCondition.notify_one();
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

			message = user.getUsername() + message;
			fileTransfer(message, user);
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
			message = user.getUsername() + message;
			whisperCMD(message, user);
			return;
		}
		message = user.getUsername() + message;
		std::string str = util::getCommand(message);
		if (str.find("/") != std::string::npos && util::search(commands, str) == nullptr)
		{
			str = "[!] Command Not Found";
			util::print(str);
			return;
		}
		server.broadcastMessageExceptSender(message, user);
	}

	void handleIncomingMessages(std::string& message, User& user)
	{
		// Check for commands
		if (message.find("/quit") != std::string::npos)
		{
			server.removeUser(user);
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
		if (message.find("/upload") != std::string::npos)
		{
			fileTransfer(message, user);
			return;
		}

		server.broadcastMessageExceptSender(message, user);
	}

	// Handles client relay
	void handleClient(User* user)
	{
		std::string username = user->getUsername(); // Only for exit message, to avoid referencing a nullptr when
													//  the User obj is removed from the server
		try
		{
			std::string message;
			while (!shouldQuit)
			{
				message = server.recvMessage(*user);
				
				if (user->inFileTransfer())
				{
					if (message.find("$Yes") != std::string::npos)
					{
						user->setTransferDecision(true);
					}
					else
					{
						user->setTransferDecision(false);
					}

					user->wait_completeTransfer();
					continue;
				}

				handleIncomingMessages(message, *user);
			}
		}
		catch (std::exception& e)
		{
			if (shouldQuit)
				return;
			if (!server.userExists(username))
			{
				std::string user_exit_message = "[!] " + username + "Has Left The Chat";
				server.broadcastMessage(user_exit_message);
				return;
			}
			std::cout << e.what() << std::endl;
		}
	}

	// Loop for sending messages from the server
	void sendMessageLoop(User* user)
	{
		std::string message;
		int res;

		std::cout << "\033[2K\r";
		while (!shouldQuit)
		{
			message = "";
			
			std::getline(std::cin, message);
			std::cout << "> ";
			
			if (user->inFileTransfer())
			{
				if (message.find("y") != std::string::npos)
				{
					user->setTransferDecision(true);
				}
				else
				{
					user->setTransferDecision(false);
				}

				user->wait_completeTransfer();
				continue;
			}

			handleOutgoingMessages(message, *user);
		}
	}

	void getConnections()
	{
		std::string user_name;
		std::string msg_to_send;

		util::print("[*] Listening for connections ...");
		while (!shouldQuit)
		{
			if (!server.pendingConnection())
				continue;

			User* user = server.getConnection();
			if (user == nullptr)
			{
				std::cerr << "[-] Client Socket Creation Failed" << std::endl;
				continue;
			}

			util::print("[+] Client Connected");

			while (true)
			{
				user_name = server.recvMessage(*user);
				if (server.userExists(user_name))
				{
					msg_to_send = "[!] Username Taken\n";
					server.sendMessage(msg_to_send, user->getSocket());
				}
				else
				{
					msg_to_send = "$Valid";
					server.sendMessage(msg_to_send, user->getSocket());
					user->setUsername(user_name);
					break;
				}
			}
			
			thread_pool.pushTask(&ChatRoom::handleClient, this, user);
		}
	}

public:

	void run_chat_room()
	{
		try 
		{
			std::unique_lock <std::mutex> lock(shutdownMutex);
			std::string username;
			server.initializeServer();
			std::cout << "[*] Hosting Server";
			std::cout << "\n[*] Enter username: ";
			std::cin >> username;
			username = "</" + username + "> ";

			auto host = std::make_unique<User>(server.getListenSocket(), username);
			host->setIP(server.getIP());
			//host->setPort(server.getListenPort());
			host->setPort(51000);
			User* user = host.get();
			server.addUser(host);
			
			thread_pool.pushTask(&ChatRoom::getConnections, this);
			thread_pool.pushTask(&ChatRoom::sendMessageLoop, this, user);

			// Server
			shutdownCondition.wait(lock, [this] {return this->shouldQuit.load();  });
		}
		catch (const std::exception& e)
		{
			std::cerr << e.what() << std::endl;
			std::cerr << "[-] Error: " << WSAGetLastError() << std::endl;
			return;
		}
		std::cout << "\033[2K\r[+] Server Closed";
	}
};
#endif 

