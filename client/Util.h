#ifndef UTIL_H
#define UTIL_H

#include <iostream>
#include <mutex>
#include <string>
#include <Winsock2.h>

namespace util 
{
	std::mutex print_mutex;

	void print(std::string message)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << "\033[2K\r";
		std::cout << message << "\n> ";
	}

	void print(std::string message, int temp)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << message;
	}

	template <typename T>
	T* search(std::vector <T>& v, T& obj)
	{
		for (int i = 0; i < v.size(); i++)
		{
			if (obj == v[i])
				return &v[i];
		}
		return nullptr;
	}

	std::string getMessage(const std::string& message)
	{
		std::string str = message;
		int pos = message.find(' ');
		int start = message.find(' ', pos + 1);
		int end = message.find(' ', start + 1);
		end = message.find(' ', end);
		str.erase(0, end + 1); // message
		return str;
	}


}

class User
{
private:
	SOCKET sock;
	std::string username;

public:
	User(SOCKET sock, std::string username)
	{
		this -> sock = sock;
		this -> username = username;
	}

	const SOCKET getSocket()
	{
		return sock;
	}

	const std::string getUsername()
	{
		return username;
	}

	bool operator==(User& user)
	{
		return (user.getSocket() == sock);
	}

};

#endif 
