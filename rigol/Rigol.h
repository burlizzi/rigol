#pragma once
#include <sstream>
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"

namespace esphome
{
    namespace rigol
    {
        static const char *const TAG = "Rigol";
        class Rigol : public Component
        {
        public:
            void setup() override;
            float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }
            void loop() override;
            void dump_config() override;
            static std::string parse(const std::string &data);
        protected:
            static void tcp_task(void *params);

            std::unique_ptr<socket::Socket> socket_ = nullptr;
            uint16_t port_{5555};
        };
    }
}