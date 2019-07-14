#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

namespace utils {


template <class Type>  
Type stringToNum(const std::string& str)
{
	std::istringstream iss(str);
	Type num;
	iss >> num;
	return num;
}

std::vector<std::string> split(const std::string& s, const std::string& pattern);
int becomeDaemon();

}


#endif