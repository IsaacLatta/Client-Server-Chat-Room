#ifndef UTIL_H
#define UTIL_H
#include <iostream>
#include <mutex>
#include <sstream>
#include <iomanip>

#define SODIUM_STATIC
#include <sodium.h>

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

	//</j> /whisper </i> testing
	std::string getCommand(const std::string& message)
	{
		int start = message.find(' ');
		int end = message.find(' ', start + 1);
		std::string command = message.substr(start + 1, end - start - 1); // /command
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

	bool sodium_startup()
	{
		if (sodium_init() != 0)
		{
			return false;
		}
		return true;
	}

	std::pair<std::vector<unsigned char>, std::vector<unsigned char>> generate_key_pair()
	{
		std::vector<unsigned char> public_key(crypto_box_PUBLICKEYBYTES);
		std::vector<unsigned char> secret_key(crypto_box_SECRETKEYBYTES);
		crypto_box_keypair(public_key.data(), secret_key.data());
		std::pair<std::vector<unsigned char>, std::vector<unsigned char>> key_pair = std::make_pair(public_key, secret_key);
		return key_pair;
	}

	// Helper function to convert a byte into a two-character hexadecimal string.
	std::string byte_to_hex(unsigned char byte) {
		std::stringstream ss;
		ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
		return ss.str();
	}

	// Function to print a vector of bytes (such as a public key) in hexadecimal format.
	void print_hex(const std::vector<unsigned char>& data) {
		for (unsigned char byte : data) {
			std::cout << byte_to_hex(byte);
		}
		std::cout << std::endl;
	}

	std::vector<unsigned char> encrypt(std::vector<unsigned char>& data, std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		std::vector <unsigned char> encrypted_data(crypto_box_MACBYTES + data.size());

		unsigned char nonce[crypto_box_NONCEBYTES];
		randombytes_buf(nonce, sizeof(nonce));
		if (crypto_box_easy(encrypted_data.data(), data.data(), data.size(), nonce, pk.data(), sk.data()) != 0)
		{
			return std::vector <unsigned char>();
		}
		std::vector<unsigned char> data_to_send;
		data_to_send.reserve(sizeof(nonce) + encrypted_data.size());
		data_to_send.insert(data_to_send.begin(), nonce, nonce + sizeof(nonce));
		data_to_send.insert(data_to_send.end(), encrypted_data.begin(), encrypted_data.end());
		return data_to_send;
	}

	std::vector<unsigned char> decrypt(std::vector<unsigned char>& data, std::vector<unsigned char>& pk, std::vector<unsigned char>& sk)
	{
		std::vector<unsigned char> nonce(data.begin(), data.begin() + crypto_box_NONCEBYTES);
		std::vector<unsigned char> encrypted_data(data.begin() + crypto_box_NONCEBYTES, data.end());

		std::vector<unsigned char> decrypted_data(encrypted_data.size() - crypto_box_MACBYTES);
		if (crypto_box_open_easy(decrypted_data.data(), encrypted_data.data(), encrypted_data.size(), nonce.data(), pk.data(), sk.data()) != 0)
		{
			return std::vector <unsigned char>();
		}
		return decrypted_data;
	}

	std::vector<unsigned char> int32ToVector(int32_t val)
	{
		std::vector <unsigned char> ret_vec(4);
		for (int i = 0; i < 4; i++)
		{
			ret_vec[i] = ((val >> (i * 8)) & 0xFF); // Shift by 1 byte and comapre against the
		}											// mask to retrieve the corresponding int32_t byte 
		return ret_vec;
	}

	int32_t vectorToInt32(std::vector<unsigned char>& v)
	{
		if (v.size() < 4) // error
		{
			return -1;
		}

		int32_t res = 0;
		for (int i = 0; i < 4; ++i)
		{
			res = res | static_cast<int32_t> (v[i] << (i * 8));
		}
		return res;
	}

	template <typename T>
	std::vector <unsigned char> dataToVector(const T& data)
	{
		static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
		std::vector<unsigned char> v(sizeof(T));
		const unsigned char* data_ptr = reinterpret_cast<const unsigned char*>(&data);

		for (int i = 0; i < sizeof(T); i++)
		{
			v[i] = data_ptr[i];
		}
		return v;
	}

	std::vector<unsigned char> strToBin_IP(const std::string& ip) {
		std::vector<unsigned char> binaryIP;
		std::istringstream ipStream(ip);
		std::string ipPart;

		while (getline(ipStream, ipPart, '.'))
		{
			binaryIP.push_back(static_cast<unsigned char>(std::stoi(ipPart)));
		}

		return binaryIP;
	}

	std::string binToStr_IP(std::vector<unsigned char>& bin_IP)
	{
		std::string IP;

		for (size_t i = 0; i < bin_IP.size(); ++i)
		{
			IP += std::to_string(bin_IP[i]);
			if (i < bin_IP.size() - 1)
			{
				IP += ".";
			}
		}

		return IP;
	}
}

#endif 
