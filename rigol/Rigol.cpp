#include "esphome/components/network/util.h"
#include "esphome/core/log.h"
#include "Rigol.h"
#include "esp_adc/adc_continuous.h"

#define S(X) X, sizeof(X) - 1

namespace esphome
{
  namespace rigol
  {

    adc_continuous_handle_t adc_handle = NULL;

    TaskHandle_t cb_task = NULL;
    static bool IRAM_ATTR callback(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
    {
      BaseType_t mustYield = pdFALSE;
      vTaskNotifyGiveFromISR(cb_task, &mustYield);
      return (mustYield == pdTRUE);
    }

    void ADC_Init(adc_channel_t *channels, uint8_t numChannels)
    {
      // handle configuration
      adc_continuous_handle_cfg_t handle_config = {
          .max_store_buf_size = 2800,
          .conv_frame_size = 1400,
      };
      ESP_ERROR_CHECK(adc_continuous_new_handle(&handle_config, &adc_handle));

      // ADC Configuration with Channels
      adc_continuous_config_t adc_cnfig = {
          .pattern_num = numChannels,
          .sample_freq_hz = 20 * 1000,
          .conv_mode = ADC_CONV_SINGLE_UNIT_1,
          .format = ADC_DIGI_OUTPUT_FORMAT_TYPE1,
      };
      adc_digi_pattern_config_t channel_config[2];
      for (int i = 0; i < numChannels; i++)
      {
        channel_config[i].channel = channels[i];
        channel_config[i].atten = ADC_ATTEN_DB_12;
        channel_config[i].bit_width = ADC_BITWIDTH_12;
        channel_config[i].unit = ADC_UNIT_1;
      }
      adc_cnfig.adc_pattern = channel_config;
      ESP_ERROR_CHECK(adc_continuous_config(adc_handle, &adc_cnfig));

      // Callback Configuration
      adc_continuous_evt_cbs_t cb_config = {
          .on_conv_done = callback,
      };
      ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_handle, &cb_config, NULL));
    }
    int ADC_DATA[2];
    int voltage[2];

    void cbTask(void *parameters)
    {
      uint8_t buf[30];
      uint32_t rxLen = 0;
      for (;;)
      {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        adc_continuous_read(adc_handle, buf, 12, &rxLen, 0);
        ESP_LOGI(TAG, "ADC Read %d", rxLen);
        for (int i = 0; i < rxLen; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
          adc_digi_output_data_t *p = (adc_digi_output_data_t *)&buf[i];
          uint16_t channel = p->type1.channel;
          uint16_t data = p->type1.data;
          if (channel == ADC_CHANNEL_3)
            ADC_DATA[0] = data;
          if (channel == ADC_CHANNEL_3)
            ADC_DATA[1] = data;
        }
      }
    }

