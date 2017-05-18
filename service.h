/**
* Copyright (c) 2017-present,chenwj;
* All rights reserved.
* NOTE: base on boost asio network framwork, thread is safe. ps, max package size 30*1024
* DATE: 2017/04/07
*/
#ifndef _SERVICE_H_
#define _SERVICE_H_

#include "message.h"

#ifdef SHARED_EXPORTS
#   define SHARED_API __declspec(dllexport)
#else
#   define SHARED_API __declspec(dllimport)
#endif

typedef void(*readcallback_t)(int fd, char* buf, int len);
typedef void(*closecallback_t)(int fd, int err);
typedef void(*connectcallback_t)(int fd);

/* listen at port */
SHARED_API void iosev(int port, bool isblock);

/* connect service, if connect successful return fd else return -1. */
SHARED_API int ioconnect(const char *ip, int port);

/* close connect */
SHARED_API void ioclose(int fd);

/* send a buf to service , if error return -1 else return 0. 
	warning: if len > 30*1024 return error = -2. 
*/
SHARED_API int iosend(int fd, const char* buf, int len);

/* send a msg buf to service, if error return -1 else return 0. 
 * waring: if (sizeof(int32_t) + strlen(msg) + 1 + len) > 30*1024 return error = -2.
*/
SHARED_API int iosendmsg(int fd, const char* msg, const char* buf, int len);

/* set callback */
SHARED_API void setconnnectcb(connectcallback_t f);
SHARED_API void setreadcb(readcallback_t f);
SHARED_API void setclosecb(closecallback_t f);

#endif
