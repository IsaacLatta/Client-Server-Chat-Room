#ifndef USER_H
#define USER_H
#include <Winsock2.h>
#include <mutex>
#include <future>

class User
{
private:
	SOCKET sock;
	std::string username;
	std::string ip_address;
	unsigned int port;

	std::mutex transfer_complete_mutex;
	std::condition_variable transfer_complete_condition;
	std::promise <bool>  transferDecisionPromise;
	std::future <bool> transferDecisionFuture;

	std::vector <unsigned char> public_key;
	
	void resetTransfer()
	{
		transferDecisionPromise = std::promise<bool>();
		transferDecisionFuture = std::future<bool>();
		transferDecisionFuture = transferDecisionPromise.get_future();
	}

public:
	std::atomic <bool> isTransfering = false;

	User()
	{
		sock = INVALID_SOCKET;
		username = "</Anon> ";
		resetTransfer();
	}

	User(const User& user)
	{
		isTransfering = user.isTransfering.load();
		username = user.username;
		sock = user.sock;
	}

	User(const SOCKET sock, const std::string username)
	{
		this->sock = sock;
		this->username = username;
		resetTransfer();
	}

	User(const SOCKET sock)
	{
		this->sock = sock;
		this->username = "</Anon> ";
		resetTransfer();
	}

	void set_public_key(std::vector<unsigned char>& pk)
	{
		public_key = pk;
	}

	std::vector <unsigned char> get_pk()
	{
		return public_key;
	}

	void setUsername(const std::string username)
	{
		this->username = username;
	}

	void setSocket(const SOCKET sock)
	{
		this->sock = sock;
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
		return ((user.getSocket() == sock) && (username == user.getUsername()));
	}

	void setTransferDecision(bool decision)
	{
		transferDecisionPromise.set_value(decision);
	}

	bool awaitDecision()
	{
		bool decision = transferDecisionFuture.get();
		resetTransfer();
		return decision;
	}

	void wait_completeTransfer()
	{
		std::unique_lock <std::mutex> lock(transfer_complete_mutex);

		transfer_complete_condition.wait(lock, [this] {return !isTransfering.load(); });
	}

	void signalEndOfTransfer()
	{
		std::lock_guard <std::mutex> lock(transfer_complete_mutex);
		isTransfering = false;
		transfer_complete_condition.notify_one();
	}

	void signalStartOfTransfer()
	{
		std::lock_guard <std::mutex> lock(transfer_complete_mutex);
		isTransfering = true;
	}

	bool inFileTransfer()
	{
		std::lock_guard <std::mutex> lock(transfer_complete_mutex);
		return isTransfering;
	}

	void setIP(const std::string ip)
	{
		ip_address = ip;
	}

	void setPort(unsigned int port)
	{
		this->port = port;
	}

	std::string getIP()
	{
		return ip_address;
	}

	unsigned getPort()
	{
		return port;
	}

};
#endif

