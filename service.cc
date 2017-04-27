/**
* Copyright (c) 2017-present,chenwj;
* All rights reserved.
* NOTE: base on boost asio network framwork.
* DATE: 2017/04/07
*/
#include "service.h"

#include <stdio.h>
#include <thread>
#include <memory>
#include <map>
#include <mutex>
#include <ctime>
#include <condition_variable>

#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/placeholders.hpp>

#include "sigslot.h"
#include "io_service_pool.h"
#include "coder.h"

using namespace boost::asio;
using namespace boost::asio::ip;
using namespace boost::system;
using namespace sigslot;

static const int MAX_PACKAGE_SIZE = 1024 * 30;
static const int ACTIVE_TIME_OUT = 20;

boost::asio::io_service io_service;

struct socket_t{
	std::shared_ptr<tcp::socket> socket;
	std::shared_ptr<boost::asio::strand> strander;
	std::mutex mtx;
	int fd;
	int32_t msglen; /* message lenth. */
	int32_t readlen;
	char buf[MAX_PACKAGE_SIZE];
	std::time_t lastactivetime; /* last active time. */
};

/* global variables */
static io_service_pool iopool(4);
static std::shared_ptr<tcp::acceptor> acceptor = NULL;
static int cfd = 0; /* the count of fds.*/
static std::map < int/*fd*/, std::shared_ptr<socket_t>> fds;
static std::mutex sktmtx, exitmtx, closemtx;
static std::condition_variable exitcnd;
static int flag = 0;
static sigslot::signal1<int, multi_threaded_local> closesig;
readcallback_t read_pfn = NULL;
closecallback_t close_pfn = NULL;
connectcallback_t conncet_pfn = NULL;

/* onclose signal class */
class CloseSlot : public has_slots<multi_threaded_local>
{
public:
	void onClose(int fd);
};
void CloseSlot::onClose(int fd)
{
	lock_block<multi_threaded_local> lock(this);
	if (close_pfn != NULL){
		close_pfn(fd, -1);
	}
}

static CloseSlot closeslot;

/* callback functions */
static void accept();
static void addsocket(std::shared_ptr<socket_t> s);
static void delsocket(std::shared_ptr<socket_t> s);
static void delsocketbyfd(int fd);
static std::shared_ptr<struct socket_t> getsocket(int fd);
static void readheader(std::shared_ptr<socket_t> s);

static void handleAccept(std::shared_ptr<socket_t> s, const error_code& error);
static void handleReadHeader(std::shared_ptr<socket_t> s, const error_code& error, size_t bytes_transferred);
static void handleReadBody(std::shared_ptr<socket_t> s, const error_code& error, std::size_t bytes_transferred);
static void handleSend(std::shared_ptr<socket_t> s, const error_code& error, std::size_t bytes_transferred);
static void handleConnect(std::shared_ptr<socket_t> s, const error_code& error);

static void loopSendHeart();
static void loopActiveSocket();

static void defreadcb(int fd, char* buf, int len);
static void sigclose(int fd);

void iosev(int port)
{
	// 1. start ioservice
	iopool.start();

	// 2. construction tcp::acceptor
	if (acceptor == NULL)
		acceptor = std::shared_ptr<tcp::acceptor>(new tcp::acceptor(iopool.get_io_service()));

	// 3.listen
	tcp::endpoint endpoint(ip::tcp::v4(), port);
	acceptor->open(endpoint.protocol());
	acceptor->set_option(tcp::acceptor::reuse_address(true));
	acceptor->bind(endpoint);
	acceptor->listen();

	// 4. set read callback
	//if (read_pfn == NULL)
	//	setreadcb(defreadcb);

	// 4. set onclose
	closesig.connect(&closeslot, &CloseSlot::onClose);

	// 5. asyn accept
	accept();

	// 6. start active socket check thread
	if (flag == 0){
		flag = 1;
		std::thread t(std::bind(loopActiveSocket));
		t.detach();
	}

	// 7. waiting for service ...
	std::unique_lock<std::mutex> lk(exitmtx);
	exitcnd.wait(lk);
}

static void accept()
{
	std::shared_ptr<socket_t> s = std::shared_ptr<socket_t>(new socket_t);
	s->socket = std::shared_ptr<tcp::socket>(new tcp::socket(iopool.get_io_service()));
	s->strander = std::shared_ptr<boost::asio::strand>(new boost::asio::strand(iopool.get_io_service()));
	acceptor->async_accept(*s->socket, boost::bind(handleAccept, s, boost::asio::placeholders::error));
}

static void handleAccept(std::shared_ptr<socket_t> s, const boost::system::error_code& error)
{
	if (0 == error){
		addsocket(s);
		readheader(s);
	}

	accept();
}

