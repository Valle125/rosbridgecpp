/*
 *  Created on: Apr 16, 2018
 *      Author: Poom Pianpak
 */

#ifndef ROSBRIDGECPP_ROSBRIDGE_WS_CLIENT_HPP_
#define ROSBRIDGECPP_ROSBRIDGE_WS_CLIENT_HPP_

#include "Simple-WebSocket-Server/client_ws.hpp"

#include "rapidjson/include/rapidjson/document.h"
#include "rapidjson/include/rapidjson/writer.h"
#include "rapidjson/include/rapidjson/stringbuffer.h"

#include <chrono>
#include <functional>
#include <thread>
#include <future>
//#include <functional>
#include <mutex>

using WsClient = SimpleWeb::SocketClient<SimpleWeb::WS>;
using InMessage =  std::function<void(std::shared_ptr<WsClient::Connection>, std::shared_ptr<WsClient::InMessage>)>;

class RosbridgeWsClient
{
  std::string server_port_path;
  std::unordered_map<std::string, std::shared_ptr<WsClient>> client_map;
  std::mutex map_mutex;

  void start(const std::string& client_name, std::shared_ptr<WsClient> client, const std::string& message)
  {
    if (!client->on_open)
    {
#ifdef DEBUG
      client->on_open = [client_name, message](std::shared_ptr<WsClient::Connection> connection) {
#else
      client->on_open = [message](std::shared_ptr<WsClient::Connection> connection) {
#endif

#ifdef DEBUG
        std::cout << client_name << ": Opened connection" << std::endl;
        std::cout << client_name << ": Sending message: " << message << std::endl;
#endif
        connection->send(message);
      };
    }

#ifdef DEBUG
    if (!client->on_message)
    {
      client->on_message = [client_name](std::shared_ptr<WsClient::Connection> /*connection*/, std::shared_ptr<WsClient::InMessage> in_message) {
        std::cout << client_name << ": Message received: " << in_message->string() << std::endl;
      };
    }

    if (!client->on_close)
    {
      client->on_close = [client_name](std::shared_ptr<WsClient::Connection> /*connection*/, int status, const std::string & /*reason*/) {
        std::cout << client_name << ": Closed connection with status code " << status << std::endl;
      };
    }

    if (!client->on_error)
    {
      // See http://www.boost.org/doc/libs/1_55_0/doc/html/boost_asio/reference.html, Error Codes for error code meanings
      client->on_error = [client_name](std::shared_ptr<WsClient::Connection> /*connection*/, const SimpleWeb::error_code &ec) {
        std::cout << client_name << ": Error: " << ec << ", error message: " << ec.message() << std::endl;
      };
    }
#endif

#ifdef DEBUG
    std::thread client_thread([client_name, client]() {
#else
    std::thread client_thread([client]() {
#endif
      client->start();

#ifdef DEBUG
      std::cout << client_name << ": Terminated" << std::endl;
#endif
      client->on_open = NULL;
      client->on_message = NULL;
      client->on_close = NULL;
      client->on_error = NULL;
    });

    client_thread.detach();
  }

public:
  RosbridgeWsClient(const std::string& server_port_path)
  {
    this->server_port_path = server_port_path;
  }

  ~RosbridgeWsClient()
  {
    std::lock_guard<std::mutex> lk(map_mutex); // neccessary?
    for (auto& client : client_map)
    {
      client.second->stop();
      client.second.reset();
    }
  }

 // The execution can take up to 100 ms!
 bool connected(){

    auto p = std::make_shared<std::promise<void>>();
    auto future = p->get_future();

    auto callback = [](std::shared_ptr<WsClient::Connection> connection, std::shared_ptr<std::promise<void>> p) {
        p->set_value();
        connection->send_close(1000);
    };
    std::shared_ptr<WsClient> status_client = std::make_shared<WsClient>(server_port_path);
    status_client->on_open = std::bind(callback, std::placeholders::_1, p);

    std::async([status_client]{

        status_client->start();
        status_client->on_open = NULL;
        status_client->on_message = NULL;
        status_client->on_close = NULL;
        status_client->on_error = NULL;

    });

    auto status = future.wait_for(std::chrono::milliseconds(100));
    return status == std::future_status::ready;
 }

  // Adds a client to the client_map.
  void addClient(const std::string& client_name)
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it == client_map.end())
    {
      client_map[client_name] = std::make_shared<WsClient>(server_port_path);
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << " has already been created" << std::endl;
    }
#endif
  }

  std::shared_ptr<WsClient> getClient(const std::string& client_name)
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      return it->second;
    }
    return NULL;
  }

  void stopClient(const std::string& client_name)
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      it->second->stop();
      it->second->on_open = NULL;
      it->second->on_message = NULL;
      it->second->on_close = NULL;
      it->second->on_error = NULL;
