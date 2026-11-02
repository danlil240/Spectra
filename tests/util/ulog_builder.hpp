#pragma once

// ULogBuilder — writes a small ULog binary in memory for tests (header,
// FORMAT/INFO/PARAMETER/ADD_LOGGED_MSG/DATA/LOGGING/DROPOUT messages).
// Shared by the ULog parser tests and the LogReader tests.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

class ULogBuilder
{
   public:
    ULogBuilder()
    {
        // Write ULog header: magic (7 bytes) + version (1 byte) + timestamp (8 bytes).
        static constexpr uint8_t magic[] = {0x55, 0x4C, 0x6F, 0x67, 0x01, 0x12, 0x35};
        buf_.insert(buf_.end(), magic, magic + sizeof(magic));
        buf_.push_back(1);   // version
        write_u64(1000);     // timestamp_us = 1000
    }

    // Add a FORMAT definition message.
    void add_format(const std::string& content)
    {
        add_message('F',
                    reinterpret_cast<const uint8_t*>(content.data()),
                    static_cast<uint16_t>(content.size()));
    }

    // Add an INFO message.
    void add_info(const std::string& type_and_key, const uint8_t* val, uint16_t val_len)
    {
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>(type_and_key.size()));
        payload.insert(payload.end(), type_and_key.begin(), type_and_key.end());
        payload.insert(payload.end(), val, val + val_len);
        add_message('I', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a string INFO message.
    void add_info_string(const std::string& key, const std::string& value)
    {
        std::string type_key = "char[" + std::to_string(value.size()) + "] " + key;
        add_info(type_key,
                 reinterpret_cast<const uint8_t*>(value.data()),
                 static_cast<uint16_t>(value.size()));
    }

    // Add a PARAMETER message.
    void add_param_float(const std::string& name, float value)
    {
        std::string          type_key = "float " + name;
        std::vector<uint8_t> payload;
        payload.push_back(static_cast<uint8_t>(type_key.size()));
        payload.insert(payload.end(), type_key.begin(), type_key.end());
        uint8_t val_bytes[4];
        std::memcpy(val_bytes, &value, 4);
        payload.insert(payload.end(), val_bytes, val_bytes + 4);
        add_message('P', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add an ADD_LOGGED_MSG (subscription) message.
    void add_subscription(uint8_t multi_id, uint16_t msg_id, const std::string& name)
    {
        std::vector<uint8_t> payload;
        payload.push_back(multi_id);
        uint8_t id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        payload.insert(payload.end(), name.begin(), name.end());
        add_message('A', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a DATA message.
    void add_data(uint16_t msg_id, uint64_t timestamp, const uint8_t* fields, size_t field_len)
    {
        std::vector<uint8_t> payload;
        uint8_t              id_bytes[2];
        std::memcpy(id_bytes, &msg_id, 2);
        payload.insert(payload.end(), id_bytes, id_bytes + 2);
        uint8_t ts_bytes[8];
        std::memcpy(ts_bytes, &timestamp, 8);
        payload.insert(payload.end(), ts_bytes, ts_bytes + 8);
        payload.insert(payload.end(), fields, fields + field_len);
        add_message('D', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a LOGGING message.
    void add_log_message(uint8_t level, uint64_t timestamp, const std::string& text)
    {
        std::vector<uint8_t> payload;
        payload.push_back(level);
        uint8_t ts_bytes[8];
        std::memcpy(ts_bytes, &timestamp, 8);
        payload.insert(payload.end(), ts_bytes, ts_bytes + 8);
        payload.insert(payload.end(), text.begin(), text.end());
        add_message('L', payload.data(), static_cast<uint16_t>(payload.size()));
    }

    // Add a DROPOUT message.
    void add_dropout(uint16_t duration_ms)
    {
        uint8_t payload[2];
        std::memcpy(payload, &duration_ms, 2);
        add_message('O', payload, 2);
    }

    // Write to file and return path.
    std::string write_to_file(const std::string& path) const
    {
        std::ofstream ofs(path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buf_.data()),
                  static_cast<std::streamsize>(buf_.size()));
        ofs.close();
        return path;
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }

   private:
    void add_message(uint8_t type, const uint8_t* payload, uint16_t len)
    {
        uint8_t hdr[3];
        std::memcpy(hdr, &len, 2);
        hdr[2] = type;
        buf_.insert(buf_.end(), hdr, hdr + 3);
        buf_.insert(buf_.end(), payload, payload + len);
    }

    void write_u64(uint64_t v)
    {
        uint8_t bytes[8];
        std::memcpy(bytes, &v, 8);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }

    std::vector<uint8_t> buf_;
};