    void Rigol::setup()
    {

      ESP_LOGCONFIG(TAG, "Setting up Rigol...");
      xTaskCreate(cbTask, "Callback Task", 4096, NULL, 0, &cb_task);

      adc_channel_t channels[] = {ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7,
                                  ADC_CHANNEL_8, ADC_CHANNEL_9};
      // ADC_CHANNEL_0, ADC_CHANNEL_1, ADC_CHANNEL_2, ADC_CHANNEL_3, ADC_CHANNEL_4, ADC_CHANNEL_5, ADC_CHANNEL_6, ADC_CHANNEL_7
      ADC_Init(channels, sizeof(channels) / sizeof(adc_channel_t)); 
      ESP_LOGCONFIG(TAG, "ADC Initialized...");

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
    std::stringstream ss;

    void Rigol::tcp_task(void *param)
    {
      std::unique_ptr<socket::Socket> client_(static_cast<socket::Socket *>(param));
      char data;
      int len = 0;
      while ((len = client_->read(&data, 1)) > 0)
      {
        if (data == '\n')
        {
          auto resp = parse(ss.str());
          client_->write(resp.c_str(), resp.length());
          ss.str(std::string()); // clear the stringstream
          ss.clear();            // clear the error state
        }
        else
          ss << data;
      }
      client_->close();
      ESP_LOGD(TAG, "Client disconnected %s", client_->getpeername().c_str());
      vTaskDelete(NULL);
    }

    void Rigol::loop()
    {
      if (!network::is_connected())
      {
        // when network is disconnected force disconnect immediately
        // don't wait for timeout
        return;
      }
      while (true)
      {
        struct sockaddr_storage source_addr;
        socklen_t addr_len = sizeof(source_addr);
        auto sock = socket_->accept((struct sockaddr *)&source_addr, &addr_len).release();
        int enable = 1;
        if (!sock)
          break;

        int err = sock->setsockopt(IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(int));
        if (err != 0)
        {
          ESP_LOGW(TAG, "Socket could not enable tcp nodelay, errno: %d", errno);
          return;
        }
        ESP_LOGW(TAG, "Socket could enable tcp nodelay, errno: %d", enable);

#ifndef CONFIG_FREERTOS_UNICORE

        if (1)
        {
          xTaskCreatePinnedToCore(Rigol::tcp_task, "tcp_thread",
                                  10000,   // stack size (in words)
                                  sock,    // input params
                                  1,       // priority
                                  nullptr, // Handle, not needed
                                  1        // core
          );
        }
        else
        {
#endif
          xTaskCreate(Rigol::tcp_task, "tcp_thread",
                      10000,  // stack size (in words)
                      sock,   // input params
                      1,      // priority
                      nullptr // Handle, not needed
          );
#ifndef CONFIG_FREERTOS_UNICORE
        }
#endif

        ESP_LOGD(TAG, "Accepted %s", sock->getpeername().c_str());
      }
    }

    void Rigol::dump_config()
    {
      ESP_LOGCONFIG(TAG, "Rigol: ");
    }

    constexpr unsigned int str2int(const char *str, int h = 0)
    {
      return !str[h] ? 5381 : (str2int(str, h + 1) * 33) ^ str[h];
    }

    std::string Rigol::parse(const std::string &data)
    {
      static char block = 0;
      // ESP_LOGD(TAG, "received: %s", data.c_str());
      switch (str2int(data.c_str()))
      {
      case str2int("*IDN?"):
        return "Rigol Technologies,MSO2302A,DS1EXXXXXXXXXX,00.02.05.02.00";
      case str2int(":CHAN1:DISP?"):
        return "1";
      case str2int(":CHAN2:DISP?"):
        return "1";
      case str2int(":LA:STAT?"):
        return "1";
      case str2int(":LA:DIG0:DISP?"):
        return "1";
      case str2int(":LA:DIG1:DISP?"):
      case str2int(":LA:DIG2:DISP?"):
      case str2int(":LA:DIG3:DISP?"):
      case str2int(":LA:DIG4:DISP?"):
      case str2int(":LA:DIG5:DISP?"):
      case str2int(":LA:DIG6:DISP?"):
      case str2int(":LA:DIG7:DISP?"):
      case str2int(":LA:DIG8:DISP?"):
      case str2int(":LA:DIG9:DISP?"):
      case str2int(":LA:DIG10:DISP?"):
      case str2int(":LA:DIG11:DISP?"):
      case str2int(":LA:DIG12:DISP?"):
      case str2int(":LA:DIG13:DISP?"):
      case str2int(":LA:DIG14:DISP?"):
      case str2int(":LA:DIG15:DISP?"):
        return "1";
      case str2int(":TIM:SCAL?"):
        return "1e-6";
      case str2int(":CHAN1:PROB?"):
        return "1";
      case str2int(":CHAN2:PROB?"):
        return "1";
      case str2int(":TRIG:STAT?"):
        return block++ ? "AUTO" : "STOP";
      case str2int(":WAV:STAT?"):
        return "IDLE,1400\n";
      case str2int("*OPC?"):
        return "1";
      case str2int(":CHAN1:SCAL?"):
        return "1";
      case str2int(":CHAN2:SCAL?"):
        return "1";
      case str2int(":CHAN1:COUP?"):
      case str2int(":CHAN2:COUP?"):
        return "DC";
      case str2int(":WAV:YINC?"):
        return "1";
      case str2int(":WAV:YOR?"):
        return "0";
      case str2int(":WAV:YREF?"):
        return "127";
      case str2int(":RUN"):
        return "";

      case str2int(":TRIG:EDGE:SLOP?"):
        return "POS";

      case str2int(":WAV:DATA?"):
      {
        std::stringstream ss;
        for (int i = 0; i < 1400; i++)
        {
          ss << (char)(i); // 1V
        }
        return "#9000001400" + ss.str() + '\n';
      }
      default:
        ESP_LOGW(TAG, "Unknown command: %s", data.c_str());
        return data.back() == '?' ? "0" : "";
      }
    }

  }
}