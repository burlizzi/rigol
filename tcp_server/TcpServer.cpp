#include "esphome/components/network/util.h"
#include "esphome/core/log.h"
#include "TcpServer.h"

#define S(X) X, sizeof(X) -1
namespace esphome
{
  namespace tcp_server
  {

    void TcpServer::setup()
    {
      socket_ = socket::socket_ip(SOCK_STREAM, 0);
      if (socket_ == nullptr)
      {
        ESP_LOGW(TAG, "Could not create socket.");
        this->mark_failed();
        return;
      }
      int enable = 1;
      int err = socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
      if (err != 0)
      {
        ESP_LOGW(TAG, "Socket unable to set reuseaddr: errno %d", err);
        // we can still continue
      }
      err = socket_->setblocking(false);
      if (err != 0)
      {
        ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
        this->mark_failed();
        return;
      }

      struct sockaddr_storage server;

      socklen_t sl = socket::set_sockaddr_any((struct sockaddr *)&server, sizeof(server), this->port_);
      if (sl == 0)
      {
        ESP_LOGW(TAG, "Socket unable to set sockaddr: errno %d", errno);
        this->mark_failed();
        return;
      }

      err = socket_->bind((struct sockaddr *)&server, sl);
      if (err != 0)
      {
        ESP_LOGW(TAG, "Socket unable to bind: errno %d", errno);
        this->mark_failed();
        return;
      }

      err = socket_->listen(4);
      if (err != 0)
      {
        ESP_LOGW(TAG, "Socket unable to listen: errno %d", errno);
        this->mark_failed();
        return;
      }
    }

    void TcpServer::loop()
    {
      while (true)
      {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        auto sock = socket_->accept((struct sockaddr *)&source_addr, &addr_len);
        if (!sock)
          break;
        ESP_LOGD(TAG, "Accepted %s", sock->getpeername().c_str());
        client_ = std::move(sock);
        int err = client_->setblocking(false);
        if (err != 0) {
          ESP_LOGW(TAG, "Socket unable to set nonblocking mode: errno %d", err);
          client_->close();
          client_ = nullptr;
          this->mark_failed();
          return;
        }
      
      }
      if (!network::is_connected())
      {
        // when network is disconnected force disconnect immediately
        // don't wait for timeout
        return;
      }
      
      if(client_)
      {
        char data;
        int len = 0;
        while((len=client_->read(&data, 1))>0)
        {
          if (data == '\n')
          {
            auto resp=parse(ss_.str());
            client_->write(resp.c_str(), resp.length());
            ss_.str(std::string()); // clear the stringstream
            ss_.clear(); // clear the error state
          }
          else
            ss_<<data;
        }
        if (len==0)
        {
          ESP_LOGW(TAG, "Connection closed");
          client_->close();
          client_ = nullptr;
          return;
        }
        if (len == -1)
        {
          if (errno == EWOULDBLOCK || errno == EAGAIN)
          {
            return;
          }
          ESP_LOGW(TAG, "Socket read failed with errno %d", errno);
          client_->close();
          client_ = nullptr;
          return;
        }
      }
    }

    void TcpServer::dump_config()
    {
      ESP_LOGCONFIG(TAG, "TcpServer: ");
    }

    constexpr unsigned int str2int(const char* str, int h = 0)
    {
        return !str[h] ? 5381 : (str2int(str, h+1) * 33) ^ str[h];
    }

    std::string TcpServer::parse(const std::string &data)
    {
      ESP_LOGD(TAG, "received: %s", data.c_str());
    }

  }
}