#ifdef DEBUG
      std::cout << client_name << " has been stopped" << std::endl;
#endif
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << " has not been created" << std::endl;
    }
#endif
  }

  void removeClient(const std::string& client_name)
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      // Stop the client asynchronously in 100 ms.
      // This is to ensure, that all threads involving the client have been launched.
      std::thread t([](std::shared_ptr<WsClient> client){
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         client->stop();
         client.reset();
      },
      it->second /*lambda param*/ );
      client_map.erase(it);
      t.detach();
#ifdef DEBUG
      std::cout << client_name << " has been removed" << std::endl;
#endif
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << " has not been created" << std::endl;
    }
#endif
  }

  // Gets the client from client_map and starts it. Advertising is essentially sending a message.
  // One client per topic!
  void advertise(const std::string& client_name, const std::string& topic, const std::string& type, const std::string& id = "")
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      std::string message = "\"op\":\"advertise\", \"topic\":\"" + topic + "\", \"type\":\"" + type + "\"";

      if (id.compare("") != 0)
      {
        message += ", \"id\":\"" + id + "\"";
      }
      message = "{" + message + "}";

      start(client_name, it->second, message);
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << "has not been created" << std::endl;
    }
#endif
  }

  void publish(const std::string& topic, const rapidjson::Document& msg, const std::string& id = "")
  {
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    msg.Accept(writer);

    std::string message = "\"op\":\"publish\", \"topic\":\"" + topic + "\", \"msg\":" + strbuf.GetString();

    if (id.compare("") != 0)
    {
      message += ", \"id\":\"" + id + "\"";
    }
    message = "{" + message + "}";

    std::shared_ptr<WsClient> publish_client = std::make_shared<WsClient>(server_port_path);

    publish_client->on_open = [message](std::shared_ptr<WsClient::Connection> connection) {
#ifdef DEBUG
      std::cout << "publish_client: Opened connection" << std::endl;
      std::cout << "publish_client: Sending message: " << message << std::endl;
#endif
      connection->send(message);

      // TODO: This could be improved by creating a thread to keep publishing the message instead of closing it right away
      connection->send_close(1000);
    };

    start("publish_client", publish_client, message);
  }

  void subscribe(const std::string& client_name, const std::string& topic, const InMessage& callback, const std::string& id = "", const std::string& type = "", int throttle_rate = -1, int queue_length = -1, int fragment_size = -1, const std::string& compression = "")
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      std::string message = "\"op\":\"subscribe\", \"topic\":\"" + topic + "\"";

      if (id.compare("") != 0)
      {
        message += ", \"id\":\"" + id + "\"";
      }
      if (type.compare("") != 0)
      {
        message += ", \"type\":\"" + type + "\"";
      }
      if (throttle_rate > -1)
      {
        message += ", \"throttle_rate\":" + std::to_string(throttle_rate);
      }
      if (queue_length > -1)
      {
        message += ", \"queue_length\":" + std::to_string(queue_length);
      }
      if (fragment_size > -1)
      {
        message += ", \"fragment_size\":" + std::to_string(fragment_size);
      }
      if (compression.compare("none") == 0 || compression.compare("png") == 0)
      {
        message += ", \"compression\":\"" + compression + "\"";
      }
      message = "{" + message + "}";

      it->second->on_message = callback;
      start(client_name, it->second, message);
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << "has not been created" << std::endl;
    }
