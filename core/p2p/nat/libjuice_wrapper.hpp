// core/p2p/nat/libjuice_wrapper.hpp
// C++ wrapper for libjuice (ICE library).
// libjuice (ICE 库) 的 C++ 封装。
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <functional>
#include <cstdint>

extern "C" {
#include <juice/juice.h>
}

namespace numotirus {
namespace nat {

// ICE state enumeration.
// ICE 状态枚举。
enum class IceState {
    kDisconnected = JUICE_STATE_DISCONNECTED,
    kGathering = JUICE_STATE_GATHERING,
    kConnecting = JUICE_STATE_CONNECTING,
    kConnected = JUICE_STATE_CONNECTED,
    kCompleted = JUICE_STATE_COMPLETED,
    kFailed = JUICE_STATE_FAILED,
};

// Callback types.
// 回调类型。
using IceStateCallback = std::function<void(IceState state)>;
using IceCandidateCallback = std::function<void(const std::string& sdp)>;
using IceDataCallback = std::function<void(const uint8_t* data, size_t len)>;

// ICE agent wrapper.
// ICE 代理封装。
class IceAgent {
public:
    IceAgent();
    ~IceAgent();

    // Initialize the ICE agent with STUN server.
    // 用 STUN 服务器初始化 ICE 代理。
    // local_port: 0 for ephemeral, or a specific port to bind.
    // local_port: 0 表示临时端口，或指定绑定端口。
    bool Initialize(uint16_t local_port,
                    const std::string& stun_server,
                    IceStateCallback state_cb,
                    IceCandidateCallback candidate_cb,
                    IceDataCallback data_cb);

    // Start gathering ICE candidates.
    // 开始收集 ICE 候选地址。
    bool GatherCandidates();

    // Get local SDP description.
    // 获取本地 SDP 描述。
    std::string GetLocalDescription();

    // Set remote SDP description.
    // 设置远程 SDP 描述。
    bool SetRemoteDescription(const std::string& sdp);

    // Add a single remote candidate.
    // 添加单个远程候选地址。
    bool AddRemoteCandidate(const std::string& sdp);

    // Send data through ICE.
    // 通过 ICE 发送数据。
    bool SendData(const uint8_t* data, size_t len);

    // Get current ICE state.
    // 获取当前 ICE 状态。
    IceState GetState() const;

    // Get selected candidate addresses (both local and remote) as "ip:port".
    // 获取选中的候选地址（本地和远程），格式为 "ip:port"。
    bool GetSelectedAddresses(std::string& local_ip_port,
                              std::string& remote_ip_port);

    // Parse "ip:port" string into IP and port.
    // 从 "ip:port" 字符串解析 IP 和端口。
    static bool ParseAddress(const std::string& addr, std::string& ip, uint16_t& port);

private:
    juice_agent_t* agent_ = nullptr;

    // Callbacks stored for internal use.
    // 存储回调供内部使用。
    IceStateCallback state_cb_;
    IceCandidateCallback candidate_cb_;
    IceDataCallback data_cb_;

    // Static callbacks for libjuice.
    // libjuice 的静态回调。
    static void OnStateChanged(juice_agent_t* agent, juice_state_t state, void* user_ptr);
    static void OnCandidate(juice_agent_t* agent, const char* sdp, void* user_ptr);
    static void OnGatheringDone(juice_agent_t* agent, void* user_ptr);
    static void OnData(juice_agent_t* agent, const char* data, size_t size, void* user_ptr);
};

} // namespace nat
} // namespace numotirus