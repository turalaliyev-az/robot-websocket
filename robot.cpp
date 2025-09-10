/*
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <iostream>
#include <set>
#include <map>
#include <string>

typedef websocketpp::server<websocketpp::config::asio> server;

using websocketpp::connection_hdl;

struct ClientInfo {
    std::string type; // "robot" veya "user"
};

server ws_server;
std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> clients;

void on_open(connection_hdl hdl) {
    clients[hdl] = {"unknown"};
    std::cout << "Yeni bağlantı açıldı!" << std::endl;
}

void on_close(connection_hdl hdl) {
    clients.erase(hdl);
    std::cout << "Bağlantı kapandı!" << std::endl;
}

void on_message(connection_hdl hdl, server::message_ptr msg) {
    std::string payload = msg->get_payload();

    // Client tipi belirleme
    if (clients[hdl].type == "unknown") {
        if (payload == "robot") {
            clients[hdl].type = "robot";
            std::cout << "Bir robot bağlandı!" << std::endl;
        } else if (payload == "user") {
            clients[hdl].type = "user";
            std::cout << "Bir kullanıcı bağlandı!" << std::endl;
        }
        return;
    }

    // Mesaj yönlendirme
    if (clients[hdl].type == "user") {
        for (auto &c : clients) {
            if (c.second.type == "robot") {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    } else if (clients[hdl].type == "robot") {
        for (auto &c : clients) {
            if (c.second.type == "user") {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    std::cout << "Mesaj: " << payload << std::endl;
}

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
*/









































#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <iostream>
#include <map>
#include <string>
#include <mutex>
#include <thread>
#include <queue>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/prepared_statement.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;


typedef websocketpp::server<websocketpp::config::asio> server;
using websocketpp::connection_hdl;

struct ClientInfo {
    std::string type; // "robot" veya "user"
    std::string id;   // Robot veya kullanıcı ID
};

struct RobotStatus {
    int x = 0;
    int y = 0;
    int battery = 100;
    std::string sensor_data = "";
    std::map<int,std::string> pins; // pin durumu
};

server ws_server;

// Thread-safe client ve robot data map
std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> clients;
std::map<std::string, RobotStatus> robot_data; // robot_id -> RobotStatus
std::mutex clients_mutex;
std::mutex robot_mutex;

// MySQL async queue
struct MySQLTask {
    std::string query;
};
std::queue<MySQLTask> mysql_queue;
std::mutex mysql_mutex;

// ----------------- MySQL yazma thread -----------------
void mysql_worker() {
    try {
        sql::Driver* driver = get_driver_instance();
        std::unique_ptr<sql::Connection> con(driver->connect("tcp://localhost:3306", "user", "password"));
        con->setSchema("robot_db");

        while(true) {
            MySQLTask task;
            {
                std::lock_guard<std::mutex> lock(mysql_mutex);
                if(!mysql_queue.empty()){
                    task = mysql_queue.front();
                    mysql_queue.pop();
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
            }

            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(task.query));
            pstmt->execute();
        }

    } catch (sql::SQLException &e) {
        std::cout << "MySQL Error: " << e.what() << std::endl;
    }
}

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

    std::lock_guard<std::mutex> lock(clients_mutex);

    // Tip belirleme
    if (clients[hdl].type == "unknown") {
        clients[hdl].type = root["type"].get<std::string>();
        clients[hdl].id   = root["id"].get<std::string>();

        if(clients[hdl].type == "robot") {
            std::lock_guard<std::mutex> lock2(robot_mutex);
            robot_data[clients[hdl].id] = RobotStatus();
            std::cout << "Robot bağlandı: " << clients[hdl].id << std::endl;
        } else {
            std::cout << "Kullanıcı bağlandı: " << clients[hdl].id << std::endl;
        }
        return;
    }

    std::string client_type = clients[hdl].type;
    std::string client_id   = clients[hdl].id;

    if(client_type == "robot") {
        std::string robot_id = client_id;
        std::lock_guard<std::mutex> lock2(robot_mutex);
        RobotStatus &r = robot_data[robot_id];

        std::string msg_type = root["type"].get<std::string>();
        if(msg_type == "status") {
            r.x           = root["x"].get<int>();
            r.y           = root["y"].get<int>();
            r.battery     = root["battery"].get<int>();
            r.sensor_data = root["sensor_data"].get<std::string>();

            for (auto& el : root["pins"].items()) {
                r.pins[std::stoi(el.key())] = el.value().get<std::string>();
            }

            // MySQL queue
            std::stringstream query;
            query << "INSERT INTO robot_status(robot_id,x,y,battery,sensor_data) VALUES('"
                  << robot_id << "'," << r.x << "," << r.y << "," << r.battery << ",'" << r.sensor_data << "')";
            {
                std::lock_guard<std::mutex> lock3(mysql_mutex);
                mysql_queue.push({query.str()});
            }

            // Kullanıcılara gönder
            for(auto &c : clients){
                if(c.second.type == "user" && c.second.id == robot_id) {
                    ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
                }
            }

        } else if(msg_type == "sensor") {
            for(auto &c : clients){
                if(c.second.type == "user" && c.second.id == robot_id) {
                    ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
                }
            }
        }

    } else if(client_type == "user") {
        std::string robot_id = root["robot_id"].get<std::string>();
        for(auto &c : clients){
            if(c.second.type == "robot" && c.second.id == robot_id) {
                ws_server.send(c.first, payload, websocketpp::frame::opcode::text);
            }
        }
    }

    std::cout << "Mesaj: " << payload << std::endl;
}


// ----------------- on_open ve on_close -----------------
void on_open(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    clients[hdl] = {"unknown",""};
    std::cout << "Yeni bağlantı açıldı!" << std::endl;
}

void on_close(connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::string id = clients[hdl].id;
    if(clients[hdl].type == "robot") {
        std::lock_guard<std::mutex> lock2(robot_mutex);
        robot_data.erase(id);
    }
    clients.erase(hdl);
    std::cout << "Bağlantı kapandı: " << id << std::endl;
}

// ----------------- main -----------------
int main() {
    // MySQL thread
    std::thread(mysql_worker).detach();

    ws_server.init_asio();

    ws_server.set_open_handler(&on_open);
    ws_server.set_close_handler(&on_close);
    ws_server.set_message_handler(&on_message);

    ws_server.listen(9001);
    ws_server.start_accept();

    std::cout << "WebSocket++ server 9001 portunda çalışıyor." << std::endl;

    ws_server.run();
}
