#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <uuid/uuid.h>
#include <json/json.h>

typedef websocketpp::server<websocketpp::config::asio> server;
typedef websocketpp::connection_hdl connection_hdl;
typedef std::map<connection_hdl, std::string, std::owner_less<connection_hdl>> con_list;

enum EventTypes {
    USER_EVENT,
    CONTENT_CHANGE,
};

struct ClientData {
    std::string type;
    std::string username;
    std::string content;
};

con_list clients;
std::map<std::string, ClientData> users;
std::string editor_content;
std::vector<std::string> user_activity;

void broadcast_message(const std::string& message, server& ws_server) {
    for (const auto& client : clients) {
        ws_server.send(client.first, message, websocketpp::frame::opcode::text);
    }
}

void handle_message(const std::string& message, const connection_hdl& hdl, server& ws_server) {
    Json::Value data_from_client;
    Json::Reader reader;
    if (!reader.parse(message, data_from_client)) {
        std::cerr << "Failed to parse JSON message from client\n";
        return;
    }

    Json::Value json_message(Json::objectValue);
    std::string type = data_from_client["type"].asString();

    if (type == "userevent") {
        std::string user_id = data_from_client["userId"].asString();
        users[user_id] = {type, data_from_client["username"].asString(), ""};
        user_activity.push_back(users[user_id].username + " joined to edit the document");
        json_message["type"] = type;
        json_message["data"]["users"] = Json::Value(Json::objectValue);
        for (const auto& user : users) {
            json_message["data"]["users"][user.first] = user.second.username;
        }
        json_message["data"]["userActivity"] = Json::Value(Json::arrayValue);
        for (const auto& activity : user_activity) {
            json_message["data"]["userActivity"].append(activity);
        }
    } else if (type == "contentchange") {
        std::string content = data_from_client["content"].asString();
        editor_content = content;
        json_message["type"] = type;
        json_message["data"]["editorContent"] = editor_content;
        json_message["data"]["userActivity"] = Json::Value(Json::arrayValue);
        for (const auto& activity : user_activity) {
            json_message["data"]["userActivity"].append(activity);
        }
    }

    Json::FastWriter writer;
    std::string data = writer.write(json_message);
    broadcast_message(data, ws_server);
}

void handle_disconnect(const connection_hdl& hdl, server& ws_server) {
    auto it = clients.find(hdl);
    if (it != clients.end()) {
        std::string user_id = it->second;
        std::cout << user_id << " disconnected.\n";

        std::string username = users[user_id].username;
        user_activity.push_back(username + " left the document");

        Json::Value json_message(Json::objectValue);
        json_message["type"] = "userevent";
        json_message["data"]["users"] = Json::Value(Json::objectValue);
        json_message["data"]["userActivity"] = Json::Value(Json::arrayValue);
        for (const auto& user : users) {
            json_message["data"]["users"][user.first] = user.second.username;
        }
        for (const auto& activity : user_activity) {
            json_message["data"]["userActivity"].append(activity);
        }

        Json::FastWriter writer;
        std::string data = writer.write(json_message);
        broadcast_message(data, ws_server);

        clients.erase(it);
        users.erase(user_id);
    }
}

int main() {
    server ws_server;

    ws_server.set_message_handler([&](connection_hdl hdl, server::message_ptr msg) {
        handle_message(msg->get_payload(), hdl, ws_server);
    });

    ws_server.set_open_handler([&](connection_hdl hdl) {
        uuid_t uuid;
        uuid_generate(uuid);
        char user_id[37];
        uuid_unparse(uuid, user_id);
        clients[hdl] = user_id;

        std::cout << "Received a new connection from " << user_id << "\n";
        std::cout << user_id << " connected.\n";
    });

    ws_server.set_close_handler([&](connection_hdl hdl) {
        handle_disconnect(hdl, ws_server);
    });

    int port = 8000;
    try {
        ws_server.init_asio();
        ws_server.listen(port);
        ws_server.start_accept();
        std::cout << "WebSocket server is running on port " << port << std::endl;
        ws_server.run();
    } catch (websocketpp::exception const& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown exception" << std::endl;
    }

    return 0;
}
