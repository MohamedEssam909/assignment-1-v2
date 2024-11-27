#include <iostream>
#include <vector>
#include <thread>
#include <map>
#include <set>
#include <queue>
#include <cstring>
#include <string>
#include <functional>
#include <chrono>
#include <mutex>
#include <cstdlib> // For rand()

#define MAX_PKT 16
#define MAX_SEQ 7

using namespace std;

typedef unsigned int seq_nr;

typedef struct {
    char data[MAX_PKT];
} packet;

typedef enum { DATA, ACK, NAK } frame_kind;

typedef struct {
    frame_kind kind;
    seq_nr seq;
    seq_nr ack;
    packet info;
    uint32_t crc;  // Actual CRC field
} frame;

#define inc(k) (k = (k + 1) % (MAX_SEQ + 1))

// Physical Layer
class Physical_Layer {
private:
    std::vector<frame> channel;
    std::mutex channel_mutex;

public:
    void send_frame(frame f) {
        std::lock_guard<std::mutex> lock(channel_mutex);

        // Simulate packet loss
       // if (rand() % 10 < 2) return; // 20% chance of packet loss

        // Simulate corruption
       // if (rand() % 10 < 1) f.crc_valid = false;


        // Simulate corruption by flipping some bits in the data field
        if (rand() % 10 < 1) { // 10% chance of corruption
            f.info.data[0] ^= 0xFF; // Flip the first byte of the data
        }


        // Simulate delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        channel.push_back(f);
    }

    bool receive_frame(frame& f) {
        std::lock_guard<std::mutex> lock(channel_mutex);

        if (!channel.empty()) {
            f = channel.front();
            channel.erase(channel.begin());
            return true;
        }
        return false;
    }
};

Physical_Layer channel;

class TimerManager {
private:
    std::map<seq_nr, std::thread> timers;
    std::map<seq_nr, bool> activeTimers;
    std::mutex timers_mutex;

public:
    void start_timer(seq_nr seq, int duration_ms, std::function<void()> timeout_handler) {
        stop_timer(seq);
        std::thread timer_thread([this, seq, duration_ms, timeout_handler]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
            std::lock_guard<std::mutex> lock(timers_mutex);
            if (activeTimers[seq]) {
                timeout_handler();
                activeTimers[seq] = false;
            }
            });
        timers[seq] = std::move(timer_thread);
        activeTimers[seq] = true;
    }

    void stop_timer(seq_nr seq) {
        std::lock_guard<std::mutex> lock(timers_mutex);
        if (activeTimers[seq] && timers[seq].joinable()) {
            timers[seq].detach();
            activeTimers[seq] = false;
        }
        timers.erase(seq);
    }

    ~TimerManager() {
        for (auto& pair : timers) {
            if (pair.second.joinable()) {
                pair.second.detach();
            }
        }
    }
};

TimerManager sender_timer;




