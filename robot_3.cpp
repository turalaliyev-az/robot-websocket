#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <nlohmann/json.hpp>
#include <set>

typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;
using json = nlohmann::json;

// -------- Robot ve Client yapısı --------
struct ClientInfo {
    std::string type; // "robot" veya "user"
    int id;           // client ID
    int robot_id;     // kullanıcı için kendi robot ID, robotlar için kendi ID
};

server ws_server;
std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> clients;
std::mutex clients_mutex;

// -------- Önceden belirlenmiş robot ID listesi --------
std::set<int> robot_id_list = {1,2,3,4,5};

// -------- Mesaj handler --------
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
    int target_robot_id = root.value("robot_id", -1);
    std::string content = root.value("payload", "");

    std::lock_guard<std::mutex> lock(clients_mutex);

    ClientInfo sender = clients[hdl];

    // -------- Robot mesajları kullanıcıya yönlendir --------
    if(sender.type == "robot" && (msg_type == "status" || msg_type == "sensor")) {
        for(auto &c : clients) {
            if(c.second.type == "user" && c.second.robot_id == sender.id) {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    // -------- Kullanıcı mesajları robotlara yönlendir --------
    if(sender.type == "user" && msg_type == "command") {
        for(auto &c : clients) {
            if(c.second.type == "robot" && c.second.id == target_robot_id) {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    std::cout << "Mesaj: " << payload << std::endl;
}

// -------- Yeni bağlantı açıldığında --------
void on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    int new_id = clients.size() + 1; // client ID

    // İlk mesaj gelene kadar tip "unknown"
    clients[hdl] = {"unknown", new_id, -1};

    std::cout << "Yeni bağlantı ID: " << new_id << std::endl;
}

// -------- Bağlantı kapandığında --------
void on_close(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    int id = clients[hdl].id;
    clients.erase(hdl);
    std::cout << "Bağlantı kapandı ID: " << id << std::endl;
}

// -------- Tip ve robot ID belirleme --------
void on_message_set_type(connection_hdl hdl, server::message_ptr msg) {
    std::string payload = msg->get_payload();
    json root;

    try {
        root = json::parse(payload);
    } catch (...) {
        return;
    }

    std::string type = root.value("type","unknown");
    int robot_id = root.value("robot_id",-1);

    std::lock_guard<std::mutex> lock(clients_mutex);
    if(clients.find(hdl) != clients.end()){
        clients[hdl].type = type;
        if(type=="robot" && robot_id_list.count(robot_id))
            clients[hdl].id = robot_id; // robot ID eşleştirme
        if(type=="user")
            clients[hdl].robot_id = robot_id; // kullanıcı kendi robotunu seçer
    }

    std::cout << "Bağlantı ID " << clients[hdl].id 
              << " tipi: " << type 
              << " robot_id: " << robot_id << std::endl;
}

// ----------------- main -----------------
int main() {
    ws_server.init_asio();

    ws_server.set_open_handler(&on_open);
    ws_server.set_close_handler(&on_close);

    // İlk mesaj ile tip ve robot ID atanacak
    ws_server.set_message_handler([&](connection_hdl hdl, server::message_ptr msg){
        ClientInfo sender = clients[hdl];
        if(sender.type=="unknown")
            on_message_set_type(hdl,msg);
        else
            on_message(hdl,msg);
    });

    ws_server.listen(9001);
    ws_server.start_accept();

    std::cout << "WebSocket++ server 9001 portunda çalışıyor." << std::endl;

    ws_server.run();
}
