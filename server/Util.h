#ifndef UTIL_H
#define UTIL_H
#include <iostream>
#include <mutex>

namespace util 
{
	std::mutex print_mutex;

	void debug_log(std::string msg)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << "\n[*] " << msg << "\n";
	}

	void log_error(std::string msg)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << "\n[-] " << msg << "\n";
	}

	void print(std::string message)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << "\033[2K\r";
		std::cout << message  << "\n> ";
	}

	void print(std::string message, int temp)
	{
		std::lock_guard <std::mutex> lock(print_mutex);
		std::cout << message;
	}

	template <typename T>
	T* search(std::vector <std::unique_ptr<T>>& v, T& obj)
	{
		for (int i = 0; i < v.size(); i++)
		{
			if (obj == v[i])
				return &v[i];
		}
		return nullptr;
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

	std::string getSender(const std::string& message)
	{
		int pos = message.find(' ');
		std::string sender = message.substr(0, pos + 1); // </sender>
		return sender;
	}

	std::string getCommand(const std::string& message)
	{
		int pos = message.find(' ');
		int start = message.find(' ', pos + 1);
		std::string command = message.substr(pos + 1, start - pos); // /command
		return command;
	}

	std::string getRecipient(const std::string& message)
	{
		int pos = message.find(' ');
		int start = message.find(' ', pos + 1);
		int end = message.find(' ', start + 1);
		std::string recipient = message.substr(start + 1, end - start);// </recipient>
		return recipient;
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

	unsigned long checkSum(const char* buffer, const int length)
	{
		unsigned long sum = 0;
		for (size_t i = 0; i < length; i++)
		{
			sum += buffer[i];
		}
		return sum;
	}
	std::string getFileSize(const std::string& filename)
	{
		size_t fileSize;
		std::ifstream file(filename, std::ios::binary | std::ios::ate);
		if (file.is_open())
		{
			fileSize = file.tellg();
			file.close();
		}
		else
			return "(0 bytes)";

		std::string units[] = { " bytes", " KB", " MB", " GB", " TB" };

		int i = 0;
		double tempFileSize = static_cast<double>(fileSize);
		for (i = 0; i < 5 && tempFileSize > 1024; ++i)
		{
			tempFileSize /= 1024;
		}
		std::string ret_str = std::to_string(tempFileSize);
		ret_str = ret_str.substr(0, ret_str.find(".") + 3);
		ret_str = "(" + ret_str + units[i] + ")";
		return ret_str;
	}


}

#endif 
