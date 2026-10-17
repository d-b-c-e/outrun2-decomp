#pragma once
// Read-only DDS bytes, never device handles. One owned prefetch job; its destructor joins before storage dies.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
namespace outrun::platform {
struct TextureFileCacheStats {
    std::uint64_t hits{},reads{},failed{},evicted{},prefetched{};
    std::size_t bytes{},entries{};
};
class TextureFileCache {
public:
    static constexpr std::size_t MaxFileBytes=128u<<20;
    explicit TextureFileCache(std::size_t capacity):capacity_(capacity){}
    ~TextureFileCache(){finish();}
    TextureFileCache(const TextureFileCache&)=delete;
    TextureFileCache& operator=(const TextureFileCache&)=delete;
    static bool read_file(const std::filesystem::path& path,std::vector<std::uint8_t>& out){
        try{
            std::error_code error;const auto n=std::filesystem::file_size(path,error);
            if(error||n<128u||n>MaxFileBytes)return false;
            std::ifstream file(path,std::ios::binary);if(!file)return false;
            out.resize(std::size_t(n));file.read(reinterpret_cast<char*>(out.data()),std::streamsize(n));
            return file.gcount()==std::streamsize(n);
        }catch(...){return false;}
    }
    bool read(const std::filesystem::path& path,std::vector<std::uint8_t>& out){
        try{return fetch(path,&out);}catch(...){return read_file(path,out);} // optional caching must not refuse a read
    }
    // Only this bank's indexed files, at most one capacity of input bytes per job. No unbounded directory scan.
    // A prior in-flight bank is retained instead of blocking a new object-load step.
    void prefetch(std::vector<std::filesystem::path> paths){
        if(!ready())return;
        try{job_=std::async(std::launch::async,[this,paths=std::move(paths)]{
            std::size_t remaining=capacity_;
            try{for(const auto& path:paths){
                std::error_code error;const auto n=std::filesystem::file_size(path,error);
                if(error||n<128u||n>MaxFileBytes||n>remaining)continue;
                if(fetch(path,nullptr)){remaining-=std::size_t(n);std::lock_guard<std::mutex> lock(mutex_);++stats_.prefetched;}
            }}catch(...){} // read/cache failures are handled again by the ordinary loader
        });}catch(...){} // unable to start: ordinary synchronous reads remain available
    }
    bool ready(){
        if(!job_.valid())return true;
        if(job_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
        finish();return true;
    }
    void finish(){if(job_.valid())try{job_.get();}catch(...){} }
    TextureFileCacheStats stats()const{
        std::lock_guard<std::mutex> lock(mutex_);auto result=stats_;result.entries=entries_.size();return result;
    }
private:
    struct Entry {std::vector<std::uint8_t> bytes;std::uint64_t age{};};
    bool fetch(const std::filesystem::path& path,std::vector<std::uint8_t>* out){
        std::lock_guard<std::mutex> lock(mutex_);
        if(const auto it=entries_.find(path);it!=entries_.end()){
            if(out)*out=it->second.bytes;
            it->second.age=++clock_;++stats_.hits;return true;
        }
        std::vector<std::uint8_t> bytes;
        if(!read_file(path,bytes)){++stats_.failed;return false;}
        ++stats_.reads;
        if(out)*out=bytes; // retain successful caller bytes even if optional cache allocation fails
        const auto size=bytes.size();if(size>capacity_)return true;
        while(stats_.bytes>capacity_-size){
            auto oldest=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.second.age<b.second.age;});
            stats_.bytes-=oldest->second.bytes.size();entries_.erase(oldest);++stats_.evicted;
        }
        try{entries_.emplace(path,Entry{std::move(bytes),++clock_});stats_.bytes+=size;}catch(...){}
        return true;
    }
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::map<std::filesystem::path,Entry> entries_;
    TextureFileCacheStats stats_;
    std::uint64_t clock_{};
    std::future<void> job_;
};
}
