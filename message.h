/* Copyright (c) 2016~2018 chenwj; see COPYRIGHT 
 * link:-lproto -std=c++11 -pthread -lpthread at linux
*/
#ifndef _MESSAGE_H_
#define _MESSAGE_H_
#include <stdint.h>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <google/protobuf/message.h> /*google proto header*/

typedef std::shared_ptr<google::protobuf::Message> message_t;
typedef std::function<void (int fd, const message_t & msg)> callback_t;
typedef std::map<const google::protobuf::Descriptor*, callback_t> callback_map_t;

// extern callback_map_t callbacks;

void setcallback(const google::protobuf::Descriptor* descriptior, callback_t func);

template<typename T>
void registerfunc(callback_t func)
{
	auto descriptior = T::descriptor();
	setcallback(descriptior, func);
	// callbacks[descriptior] = func;
}

callback_t getfunc(const std::string& name);
void invorkfun(int fd, const std::string& name, const std::string& data);

#endif
