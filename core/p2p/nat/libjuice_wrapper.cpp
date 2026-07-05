// core/p2p/nat/libjuice_wrapper.cpp
// libjuice wrapper implementation.
// libjuice 封装实现。
// SPDX-License-Identifier: Apache-2.0

#include "libjuice_wrapper.hpp"
#include <cstring>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

namespace numotirus {
namespace nat {

// Static callback implementations (members of IceAgent).
// 静态回调实现（IceAgent 的成员）。

void IceAgent::OnStateChanged(juice_agent_t* agent, juice_state_t state, void* user_ptr) {
    (void)agent;
    auto* self = static_cast<IceAgent*>(user_ptr);
    if (self && self->state_cb_) {
        self->state_cb_(static_cast<IceState>(state));
    }
}

void IceAgent::OnCandidate(juice_agent_t* agent, const char* sdp, void* user_ptr) {
    (void)agent;
    auto* self = static_cast<IceAgent*>(user_ptr);
    if (self && self->candidate_cb_) {
        self->candidate_cb_(sdp ? sdp : "");
    }
}

void IceAgent::OnGatheringDone(juice_agent_t* agent, void* user_ptr) {
    (void)agent;
    (void)user_ptr;
    // Gathering complete. 收集完成。
}

void IceAgent::OnData(juice_agent_t* agent, const char* data, size_t size, void* user_ptr) {
    (void)agent;
    auto* self = static_cast<IceAgent*>(user_ptr);
    if (self && self->data_cb_) {
        self->data_cb_(reinterpret_cast<const uint8_t*>(data), size);
    }
}

IceAgent::IceAgent() : agent_(nullptr) {}

IceAgent::~IceAgent() {
    if (agent_) {
        juice_destroy(agent_);
        agent_ = nullptr;
    }
}

bool IceAgent::Initialize(uint16_t local_port,
                          const std::string& stun_server,
                          IceStateCallback state_cb,
                          IceCandidateCallback candidate_cb,
                          IceDataCallback data_cb) {
    // Store callbacks.
    // 存储回调。
    state_cb_ = state_cb;
    candidate_cb_ = candidate_cb;
    data_cb_ = data_cb;

    // Parse STUN server.
    // 解析 STUN 服务器。
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

    // STUN server configuration.
    // STUN 服务器配置。
    config.stun_server_host = stun_host.c_str();
    config.stun_server_port = stun_port;

    // Correct callback members (from juice.h).
    // 正确的回调成员（来自 juice.h）。
    config.cb_state_changed = IceAgent::OnStateChanged;
    config.cb_candidate = IceAgent::OnCandidate;
    config.cb_gathering_done = IceAgent::OnGatheringDone;
    config.cb_recv = IceAgent::OnData;
    config.user_ptr = this;

    // Bind to specific port if requested.
    // 如果指定了端口则绑定。
    // Note: libjuice uses bind_address and local_port_range for port control.
    // 注意：libjuice 使用 bind_address 和 local_port_range 控制端口。
    if (local_port != 0) {
        config.bind_address = "0.0.0.0";
        config.local_port_range_begin = local_port;
        config.local_port_range_end = local_port;
    }

    // Create agent.
    // 创建代理。
    agent_ = juice_create(&config);
    if (!agent_) {
        std::cerr << "[ICE] juice_create failed\n";
        return false;
    }

    return true;
}

bool IceAgent::GatherCandidates() {
    if (!agent_) return false;
    return juice_gather_candidates(agent_) == 0;
}

std::string IceAgent::GetLocalDescription() {
    if (!agent_) return "";
    char buffer[JUICE_MAX_SDP_STRING_LEN];
    if (juice_get_local_description(agent_, buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
    return "";
}

bool IceAgent::SetRemoteDescription(const std::string& sdp) {
    if (!agent_) return false;
    return juice_set_remote_description(agent_, sdp.c_str()) == 0;
}

bool IceAgent::AddRemoteCandidate(const std::string& sdp) {
    if (!agent_) return false;
    return juice_add_remote_candidate(agent_, sdp.c_str()) == 0;
}

bool IceAgent::SendData(const uint8_t* data, size_t len) {
    if (!agent_) return false;
    return juice_send(agent_, reinterpret_cast<const char*>(data), len) == 0;
}

IceState IceAgent::GetState() const {
    if (!agent_) return IceState::kDisconnected;
    return static_cast<IceState>(juice_get_state(agent_));
}

bool IceAgent::GetSelectedAddresses(std::string& local_ip_port,
                                    std::string& remote_ip_port) {
    if (!agent_) return false;

    char local_buf[JUICE_MAX_ADDRESS_STRING_LEN];
    char remote_buf[JUICE_MAX_ADDRESS_STRING_LEN];

    int ret = juice_get_selected_addresses(
        agent_,
        local_buf, sizeof(local_buf),
        remote_buf, sizeof(remote_buf)
    );

    if (ret == 0) {
        local_ip_port = local_buf;
        remote_ip_port = remote_buf;
        return true;
    }
    return false;
}

bool IceAgent::ParseAddress(const std::string& addr, std::string& ip, uint16_t& port) {
    size_t colon = addr.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    ip = addr.substr(0, colon);
    port = static_cast<uint16_t>(std::atoi(addr.substr(colon + 1).c_str()));
    return true;
}

} // namespace nat
} // namespace numotirus