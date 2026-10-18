#include <algorithm>
#include <cctype>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace fs = std::filesystem;
static constexpr int PORT = 8765;
static volatile std::sig_atomic_t running = 1;

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream s; s << in.rdbuf(); return s.str();
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}
std::string json_escape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        switch(c) {
            case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break; case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default: if (c >= 0x20) o += static_cast<char>(c);
        }
    }
    return o;
}
std::string url_decode(std::string s) {
    std::string out;
    for (size_t i=0; i<s.size(); ++i) {
        if (s[i]=='+' ) out+=' ';
        else if (s[i]=='%' && i+2<s.size()) {
            try { out += static_cast<char>(std::stoi(s.substr(i+1,2),nullptr,16)); i+=2; }
            catch (...) { out+=s[i]; }
        } else out+=s[i];
    }
    return out;
}
std::string get_json_string(const std::string& body, const std::string& key) {
    // Small, dependency-free parser for a simple JSON string field.
    const std::string needle = "\"" + key + "\"";
    size_t p=body.find(needle); if(p==std::string::npos) return {};
    p=body.find(':',p+needle.size()); if(p==std::string::npos) return {};
    p=body.find('"',p+1); if(p==std::string::npos) return {};
    std::string out; bool esc=false;
    for (++p; p<body.size(); ++p) {
        char c=body[p];
        if (esc) { if(c=='n') out+='\n'; else if(c=='r') out+='\r'; else if(c=='t') out+='\t'; else out+=c; esc=false; }
        else if(c=='\\') esc=true;
        else if(c=='"') break;
        else out+=c;
    }
    return out;
}
std::vector<fs::path> knowledge_files() {
    std::vector<fs::path> v;
    fs::path dir="AiBalasan";
    std::error_code ec;
    if (fs::exists(dir,ec)) for (auto& e: fs::directory_iterator(dir,ec))
        if (e.is_regular_file() && e.path().extension()==".txt") v.push_back(e.path());
    std::sort(v.begin(),v.end()); return v;
}
std::string make_reply(const std::string& message) {
    std::string msg=lower(message);
    int best=-1; std::string answer;
    for (const auto& p : knowledge_files()) {
        std::istringstream lines(read_file(p)); std::string line, keywords, reply, fallback;
        while(std::getline(lines,line)) {
            if(line.rfind("kata kunci:",0)==0 || line.rfind("keywords:",0)==0) {
                keywords=line.substr(line.find(':')+1);
            } else if(line.rfind("jawaban:",0)==0 || line.rfind("answer:",0)==0) {
                reply=line.substr(line.find(':')+1);
                while(!reply.empty() && std::isspace(static_cast<unsigned char>(reply.front()))) reply.erase(reply.begin());
            } else if(!line.empty() && line[0]!='#') fallback=line;
        }
        int score=0;
        std::stringstream ks(keywords); std::string word;
        while(std::getline(ks,word,',')) {
            while(!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) word.erase(word.begin());
            while(!word.empty() && std::isspace(static_cast<unsigned char>(word.back()))) word.pop_back();
            if(word.size()>1 && msg.find(lower(word))!=std::string::npos) score += static_cast<int>(word.size());
        }
        if(score>best && !reply.empty()) { best=score; answer=reply; }
        if(answer.empty() && !fallback.empty()) answer=fallback;
    }
    if(best>0 && !answer.empty()) return answer;
    if(msg.find("halo")!=std::string::npos || msg.find("hai")!=std::string::npos)
        return "Halo! Saya Maghribi AI lokal. Apa yang ingin Anda ketahui?";
    if(msg.find("bantuan")!=std::string::npos || msg.find("help")!=std::string::npos)
        return "Anda bisa bertanya di chat, atau buka panel File untuk melihat executable dalam folder Maghribi/.";
    if(!answer.empty()) return answer;
    return "Saya belum menemukan jawaban yang cocok di AiBalasan. Tambahkan kata kunci dan jawaban pada file .txt agar pengetahuan saya bertambah.";
}
bool safe_filename(const std::string& name) {
    if(name.empty() || name=="." || name=="..") return false;
    return name.find('/')==std::string::npos && name.find('\\')==std::string::npos &&
           name.find("..")==std::string::npos;
}
std::string files_json() {
    std::ostringstream out; out << "{\"files\":[";
    bool first=true; std::error_code ec;
    fs::path dir="Maghribi";
    if(fs::exists(dir,ec)) for(auto& e:fs::directory_iterator(dir,ec)) {
        if(!e.is_regular_file()) continue;
        auto perms=e.status(ec).permissions();
        bool executable=(perms & (fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec)) != fs::perms::none;
        if(!executable) continue;
        if(!first) out << ','; first=false;
        out << "\"" << json_escape(e.path().filename().string()) << "\"";
    }
    out << "]}"; return out.str();
}
std::string handle(const std::string& method, const std::string& path, const std::string& body) {
    if(method=="GET" && path=="/api/status") return "{\"ok\":true,\"name\":\"MaghribiSDK\",\"compiler\":\"C++17\"}";
    if(method=="GET" && path=="/api/files") return files_json();
    if(method=="POST" && path=="/api/chat") {
        auto msg=get_json_string(body,"message");
        if(msg.empty()) return "{\"reply\":\"Silakan tulis pesan terlebih dahulu.\"}";
        return "{\"reply\":\""+json_escape(make_reply(msg))+"\"}";
    }
    if(method=="POST" && path=="/api/run") {
        std::string name=get_json_string(body,"file");
        if(!safe_filename(name)) return "{\"ok\":false,\"message\":\"Nama file tidak valid.\"}";
        fs::path target=fs::path("Maghribi")/name;
        std::error_code ec;
        if(!fs::exists(target,ec) || !fs::is_regular_file(target,ec))
            return "{\"ok\":false,\"message\":\"Executable tidak ditemukan.\"}";
        auto perms=fs::status(target,ec).permissions();
        if((perms & (fs::perms::owner_exec|fs::perms::group_exec|fs::perms::others_exec))==fs::perms::none)
            return "{\"ok\":false,\"message\":\"File belum memiliki izin executable.\"}";
        fs::create_directories("logs",ec);
        pid_t pid=fork();
        if(pid==0) {
            std::ofstream log("logs/"+name+".log",std::ios::app);
            if(log) { dup2(fileno(stdout),STDOUT_FILENO); }
            execl(target.c_str(), target.c_str(), (char*)nullptr);
            _exit(127);
        }
        if(pid<0) return "{\"ok\":false,\"message\":\"Gagal menjalankan proses.\"}";
        return "{\"ok\":true,\"message\":\"Proses dimulai: "+json_escape(name)+" (PID "+std::to_string(pid)+").\"}";
    }
    return "{\"error\":\"not_found\"}";
}
void stop_server(int) { running=0; }
int main() {
    std::signal(SIGINT,stop_server); std::signal(SIGTERM,stop_server);
    int server=socket(AF_INET,SOCK_STREAM,0);
    if(server<0) { std::cerr<<"socket gagal: "<<std::strerror(errno)<<"\n"; return 1; }
    int opt=1; setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK); addr.sin_port=htons(PORT);
    if(bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0 || listen(server,16)<0) {
        std::cerr<<"Tidak dapat bind/listen pada 127.0.0.1:"<<PORT<<": "<<std::strerror(errno)<<"\n";
        close(server); return 1;
    }
    std::cout<<"MaghribiSDK server: http://127.0.0.1:"<<PORT<<"\n";
    while(running) {
        sockaddr_in client{}; socklen_t len=sizeof(client);
        int fd=accept(server,reinterpret_cast<sockaddr*>(&client),&len);
        if(fd<0) { if(errno==EINTR) continue; if(!running) break; continue; }
        std::string req; char buf[4096]; ssize_t n;
        while((n=recv(fd,buf,sizeof(buf),0))>0) { req.append(buf,static_cast<size_t>(n)); if(req.find("\r\n\r\n")!=std::string::npos) break; }
        auto split=req.find("\r\n\r\n");
        if(split==std::string::npos) { close(fd); continue; }
        std::string headers=req.substr(0,split), body=req.substr(split+4);
        std::istringstream hs(headers); std::string method, target, version; hs>>method>>target>>version;
        size_t content_len=0, pos=headers.find("Content-Length:");
        if(pos!=std::string::npos) { try { content_len=std::stoul(headers.substr(pos+15)); } catch(...){} }
        while(body.size()<content_len) { n=recv(fd,buf,sizeof(buf),0); if(n<=0) break; body.append(buf,static_cast<size_t>(n)); }
        auto q=target.find('?'); if(q!=std::string::npos) target=target.substr(0,q);
        std::string response, type="application/json; charset=utf-8";
        if(method=="GET" && (target=="/" || target=="/index.html" || target=="/styles.css" || target=="/app.js")) {
            fs::path p=(target=="/")?"index.html":target.substr(1);
            response=read_file(p);
            if(p.extension()==".html") type="text/html; charset=utf-8";
            else if(p.extension()==".css") type="text/css; charset=utf-8";
            else type="application/javascript; charset=utf-8";
            if(response.empty()) { response="File UI tidak ditemukan."; type="text/plain; charset=utf-8"; }
        } else response=handle(method,target,body);
        std::ostringstream out; out<<"HTTP/1.1 200 OK\r\nContent-Type: "<<type<<"\r\nAccess-Control-Allow-Origin: http://127.0.0.1:8765\r\nAccess-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nConnection: close\r\nContent-Length: "<<response.size()<<"\r\n\r\n"<<response;
        auto wire=out.str(); send(fd,wire.data(),wire.size(),0); close(fd);
    }
    close(server); return 0;
}
