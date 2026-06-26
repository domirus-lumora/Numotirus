// core/p2p/libjuice_wrapper.cpp
// libjuice wrapper implementation. libjuice 封装实现。

#include "libjuice_wrapper.hpp"
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace numotirus {
namespace nat {

static void OnStateChanged(juice_agent_t* agent, juice_state_t state, void* user_data) {
    auto* self = static_cast<IceAgent*>(user_data);
    if (self && self->state_cb_) {
        self->state_cb_(static_cast<IceState>(state));
    }
}

static void OnCandidate(juice_agent_t* agent, const char* sdp, void* user_data) {
    auto* self = static_cast<IceAgent*>(user_data);
    if (self && self->candidate_cb_) {
        self->candidate_cb_(sdp ? sdp : "");
    }
}

static void OnGatheringDone(juice_agent_t* agent, void* user_data) {
    // Gathering complete. 收集完成。
}

static void OnData(juice_agent_t* agent, const char* data, size_t len, void* user_data) {
    auto* self = static_cast<IceAgent*>(user_data);
    if (self && self->data_cb_) {
        self->data_cb_(reinterpret_cast<const uint8_t*>(data), len);
    }
}

IceAgent::IceAgent() : agent_(nullptr) {}

IceAgent::~IceAgent() {
    if (agent_) {
        juice_destroy(agent_);
        agent_ = nullptr;
    }
}

// Initialize ICE agent with STUN server. 用 STUN 服务器初始化 ICE 代理。
bool IceAgent::Initialize(uint16_t local_port,
                          const std::string& stun_server,
                          IceStateCallback state_cb,
                          IceCandidateCallback candidate_cb,
                          IceDataCallback data_cb) {
    state_cb_ = state_cb;
    candidate_cb_ = candidate_cb;
    data_cb_ = data_cb;

    std::string stun_host = "stun.l.google.com";
    uint16_t stun_port = 19302;
    if (!stun_server.empty()) {
        size_t colon = stun_server.find(':');
        if (colon != std::string::npos) {
            stun_host = stun_server.substr(0, colon);
            stun_port = static_cast<uint16_t>(std::atoi(stun_server.substr(colon + 1).c_str()));
        } else {
            stun_host = stun_server;
        }
    }

    juice_config_t config;
    std::memset(&config, 0, sizeof(config));
    config.stun_server_host = stun_host.c_str();
    config.stun_server_port = stun_port;

    agent_ = juice_create(&config);
    if (!agent_) {
        return false;
    }

    return true;
}

// Start gathering ICE candidates. 开始收集 ICE 候选地址。
bool IceAgent::GatherCandidates() {
    if (!agent_) return false;
    return juice_gather_candidates(agent_) == 0;
}

// Get local SDP description. 获取本地 SDP 描述。
std::string IceAgent::GetLocalDescription() {
    if (!agent_) return "";
    char buffer[4096];
    if (juice_get_local_description(agent_, buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    return "";
}

// Set remote SDP description. 设置远程 SDP 描述。
bool IceAgent::SetRemoteDescription(const std::string& sdp) {
    if (!agent_) return false;
    return juice_set_remote_description(agent_, sdp.c_str()) == 0;
}

// Add a single remote candidate. 添加单个远程候选地址。
bool IceAgent::AddRemoteCandidate(const std::string& sdp) {
    if (!agent_) return false;
    return juice_add_remote_candidate(agent_, sdp.c_str()) == 0;
}

// Send data through ICE. 通过 ICE 发送数据。
bool IceAgent::SendData(const uint8_t* data, size_t len) {
    if (!agent_) return false;
    return juice_send(agent_, reinterpret_cast<const char*>(data), len) == 0;
}

// Get current ICE state. 获取当前 ICE 状态。
IceState IceAgent::GetState() const {
    if (!agent_) return IceState::kDisconnected;
    return static_cast<IceState>(juice_get_state(agent_));
}

// Get selected candidate addresses. 获取选中的候选地址。
bool IceAgent::GetSelectedAddresses(std::string& local_ip, uint16_t& local_port,
                                    std::string& remote_ip, uint16_t& remote_port) {
    if (!agent_) return false;

    char local_buf[128];
    char remote_buf[128];

    int ret = juice_get_selected_addresses(
        agent_,
        local_buf, sizeof(local_buf),
        remote_buf, sizeof(remote_buf)
    );

    if (ret == 0) {
        local_ip = local_buf;
        remote_ip = remote_buf;
        local_port = 0;
        remote_port = 0;
        return true;
    }
    return false;
}

} // namespace nat
} // namespace numotirus