// CRC calculation function (using a simple polynomial)
uint32_t calculate_crc(const char* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF; // Initialize CRC to all 1's
    const uint32_t polynomial = 0xEDB88320; // CRC-32 polynomial

    for (size_t i = 0; i < length; i++) {
        crc ^= static_cast<uint8_t>(data[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ polynomial;
            else
                crc >>= 1;
        }
    }
    return ~crc; // Return the bitwise inverse of the CRC
}


class DataLink_Layer {
private:
    seq_nr ack_expected;
    seq_nr next_frame_to_send;
    seq_nr frame_expected;
    frame r;
    packet buffer[MAX_SEQ + 1];
    std::set<seq_nr> received_frames;
    std::priority_queue<seq_nr, std::vector<seq_nr>, std::greater<>> receiver_buffer;
    bool network_layer_enabled;
    vector<string> dynamic_content = { "Hello", "World", "GO", "BACK", "N", "PROTOCOL", "COMPUTER", "NETWORK" };

public:




    DataLink_Layer()
        : ack_expected(0), next_frame_to_send(0), frame_expected(0), network_layer_enabled(true) {}





    void enable_network_layer() { network_layer_enabled = true; }
    void disable_network_layer() { network_layer_enabled = false; }




    void send_data_frame(seq_nr frame_nr, const std::string& content) {
        frame s;
        s.kind = DATA;
        s.seq = frame_nr;
        s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

        // Populate the packet with the provided content
        memset(s.info.data, 0, sizeof(s.info.data));
        strncpy_s(s.info.data, sizeof(s.info.data), content.c_str(), _TRUNCATE);

        // Calculate and append CRC
        s.crc = calculate_crc(s.info.data, MAX_PKT);

        // Send the frame via the physical layer
        channel.send_frame(s);

        // Start the timer for the frame
        sender_timer.start_timer(frame_nr, 2000, [this, frame_nr, content]() {
            std::cout << "Sender: Timeout Resending frame " << frame_nr << std::endl;
            send_data_frame(frame_nr, content);
            });

        std::cout << "Sender: Sent frame " << frame_nr << " with content: " << content << ", CRC: " << s.crc << std::endl;
    }





    void to_network_layer(const packet& p) {
        std::cout << "Receiver: Delivered to network layer => " << p.data << std::endl;
        std::cout << std::endl;
    }






    void handle_event_sender() {
        if (channel.receive_frame(r)) {
            if (r.kind == DATA) {  // Check if it's a data frame
                if (r.ack == ack_expected) {  // Check piggybacked ACK
                    sender_timer.stop_timer(ack_expected);
                    std::cout << "Sender: Piggybacked ACK received for frame " << ack_expected << ", Timer Stopped" << std::endl;
                    inc(ack_expected);
                }
            }
            else if (r.kind == NAK) {
                std::cout << "Sender: NAK received for frame " << r.seq << ". Retransmitting.\n";
                send_data_frame(r.seq, dynamic_content[r.seq]);
            }
        }
    }

    void handle_event_receiver() {
        if (channel.receive_frame(r)) {
            if (r.kind == DATA) {
                uint32_t calculated_crc = calculate_crc(r.info.data, MAX_PKT);
                if (calculated_crc != r.crc) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    std::cout << "Receiver: Corrupted frame " << r.seq << " ,CRC bits error : " << calculated_crc << " frame discarded, Sending NAK\n";
                    frame nak_frame = { NAK, r.seq, 0 };
                    channel.send_frame(nak_frame);
                }
                else if (received_frames.count(r.seq) == 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    std::cout << "Receiver: Frame " << r.seq << " received. Piggybacking ACK.\n";
                    received_frames.insert(r.seq);
                    receiver_buffer.push(r.seq);

                    buffer[r.seq] = r.info;

                    frame piggyback_frame = { DATA, 0, r.seq };
                    channel.send_frame(piggyback_frame);

                    while (!receiver_buffer.empty() && receiver_buffer.top() == frame_expected) {
                        to_network_layer(buffer[receiver_buffer.top()]);
                        receiver_buffer.pop();
                        inc(frame_expected);
                    }
                }
                else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    std::cout << "Receiver: Duplicate frame " << r.seq << " discarded. Piggybacking ACK.\n";
                    frame piggyback_frame = { DATA, 0, r.seq };
                    channel.send_frame(piggyback_frame);
                }
            }
        }
    }





    void run_sender() {
        for (int i = 0; i < dynamic_content.size(); ++i) {
            send_data_frame(next_frame_to_send, dynamic_content[i]);
            inc(next_frame_to_send);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            handle_event_sender();
        }
    }

    void run_receiver() {
        while (true) {
            handle_event_receiver();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }




};

void simulate() {
    DataLink_Layer sender, receiver;

    std::thread sender_thread([&]() { sender.run_sender(); });
    std::thread receiver_thread([&]() { receiver.run_receiver(); });

    sender_thread.join();
    receiver_thread.join();
}

int main() {
    simulate();
    return 0;
}
