// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is a public header file, it must only include public header files.

#ifndef ENDIAN_H
#define ENDIAN_H

#include <stdint.h>
#include <arpa/inet.h>


namespace sockets
{

// the inline assembler code makes type blur,
// so we disable warnings for a while.

inline uint64_t hostToNetwork64(uint64_t host64)
{
  //return htobe64(host64);
	uint64_t   ret = 0;   
	uint32_t   high,low;

	low  = host64 & 0xFFFFFFFF;
	high = (host64 >> 32) & 0xFFFFFFFF;
	low  = htonl(low);
	high = htonl(high); 

	ret =   low;
	ret <<= 32;   
	ret |=  high;   

	return ret;   
}

inline uint32_t hostToNetwork32(uint32_t host32)
{
  //return htobe32(host32);
	return htonl(host32);
}

inline uint16_t hostToNetwork16(uint16_t host16)
{
  //return htobe16(host16);
	return htons(host16);
}

inline uint64_t networkToHost64(uint64_t net64)
{
  //return be64toh(net64);
	uint64_t   ret = 0;   
	uint32_t   high,low;

	low  = net64 & 0xFFFFFFFF;
	high = (net64 >> 32) & 0xFFFFFFFF;
	low  = ntohl(low);   
	high = ntohl(high);   

	ret =   low;
	ret <<= 32;   
	ret |=  high;   

	return ret;
}

inline uint32_t networkToHost32(uint32_t net32)
{
  //return be32toh(net32);
	return ntohl(net32);
}

inline uint16_t networkToHost16(uint16_t net16)
{
  //return be16toh(net16);
	return ntohs(net16);
}


}

#endif  // MUDUO_NET_ENDIAN_H