int ioconnect(const char *ip, int port)
{
	// 1. io service start
	iopool.start();

	// 2. set read callback
	// if (read_pfn == NULL)
	//	setreadcb(defreadcb);

	// 2. set onclose
	closesig.connect(&closeslot, &CloseSlot::onClose);

	error_code ec;
	tcp::endpoint endpt(boost::asio::ip::address_v4::from_string(ip, ec), port);
	if (ec != 0){
		return -1;
	}

	std::shared_ptr<socket_t> s = std::shared_ptr<socket_t>(new socket_t);
	s->socket = std::shared_ptr<tcp::socket>(new tcp::socket(iopool.get_io_service()));
	s->strander = std::shared_ptr<boost::asio::strand>(new boost::asio::strand(iopool.get_io_service()));

	// s->socket->async_connect(endpt, boost::bind(handleConnect, s, boost::asio::placeholders::error));
	s->socket->connect(endpt, ec);
	if (ec){
		return -1;
	}
	else{
		std::unique_lock<std::mutex> lk(s->mtx);
		s->socket->set_option(tcp::socket::reuse_address(true));
		std::time_t now;
		std::time(&now);
		s->lastactivetime = now;
		addsocket(s);
		readheader(s);
	}

	if (flag == 0){
		flag = 1;
		std::thread t(std::bind(loopSendHeart));
		t.detach();
	}

	return s->fd;
}

static void handleConnect(std::shared_ptr<socket_t> s, const error_code& error)
{
	if (0 == error){
		std::unique_lock<std::mutex> lk(s->mtx);
		s->socket->set_option(tcp::socket::reuse_address(true));
		std::time_t now;
		std::time(&now);
		s->lastactivetime = now;
		addsocket(s);
		readheader(s);
	}else{
		if (conncet_pfn != NULL)
			conncet_pfn(-1);
	}
}

/* close connect */
void ioclose(int fd)
{
	delsocketbyfd(fd);
}

static void addsocket(std::shared_ptr<socket_t> s)
{
	if (s == NULL)
		return;

	std::unique_lock<std::mutex> lk(sktmtx);
	s->fd = cfd++;
	fds[s->fd] = s;

	if (conncet_pfn != NULL){
		conncet_pfn(s->fd);
	}
}

static void delsocket(std::shared_ptr<socket_t> s)
{
	if (s == NULL)
		return;

	int tmpfd = s->fd;

	// lock map
	{
		std::unique_lock<std::mutex> lk(sktmtx);
		auto itor = fds.find(s->fd);
		if (itor != fds.end()){
			s->socket->close();
			fds.erase(itor);
		}
	}

	closesig.emit(tmpfd);
}

static void delsocketbyfd(int fd)
{
	// lock map
	{
		std::unique_lock<std::mutex> lk(sktmtx);
		auto itor = fds.find(fd);
		if (itor != fds.end()){
			itor->second->socket->close();
			fds.erase(itor);
		}
	}

	closesig.emit(fd);
}

static std::shared_ptr<struct socket_t> getsocket(int fd)
{
	std::unique_lock<std::mutex> lk(sktmtx);
	auto itor = fds.find(fd);
	if (itor != fds.end()){
		return itor->second;
	}
	else{
		return NULL;
	}
}

static void readheader(std::shared_ptr<socket_t> s)
{
	boost::asio::async_read(*s->socket, boost::asio::buffer(&s->msglen, sizeof(int32_t)),
		s->strander->wrap(boost::bind(handleReadHeader, s, placeholders::error, placeholders::bytes_transferred)));
}

static void handleReadHeader(std::shared_ptr<socket_t> s, const boost::system::error_code& error, size_t bytes_transferred)
{
	s->readlen = 0;
	int len = ntohl(s->msglen);

	/* error */
	if ((error != 0) ||(bytes_transferred != sizeof(int32_t)) || (len < 0) || (len > MAX_PACKAGE_SIZE)){
		delsocket(s);
		return;
	}

	s->msglen = len;
	std::time_t now;
	std::time(&now);
	s->lastactivetime = now;

	if (len == 0){
		readheader(s);
		return;
	}
	else{
		boost::asio::async_read(*s->socket, boost::asio::buffer(s->buf + s->readlen, len),
			s->strander->wrap(boost::bind(handleReadBody, s, placeholders::error, placeholders::bytes_transferred)));
	}
}

static void handleReadBody(std::shared_ptr<socket_t> s, const error_code& error, std::size_t bytes_transferred)
{
	if (error != 0){
		delsocket(s);
		return;
	}

	s->readlen += bytes_transferred;

	/* loop reading until readlen == msglen */
	if (s->readlen < s->msglen){
		boost::asio::async_read(*s->socket, boost::asio::buffer(s->buf + s->readlen, s->msglen - s->readlen),
			s->strander->wrap(boost::bind(handleReadBody, s, placeholders::error, placeholders::bytes_transferred)));
	}
	else if (s->readlen == s->msglen){
		/* processing data */
		if (read_pfn != NULL){
			// printf("call back read pfn\n fn=0x%x", read_pfn);
			read_pfn(s->fd, s->buf, s->readlen);
		}
		else{
			// printf("read_pfn is NULL\n");
		}
		readheader(s);
	}	
}

