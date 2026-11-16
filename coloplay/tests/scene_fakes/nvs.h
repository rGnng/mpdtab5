#pragma once
#include "Preferences.h"
using esp_err_t=int;
using nvs_handle_t=int;
constexpr int ESP_OK=0,ESP_FAIL=1,NVS_READWRITE=1;
struct nvs_stats_t {unsigned free_entries=0;};
inline int nvs_open(const char *,int,int *out){*out=1;return FakeNvs::open?ESP_OK:ESP_FAIL;}
inline int nvs_set_blob(int,const char *key,const void *bytes,size_t n){
  ++FakeNvs::settingsWrites;if(FakeNvs::failSettings)return ESP_FAIL;
  auto p=static_cast<const uint8_t*>(bytes);FakeNvs::pending[key].assign(p,p+n);return ESP_OK;
}
inline int nvs_commit(int){for(const auto &p:FakeNvs::pending)FakeNvs::saved[p.first]=p.second;FakeNvs::pending.clear();return ESP_OK;}
inline void nvs_close(int){}
inline int nvs_get_stats(const char *,nvs_stats_t *out){out->free_entries=100;return ESP_OK;}
inline const char *esp_err_to_name(int error){return error?"ESP_FAIL":"ESP_OK";}
