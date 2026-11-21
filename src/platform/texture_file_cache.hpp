#pragma once
// Read-only DDS bytes, never device handles. One owned prefetch worker; its destructor joins before storage dies.
// Each bank queued for loading hands its pack files to prefetch(); the worker reads them in order, so a race that streams
// several banks at once (a stage's end: the next stage's course, sky and objects) reads none of them on the game's
// thread (Wheelkit issue outrun-2006-0012: the pack is on a hard disk, about 20 ms a file when it is not in the cache).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
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
    ~TextureFileCache(){{std::lock_guard<std::mutex> lock(queue_mutex_);queue_.clear();}finish();}
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
    // Only this bank's indexed files, at most one capacity of input bytes per bank. No unbounded directory scan. A bank
    // queued while another is being read waits its turn in the same worker; the object-load step is never blocked.
    void prefetch(std::vector<std::filesystem::path> paths){
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.push_back(std::move(paths));
        if(running_)return;   // the worker takes it after the banks before it
        finish();
        try{running_=true;job_=std::async(std::launch::async,[this]{work();});}
        catch(...){running_=false;queue_.clear();} // unable to start: ordinary synchronous reads remain available
    }
    // Every bank handed to prefetch() has been read (or the worker could not start).
    bool ready(){
        {std::lock_guard<std::mutex> lock(queue_mutex_);if(running_)return false;}
        finish();return true;
    }
    void finish(){if(job_.valid())try{job_.get();}catch(...){} }
    TextureFileCacheStats stats()const{
        std::lock_guard<std::mutex> lock(mutex_);auto result=stats_;result.entries=entries_.size();return result;
    }
private:
    struct Entry {std::vector<std::uint8_t> bytes;std::uint64_t age{};};
    void work(){
        for(;;){
            std::vector<std::filesystem::path> paths;
            {std::lock_guard<std::mutex> lock(queue_mutex_);
             if(queue_.empty()){running_=false;return;}
             paths=std::move(queue_.front());queue_.pop_front();}
            std::size_t remaining=capacity_;
            try{for(const auto& path:paths){
                std::error_code error;const auto n=std::filesystem::file_size(path,error);
                if(error||n<128u||n>MaxFileBytes||n>remaining)continue;
                if(fetch(path,nullptr)){remaining-=std::size_t(n);std::lock_guard<std::mutex> lock(mutex_);++stats_.prefetched;}
            }}catch(...){} // read/cache failures are handled again by the ordinary loader
        }
    }
    bool fetch(const std::filesystem::path& path,std::vector<std::uint8_t>* out){
        {   std::lock_guard<std::mutex> lock(mutex_);
            if(const auto it=entries_.find(path);it!=entries_.end()){
                if(out)*out=it->second.bytes;
                it->second.age=++clock_;++stats_.hits;return true;
            }
        }
        // The disk read outside the lock: a reader on the game's thread never waits for the worker's file.
        std::vector<std::uint8_t> bytes;
        const bool read=read_file(path,bytes);
        std::lock_guard<std::mutex> lock(mutex_);
        if(!read){++stats_.failed;return false;}
        if(entries_.count(path)){if(out)*out=std::move(bytes);return true;}   // the other thread read it meanwhile
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
    std::mutex queue_mutex_;
    std::deque<std::vector<std::filesystem::path>> queue_;
    bool running_{};
    std::future<void> job_;
};
}
