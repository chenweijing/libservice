﻿// Author : chenwj
// time   : 2017/1/16
// Copyright :All rights reserved by chenwj @copyright 2017 ~ 2018.
// 
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
////////////////////////////////////////////////////////////////////////

#include "coder.h"
#include <sys/types.h>
#include <winsock.h>

namespace net{
Coder::Coder()
  : buf_size_(0)
  , buf_(NULL)
  , buf_index_(-1)
{
}

Coder::~Coder()
{
}

// set Msg name
void Coder::setMsgName(const std::string& name)
{
    msg_name_ = name;
}

// set msg data (protobuf data)
void Coder::setBody(const std::string& body)
{
    body_ = body;
}

// encoding the data to buff
void Coder::encoding()
{
    uint32_t len = sizeof(uint32_t)           // msg_len
                 + msg_name_.length()         // msg name
                 + body_.length()             // protobuf data (body)
                 + sizeof(uint32_t);          // check sum

    data_.resize(len + sizeof(uint32_t));     // total transport data length

    const char * buff = &*data_.begin();
		uint32_t index = 0;

    // len
    uint32_t header = htonl(len);
    memcpy((void*)buff, (void*)&header, sizeof(uint32_t));
    index += sizeof(uint32_t);

    // msg len 
    uint32_t msg_len = htonl(msg_name_.length());
    memcpy((void*)(buff + index), &msg_len, sizeof(uint32_t));
    index += sizeof(uint32_t);
    
    // msg name
    memcpy((void*)(buff + index), &*msg_name_.begin(), msg_name_.length());
    index += msg_name_.length();

    // protobuf data(body)
    memcpy((void*)(buff + index), &*body_.begin(), body_.length());
    index += body_.length();

    // check sum TODO:: only a test 
    uint32_t check_sum = htonl(0); 
    memcpy((void*)(buff + index), &check_sum, sizeof(check_sum));
}

// get encoding data buffer
const std::string &  Coder::getData()
{
    return data_;
}


// get Msg name
std::string Coder::getMsgName()
{
    return msg_name_;
}

std::string Coder::getBody()
{
    return body_;
}

// decoding the data from buff
void Coder::decoding(const char * buff, int size)
{
    int index = 0;
    uint32_t len = 0;

    // msg_len
    memcpy(&len, buff, sizeof(uint32_t));
    index += sizeof(uint32_t);
    uint32_t msg_len = ntohl(len);

    // msg name
    msg_name_.resize(msg_len);
    memcpy(&*msg_name_.begin(), (void*)(buff + index), msg_len);
    index += msg_len;

    // protobuf data(body)
    uint32_t body_len = size - sizeof(uint32_t) - msg_len - sizeof(uint32_t);
    body_.resize(body_len);
    memcpy(&*body_.begin(), (void*)(buff + index), body_len);
    index += body_len;

    uint32_t check_sum = 0;
    memcpy(&check_sum, (void*)(buff + index), sizeof(uint32_t));
}
} // !namespace net

