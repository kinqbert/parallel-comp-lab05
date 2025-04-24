#define  CRT_SECURE_NO_WARNINGS
#define  WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unordered_map>
#include <iostream>
#include <string>
#include <vector>
#pragma comment(lib,"ws2_32.lib")

using namespace std;
namespace fs = std::filesystem;

constexpr uint16_t PORT = 8080;
constexpr size_t   SEND_BUF = 8 * 1024;
const unordered_map<string,string> MIME {
    {".html","text/html; charset=utf-8"}, {".htm","text/html"},
    {".css","text/css"},  {".js","text/javascript"},
    {".png","image/png"}, {".jpg","image/jpeg"}, {".jpeg","image/jpeg"},
    {".gif","image/gif"}, {".txt","text/plain"}
};

bool  sendAll(const SOCKET s,const char*buf,int len) {
    while (len>0) {
        const int n=send(s,buf,len,0);
        if(n<=0) return false; buf+=n; len-=n;
    }
    return true;
}
bool  recvLine(const SOCKET s,string& out){
    out.clear();
    char c;
    while(true){
        if(int r=recv(s,&c,1,0); r<=0) return false;
        if(c=='\r') continue;
        if(c=='\n') break;
        out.push_back(c);
        if(out.size()>4096) return false;
    }
    return true;
}
string httpTime(){
    char buf[128];
    const time_t t=time(nullptr);
    tm g{}; gmtime_s(&g,&t);
    strftime(buf,128,"%a, %d %b %Y %H:%M:%S GMT",&g);
    return buf;
}
void handleClient(SOCKET client){
    string reqLine;
    if(!recvLine(client,reqLine)){closesocket(client);return;}

    std::istringstream iss(reqLine);
    string method,path,ver; iss>>method>>path>>ver;

    string dummy;
    while(recvLine(client,dummy) && !dummy.empty()){}

    if(method!="GET"){
        const string msg="HTTP/1.1 405 Method Not Allowed\r\n\r\n";
        sendAll(client,msg.c_str(),(int)msg.size());
        closesocket(client); return;
    }

    if(path=="/") path="/index.html";
    fs::path file = fs::current_path() / path.substr(1);

    if(!fs::exists(file) || fs::is_directory(file)){
        const string body="<h1>404 Not Found</h1>";
        string resp="HTTP/1.1 404 Not Found\r\nContent-Length: "
                    + to_string(body.size()) + "\r\nContent-Type: text/html\r\n\r\n"
                    + body;
        sendAll(client,resp.c_str(),(int)resp.size());
        closesocket(client); return;
    }

    const uint64_t fsize = fs::file_size(file);
    string ext = file.extension().string();
    string mime = MIME.contains(ext)? MIME.at(ext) : "application/octet-stream";

    string header = "HTTP/1.1 200 OK\r\n"
                    "Date: "   + httpTime() + "\r\n"
                    "Content-Length: " + to_string(fsize) + "\r\n"
                    "Content-Type: "   + mime + "\r\n"
                    "Connection: close\r\n\r\n";

    if(!sendAll(client,header.c_str(),(int)header.size())){
        closesocket(client); return;
    }

    std::ifstream in(file,ios::binary);
    vector<char> buffer(SEND_BUF);
    while(in){
        in.read(buffer.data(),buffer.size());
        streamsize n=in.gcount();
        if(n<=0) break;
        if(!sendAll(client,buffer.data(),(int)n)){ break; }
    }
    closesocket(client);
}

int main(){
    WSADATA wsa;  if(WSAStartup(MAKEWORD(2,2),&wsa)!=0){cerr<<"WSAStartup\n"; return 1;}

    const SOCKET srv = socket(AF_INET,SOCK_STREAM,0);
    if(srv==INVALID_SOCKET){cerr<<"socket()\n";WSACleanup();return 1;}

    sockaddr_in addr{}; addr.sin_family=AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(PORT);

    if(bind(srv,reinterpret_cast<sockaddr *>(&addr),sizeof(addr))==SOCKET_ERROR){
        cerr<<"bind()\n"; closesocket(srv); WSACleanup(); return 1;
    }
    if(listen(srv,SOMAXCONN)==SOCKET_ERROR){
        cerr<<"listen()\n"; closesocket(srv); WSACleanup(); return 1;
    }
    cout<<"[SERVER] HTTP static server on http://localhost:"<<PORT<<"/  (Ctrl+C to stop)\n";

    while(true){
        SOCKET cli = accept(srv,nullptr,nullptr);
        if(cli==INVALID_SOCKET) continue;
        std::thread(handleClient,cli).detach();
    }
}
