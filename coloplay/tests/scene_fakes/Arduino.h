#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <type_traits>
#define PROGMEM
class String {
  std::string value_;
 public:
  String(const char *text=""):value_(text){}
  String(const std::string &text):value_(text){}
  String(char c):value_(1,c){}
  template<class T,typename std::enable_if<std::is_arithmetic<T>::value,int>::type=0>
  String(T n):value_(std::to_string(n)){}
  const char *c_str()const{return value_.c_str();}
  size_t length()const{return value_.size();}
  bool isEmpty()const{return value_.empty();}
  void reserve(size_t n){value_.reserve(n);}
  char operator[](size_t n)const{return value_[n];}
  auto begin()const->std::string::const_iterator{return value_.begin();}
  auto end()const->std::string::const_iterator{return value_.end();}
  int indexOf(char c,size_t at=0)const{auto n=value_.find(c,at);return n==std::string::npos?-1:int(n);}
  String substring(size_t start,size_t end=std::string::npos)const{return value_.substr(start,end==std::string::npos?end:end-start);}
  void trim(){auto start=value_.find_first_not_of(" \t\r\n");if(start==std::string::npos)value_.clear();else value_=value_.substr(start,value_.find_last_not_of(" \t\r\n")-start+1);}
  void toCharArray(char *out,size_t n)const{if(n){std::strncpy(out,c_str(),n-1);out[n-1]=0;}}
  String &operator+=(const String &s){value_+=s.value_;return *this;}
  friend String operator+(const String &a,const String &b){return a.value_+b.value_;}
  friend bool operator==(const String &a,const String &b){return a.value_==b.value_;}
  friend bool operator!=(const String &a,const String &b){return !(a==b);}
};
extern uint32_t gFakeNow;
inline uint32_t millis(){return gFakeNow;}
