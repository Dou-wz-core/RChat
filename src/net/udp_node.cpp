#include "net/udp_node.hpp"
#include "net/udp_socket.hpp"
#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

namespace chat::net {
struct UdpNode::State : std::enable_shared_from_this<State> {
    struct PeerConnection : Connection {
        std::weak_ptr<State> state;
        std::uint64_t session;
        std::atomic<bool> active{true};
        PeerConnection(std::weak_ptr<State> s, std::uint64_t id) : state(std::move(s)), session(id) {}
        std::uint64_t id() const noexcept override { return session; }
        bool alive() const noexcept override { return active.load(); }
        bool send(std::string body, DeliveryCallback delivered, std::uint32_t replyTo) override;
    };
    static constexpr std::size_t MaxEvents = 1024, MaxEventBytes = 2 * 1024 * 1024;
    UdpSocket socket;
    Endpoint local;
    bool server, stopping = false, connectStarted = false;
    std::mutex mutex;
    std::condition_variable ready;
    std::unique_ptr<ReliableUdp> engine;
    std::map<std::uint64_t, std::shared_ptr<PeerConnection>> connections;
    std::map<std::pair<std::uint64_t, std::uint32_t>, DeliveryCallback> completions;
    std::deque<Event> events;
    std::size_t eventBytes = 0, reserved = 0;

    State(bool isServer, const Endpoint& bind) : socket(bind), local(socket.localEndpoint()), server(isServer) {}
    void push(Event event) { eventBytes += event.message.body.size(); events.push_back(std::move(event)); ready.notify_one(); }
    void initialize(const TransportConfig& config) {
        ReliableUdp::Callbacks cb;
        cb.opened = [this](std::uint64_t id, const Endpoint&) {
            if (events.size() + reserved + 2 > MaxEvents) return false;
            if (!server) --reserved; // release connect-failure reservation
            auto connection = std::make_shared<PeerConnection>(weak_from_this(), id);
            connections.emplace(id, connection); ++reserved; // guaranteed future close event
            Event e; e.type = EventType::Opened; e.connection = std::move(connection); push(std::move(e));
            return true;
        };
        cb.received = [this](std::uint64_t id, ReceivedMessage message) {
            if (events.size() + reserved >= MaxEvents || message.body.size() > MaxEventBytes - eventBytes) return false;
            Event e; e.type = EventType::Message; e.connection = connections.at(id); e.message = std::move(message);
            push(std::move(e)); return true;
        };
        cb.delivered = [this](std::uint64_t id, std::uint32_t message, bool success) {
            auto it = completions.find({id, message});
            if (it == completions.end()) return;
            --reserved;
            Event e; e.type = EventType::Delivered; e.connection = connections.at(id);
            e.completion = std::move(it->second); e.success = success;
            completions.erase(it); push(std::move(e));
        };
        cb.closed = [this](std::uint64_t id, const std::string& reason) {
            Event e; e.type = EventType::Closed; e.reason = reason;
            if (id) {
                auto it = connections.find(id);
                if (it == connections.end()) return;
                it->second->active.store(false); e.connection = it->second; connections.erase(it);
            }
            --reserved; push(std::move(e));
        };
        engine = std::make_unique<ReliableUdp>(server,
            [this](const Endpoint& to, const Bytes& bytes) { socket.send(to, bytes); }, std::move(cb), config);
    }
};
bool UdpNode::State::PeerConnection::send(std::string body, DeliveryCallback delivered, std::uint32_t replyTo) {
    auto owner = state.lock();
    if (!owner) return false;
    std::lock_guard<std::mutex> lock(owner->mutex);
    if (owner->stopping || !alive() || (delivered && owner->events.size() + owner->reserved >= MaxEvents)) return false;
    auto message = owner->engine->enqueue(session, std::move(body), replyTo);
    if (!message) return false;
    if (delivered) { owner->completions.emplace(std::make_pair(session, message), std::move(delivered)); ++owner->reserved; }
    return true;
}
UdpNode::UdpNode(bool server, Endpoint local, TransportConfig config)
    : state_(std::make_shared<State>(server, local)) { state_->initialize(config); }
UdpNode::~UdpNode() { stop(); }
Endpoint UdpNode::localEndpoint() const { return state_->local; }
void UdpNode::connect(const Endpoint& server) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->server || state_->connectStarted || state_->stopping) throw std::logic_error("invalid client connect");
    state_->connectStarted = true; ++state_->reserved;
    state_->engine->connect(server, randomToken(), monotonicMs());
}
void UdpNode::poll(unsigned timeoutMs) {
    if (stopped()) return;
    if (state_->socket.waitReadable(timeoutMs)) {
        for (unsigned count = 0; count < 64; ++count) {
            Endpoint from; Bytes bytes;
            if (!state_->socket.receive(from, bytes)) break;
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->stopping) return;
            state_->engine->input(from, bytes, monotonicMs());
        }
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->stopping) state_->engine->tick(monotonicMs());
}
bool UdpNode::nextEvent(Event& event, unsigned timeoutMs) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->ready.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return !state_->events.empty() || state_->stopping; });
    if (state_->events.empty()) return false;
    event = std::move(state_->events.front()); state_->events.pop_front();
    state_->eventBytes -= event.message.body.size();
    return true;
}
void UdpNode::stop() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping) return;
    state_->stopping = true; state_->engine->closeAll(); state_->ready.notify_all();
}
bool UdpNode::stopped() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->stopping; }
} // namespace chat::net
