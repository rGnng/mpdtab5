#pragma once
#include <cstring>
#include <map>
#include <string>
#include <vector>
namespace FakeNvs {
extern std::map<std::string,std::vector<uint8_t>> saved,pending;
extern bool open,failSettings,failOrder;
extern int settingsWrites,orderWrites;
}
class Preferences {
 public:
  bool begin(const char *name,bool=false){return FakeNvs::open&&!std::strcmp(name,"scenes");}
  size_t getBytesLength(const char *key)const{auto at=FakeNvs::saved.find(key);return at==FakeNvs::saved.end()?0:at->second.size();}
  size_t getBytes(const char *key,void *out,size_t n)const{
    auto at=FakeNvs::saved.find(key);if(at==FakeNvs::saved.end()||at->second.size()!=n)return 0;
    std::memcpy(out,at->second.data(),n);return n;
  }
  size_t putBytes(const char *key,const void *bytes,size_t n){
    ++FakeNvs::orderWrites;if(FakeNvs::failOrder)return 0;
    auto p=static_cast<const uint8_t*>(bytes);FakeNvs::saved[key].assign(p,p+n);return n;
  }
};
