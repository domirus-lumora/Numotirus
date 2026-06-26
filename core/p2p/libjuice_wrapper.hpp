// core/p2p/libjuice_wrapper.hpp
// C++ wrapper for libjuice (ICE library). libjuice (ICE 库) 的 C++ 封装。

#pragma once

#include <string>
#include <functional>
#include <cstdint>

extern "C" {
#include <juice/juice.h>
}

namespace numotirus {
namespace nat {

enum class IceState {
    kDisconnected = JUICE_STATE_DISCONNECTED,
    kGathering = JUICE_STATE_GATHERING,
    kConnecting = JUICE_STATE_CONNECTING,
    kConnected = JUICE_STATE_CONNECTED,
    kCompleted = JUICE_STATE_COMPLETED,
    kFailed = JUICE_STATE_FAILED,
};

using IceStateCallback = std::function<void(IceState state)>;
using IceCandidateCallback = std::function<void(const std::string& sdp)>;
using IceDataCallback = std::function<void(const uint8_t* data, size_t len)>;

class IceAgent {
public:
    IceAgent();
    ~IceAgent();

    bool Initialize(uint16_t local_port,
                    const std::string& stun_server,
                    IceStateCallback state_cb,
                    IceCandidateCallback candidate_cb,
                    IceDataCallback data_cb);

    bool GatherCandidates();
    std::string GetLocalDescription();
    bool SetRemoteDescription(const std::string& sdp);
    bool AddRemoteCandidate(const std::string& sdp);
    bool SendData(const uint8_t* data, size_t len);
    IceState GetState() const;
    bool GetSelectedAddresses(std::string& local_ip, uint16_t& local_port,
                              std::string& remote_ip, uint16_t& remote_port);

    // Callbacks are public so static C functions can access them.
    // 回调函数为 public，以便静态 C 函数访问。
    IceStateCallback state_cb_;
    IceCandidateCallback candidate_cb_;
    IceDataCallback data_cb_;

private:
    juice_agent_t* agent_;

    static void OnStateChanged(juice_agent_t* agent, juice_state_t state, void* user_data);
    static void OnCandidate(juice_agent_t* agent, const char* sdp, void* user_data);
    static void OnGatheringDone(juice_agent_t* agent, void* user_data);
    static void OnData(juice_agent_t* agent, const char* data, size_t len, void* user_data);
};

} // namespace nat
} // namespace numotirus