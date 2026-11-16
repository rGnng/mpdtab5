#pragma once
#include "Arduino.h"
#include <vector>
#include <utility>
class WebServer {
 public:
  std::vector<std::pair<std::string,std::string>> fields;
  bool hasArg(const String &name)const{for(const auto &p:fields)if(p.first==name.c_str())return true;return false;}
  String arg(const String &name)const{for(const auto &p:fields)if(p.first==name.c_str())return p.second;return "";}
};