int iosend(int fd, const char * buf, int len)
{
	if ( len + sizeof(int32_t) > MAX_PACKAGE_SIZE)
	{
		return -2;
	}

	auto s = getsocket(fd);
	if (s == NULL || buf == NULL || len <= 0){
		return -1;
	}

	std::unique_lock<std::mutex> lock(s->mtx);
	boost::asio::async_write(*s->socket, boost::asio::buffer(buf, len),
		s->strander->wrap(boost::bind(handleSend, s, placeholders::error, placeholders::bytes_transferred)));

	return 0;
}

/* send a buf to service */
int iosendmsg(int fd, const char* msg, const char* buf, int len)
{
	if ((sizeof(int32_t) +len + strlen(msg) + 1) > MAX_PACKAGE_SIZE)
	{
		return -2;
	}

	auto s = getsocket(fd);
	if (s == NULL || buf == NULL || len <= 0){
		return -1;
	}

	net::Coder coder;
	coder.setMsgName(msg);
	std::string data;
	data.resize(len);
	memcpy(&*data.begin(), buf, len);
	coder.setBody(data);
	coder.encoding();

	std::unique_lock<std::mutex> lock(s->mtx);
	boost::asio::async_write(*s->socket, boost::asio::buffer(coder.getData(), coder.getData().length()),
		s->strander->wrap(boost::bind(handleSend, s, placeholders::error, placeholders::bytes_transferred)));

	return 0;
}

static void handleSend(std::shared_ptr<socket_t> s, const error_code& error, std::size_t bytes_transferred)
{
	if (error != 0){
		delsocket(s);
	}
}

void setconnnectcb(connectcallback_t f)
{
	conncet_pfn = f;
}

void setreadcb(readcallback_t f)
{
	read_pfn = f;
}

void setclosecb(closecallback_t f)
{
	close_pfn = f;
}

static void loopSendHeart()
{
	while (flag){
		// printf("loop send heart\n");
		std::map < int/*fd*/, std::shared_ptr<socket_t>> fdstmp;
		int len = 0;
		int32_t heart = htonl(len);

		// copy sockets 
		{
			std::unique_lock<std::mutex> lk(sktmtx);
			fdstmp.insert(fds.begin(), fds.end());
		}

		std::time_t now;
		std::time(&now);

		for (auto& fd : fdstmp){
			// printf("fd %d send heart.\n", fd.second->fd);
			iosend(fd.second->fd, (const char *)&heart, sizeof(int32_t));
		}

		Sleep((ACTIVE_TIME_OUT / 3) * 1000);
	} // !while
}

static void loopActiveSocket()
{
	while (flag){
		// printf("loop check heart\n");
		std::map < int/*fd*/, std::shared_ptr<socket_t>> fdstmp;
		Sleep(ACTIVE_TIME_OUT * 1000);

		// copy sockets 
		{
			std::unique_lock<std::mutex> lk(sktmtx);
			fdstmp.insert(fds.begin(), fds.end());
		}

		std::time_t now;
		std::time(&now);

		for (auto& fd : fdstmp){
			if (now - fd.second->lastactivetime > ACTIVE_TIME_OUT){
				delsocketbyfd(fd.second->fd);
			}
		}
	} // !while
}

static void defreadcb(int fd, char* buf, int len)
{
	printf("default call back\n");
	net::Coder coder;
	coder.decoding(buf, len);
	printf("msg %s\n", coder.getMsgName().c_str());
	// invorkfun(fd, coder.getMsgName(), coder.getBody());
}

#if 0
/* ----------- for test ------------*/
void connectthread()
{
	for (int i = 0; i < 5; i++){
		Sleep(1000);
		printf("connect service...\n");
		ioconnect("127.0.0.1", 1234);
	}

}


void connectcb(int fd)
{
	printf("connect fd = %d\n", fd);
}

void readcb2(int fd, char* buf, int len)
{
	net::Coder coder;
	coder.decoding(buf, len);
	printf("msg %s\n", coder.getMsgName().c_str());
}

void closecb(int fd, int error)
{
	printf("fd %d  connection leave.\n", fd);
}

static void ontest(int fd, const message_t & v)
{
	//scan::searchFileByImgReq * msg = dynamic_cast<scan::searchFileByImgReq *>(v.get());
	std::cout << "call: " << "scan::searchFileByImgReq" << std::endl;
}

void test1(int fd, const message_t & msg)
{
	printf("fd %d on test function.\n", fd);
}

int main()
{
	//registerfunc<scan_test::test>(test1);

	setconnnectcb(connectcb);
	setreadcb(readcb2);
	setclosecb(closecb);
#if 0
	printf("connect service...\n");
	int fd = ioconnect("127.0.0.1", 1234);

	printf("client ... \n");
	//std::thread t(std::bind(connectthread));
	//t.detach();
	std::unique_lock<std::mutex> lk(exitmtx);
	exitcnd.wait(lk);
	
#else
	printf("service ... \n");
	iosev(1234);
#endif
	return 0;
}

#endif 