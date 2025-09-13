#pragma once
#include <sstream>
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"

namespace esphome
{
    namespace tcp_server
    {
        static const char *const TAG = "TCP_SERVER";
        class TcpServer : public Component
        {
        public:
            TcpServer(uint16_t port):port_(port){};
            void setup() override;
            float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
            void loop() override;
            void dump_config() override;
            virtual std::string parse(const std::string &data);
        protected:
            std::unique_ptr<socket::Socket> socket_ = nullptr;
            std::unique_ptr<socket::Socket> client_;
            std::stringstream ss_;
            uint16_t port_;
        };
    }
}