#endif
  }

  void advertiseService(const std::string& client_name, const std::string& service, const std::string& type, const InMessage& callback)
  {
    std::lock_guard<std::mutex> lk(map_mutex);
    std::unordered_map<std::string, std::shared_ptr<WsClient>>::iterator it = client_map.find(client_name);
    if (it != client_map.end())
    {
      std::string message = "{\"op\":\"advertise_service\", \"service\":\"" + service + "\", \"type\":\"" + type + "\"}";

      it->second->on_message = callback;
      start(client_name, it->second, message);
    }
#ifdef DEBUG
    else
    {
      std::cerr << client_name << "has not been created" << std::endl;
    }
#endif
  }

  void serviceResponse(const std::string& service, const std::string& id, bool result, const rapidjson::Document& values = {})
  {
    std::string message = "\"op\":\"service_response\", \"service\":\"" + service + "\", \"result\":" + ((result)? "true" : "false");

    // Rosbridge somehow does not allow service_response to be sent without id and values
    // , so we cannot omit them even though the documentation says they are optional.
    message += ", \"id\":\"" + id + "\"";

    // Convert JSON document to string
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    values.Accept(writer);

    message += ", \"values\":" + std::string(strbuf.GetString());
    message = "{" + message + "}";

    std::shared_ptr<WsClient> service_response_client = std::make_shared<WsClient>(server_port_path);

    service_response_client->on_open = [message](std::shared_ptr<WsClient::Connection> connection) {
#ifdef DEBUG
      std::cout << "service_response_client: Opened connection" << std::endl;
      std::cout << "service_response_client: Sending message: " << message << std::endl;
#endif
      connection->send(message);

      connection->send_close(1000);
    };

    start("service_response_client", service_response_client, message);
  }

  void callService(const std::string& service, const InMessage& callback, const rapidjson::Document& args = {}, const std::string& id = "", int fragment_size = -1, const std::string& compression = "")
  {
    std::string message = "\"op\":\"call_service\", \"service\":\"" + service + "\"";

    if (!args.IsNull())
    {
      rapidjson::StringBuffer strbuf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
      args.Accept(writer);

      message += ", \"args\":" + std::string(strbuf.GetString());
    }
    if (id.compare("") != 0)
    {
      message += ", \"id\":\"" + id + "\"";
    }
    if (fragment_size > -1)
    {
      message += ", \"fragment_size\":" + std::to_string(fragment_size);
    }
    if (compression.compare("none") == 0 || compression.compare("png") == 0)
    {
      message += ", \"compression\":\"" + compression + "\"";
    }
    message = "{" + message + "}";

    std::shared_ptr<WsClient> call_service_client = std::make_shared<WsClient>(server_port_path);

    if (callback)
    {
      call_service_client->on_message = callback;
    }
    else
    {
      call_service_client->on_message = [](std::shared_ptr<WsClient::Connection> connection, std::shared_ptr<WsClient::InMessage> in_message) {
#ifdef DEBUG
        std::cout << "call_service_client: Message received: " << in_message->string() << std::endl;
        std::cout << "call_service_client: Sending close connection" << std::endl;
#endif
        connection->send_close(1000);
      };
    }

    start("call_service_client", call_service_client, message);
  }

  //!
  //! \brief Checks if the service \p service is available.
  //! \param service Service name.
  //! \return Returns true if the service is available, false either.
  //! \note Don't call this function to frequently. Use \fn waitForService() instead.
  //!
  bool serviceAvailable(const std::string& service)
  {
    const rapidjson::Document args = {};
    const std::string& id = "";
    const int fragment_size = -1;
    const std::string& compression = "";
    std::string message = "\"op\":\"call_service\", \"service\":\"" + service + "\"";
    std::string client_name("wait_for_service_client");

    if (!args.IsNull())
    {
      rapidjson::StringBuffer strbuf;
      rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
      args.Accept(writer);

      message += ", \"args\":" + std::string(strbuf.GetString());
    }
    if (id.compare("") != 0)
    {
      message += ", \"id\":\"" + id + "\"";
    }
    if (fragment_size > -1)
    {
      message += ", \"fragment_size\":" + std::to_string(fragment_size);
    }
    if (compression.compare("none") == 0 || compression.compare("png") == 0)
    {
      message += ", \"compression\":\"" + compression + "\"";
    }
    message = "{" + message + "}";

    std::shared_ptr<WsClient> wait_for_service_client = std::make_shared<WsClient>(server_port_path);
    std::shared_ptr<std::promise<bool>> p(std::make_shared<std::promise<bool>>());
    auto future = p->get_future();
#ifdef DEBUG
    wait_for_service_client->on_message = [p, service, client_name](
#else
    wait_for_service_client->on_message = [p](
#endif
            std::shared_ptr<WsClient::Connection> connection,
            std::shared_ptr<WsClient::InMessage> in_message){
#ifdef DEBUG
#endif
        rapidjson::Document doc;
        doc.Parse(in_message->string().c_str());
        if ( !doc.HasParseError()
             && doc.HasMember("result")
             && doc["result"].IsBool()
             && doc["result"] == true )
        {
#ifdef DEBUG
            std::cout << client_name << ": "
                      << "service " << service
                      << " available." << std::endl;
            std::cout << client_name << ": "
                      << "message: " << in_message->string()
                      << std::endl;
#endif
            p->set_value(true);
        } else {
            p->set_value(false);
        }
        connection->send_close(1000);
    };
    start(client_name, wait_for_service_client, message);
    return future.get();
  }

  //!
  //! \brief Waits until the service with the name \p service is available.
  //! \param service Service name.
  //! @note This function will block as long as the service is not available.
  //!
  void waitForService(const std::string& service)
  {


#ifdef DEBUG
    auto s = std::chrono::high_resolution_clock::now();
    long counter = 0;
#endif
    while(true)
    {
#ifdef DEBUG
        ++counter;
#endif
        if (serviceAvailable(service)){
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Avoid excessive polling.
    };
#ifdef DEBUG
    auto e = std::chrono::high_resolution_clock::now();
    std::cout << "waitForService(): time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(e-s).count()
              << " ms." << std::endl;
    std::cout << "waitForService(): clients launched: "
              << counter << std::endl;
#endif
  }
};

#endif
