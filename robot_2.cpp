#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>

typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;
using json = nlohmann::json;

struct ClientInfo {
    std::string type; // "robot" veya "user"
    int id;
};

server ws_server;
std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> clients;
std::mutex clients_mutex;

// ----------------- on_message -----------------
void on_message(connection_hdl hdl, server::message_ptr msg) {
    std::string payload = msg->get_payload();
    json root;

    try {
        root = json::parse(payload);
    } catch (json::parse_error& e) {
        std::cout << "JSON parse error: " << e.what() << std::endl;
        return;
    }

    std::string msg_type = root.value("type", "");
    int target_id = root.value("robot_id", -1);
    std::string content = root.value("payload", "");

    std::lock_guard<std::mutex> lock(clients_mutex);

    ClientInfo sender = clients[hdl];

    // Robot mesajları kullanıcıya yönlendir
    if(sender.type == "robot" && (msg_type == "status" || msg_type == "sensor")) {
        for(auto &c : clients) {
            if(c.second.type == "user" && c.second.id == sender.id) { // kendi ID'sine bağlı user
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    // Kullanıcı mesajları robotlara yönlendir
    if(sender.type == "user" && msg_type == "command") {
        for(auto &c : clients) {
            if(c.second.type == "robot" && c.second.id == target_id) {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    std::cout << "Mesaj: " << payload << std::endl;
}

// ----------------- on_open -----------------
void on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    int new_id = clients.size() + 1; // basit ID atama
    clients[hdl] = {"unknown", new_id};
    std::cout << "Yeni bağlantı ID: " << new_id << std::endl;
}

// ----------------- on_close -----------------
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
