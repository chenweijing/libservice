// Author : chenwj
// Time   : 2017/1/16
// Copyright :All rights reserved by chenwj @copyright 2017 ~ 2018.
// 
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
////////////////////////////////////////////////////////////////////////

#ifndef _CODE_H_
#define _CODE_H_

#include <stdint.h>
#include <memory.h>
#include <string>

// data encode and decode
// format
// | len                 | -- sizeof (int32_t)
// | name len            | -- sizeof (int32_t)
// | msg name            | -- name len (name len)
// | body(protobuf data) | -- len - sizeof(int32_t) - sizeof(int32_t) - (name len) - sizeof(int32_t)
// | check sum           | -- sizeof (int32_t)
namespace net{
class Coder
{
public:
    Coder();
    ~Coder();
    // set Msg name
    void setMsgName(const std::string& name);
    // set msg data (protobuf data)
    void setBody(const std::string& body);
    // encoding the data to buff
    void encoding();
    // get the encoding buffer
    const std::string & getData();

    // get Msg name
    std::string getMsgName();
    // get msg data (protobuf data)
    std::string getBody();
    // decoding the data from buff
    void decoding(const char * buff, int size);
private:
    int buf_size_;
    int buf_index_;
    char * buf_;
    int32_t len_;
    int32_t name_len_;
    std::string msg_name_;
    std::string body_;
    int32_t check_sum_;

    std::string data_;
}; // !class Coder
} // !namespace net
#endif
