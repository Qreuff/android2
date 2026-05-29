#pragma once
#include "location_data.h"
#include <zmq.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <fstream>
#include <iostream>

class ZMQServer {
public:
    ZMQServer();
    ~ZMQServer();
    
    void start();
    void stop();
    bool isRunning() const;

private:
    void serverLoop();
    void processMessage(const std::string& msg_str);
    
    zmq::context_t context_;
    zmq::socket_t socket_;
    std::thread server_thread_;
    std::atomic<bool> running_{true};
    std::string log_file_path_ = LOG_FILE;
};