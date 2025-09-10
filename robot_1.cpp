#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <iostream>
#include <map>
#include <mutex>
#include <string>

typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;

struct ClientInfo {
    std::string type; // "robot" veya "user"
    int id;           // 1, 2, 3 ...
};

server ws_server;

// Thread-safe client map
std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> clients;
std::mutex clients_mutex;

// Mesaj handler
void on_message(connection_hdl hdl, server::message_ptr msg) {
    std::string payload = msg->get_payload();

    int target_id = -1;
    std::string message_text;

    // Basit format: "<target_id>:<mesaj>"
    auto pos = payload.find(':');
    if(pos != std::string::npos){
        target_id = std::stoi(payload.substr(0,pos));
        message_text = payload.substr(pos+1);
    } else {
        message_text = payload;
    }

    std::lock_guard<std::mutex> lock(clients_mutex);

    // Mesajı hedef ID'ye gönder
    for(auto &c : clients){
        if(c.second.id == target_id){
            ws_server.send(c.first, message_text, websocketpp::frame::opcode::text);
        }
    }

    std::cout << "Mesaj gönderildi: " << payload << std::endl;
}

// Yeni bağlantı açıldığında
void on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    int new_id = clients.size() + 1; // basit ID atama
    clients[hdl] = {"user", new_id};
    std::cout << "Yeni bağlantı ID: " << new_id << std::endl;
}

// Bağlantı kapandığında
void on_close(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    int id = clients[hdl].id;
    clients.erase(hdl);
    std::cout << "Bağlantı kapandı ID: " << id << std::endl;
}

// ----------------- main -----------------
int main() {
    ws_server.init_asio();

    ws_server.set_open_handler(&on_open);
    ws_server.set_close_handler(&on_close);
    ws_server.set_message_handler(&on_message);

    ws_server.listen(9001);
    ws_server.start_accept();

    std::cout << "WebSocket++ server 9001 portunda çalışıyor." << std::endl;

    ws_server.run();
}
