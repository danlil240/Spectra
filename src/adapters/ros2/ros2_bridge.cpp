#include "ros2_bridge.hpp"

namespace spectra::adapters::ros2
{

Ros2Bridge::Ros2Bridge() = default;

Ros2Bridge::~Ros2Bridge()
{
    shutdown();
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

bool Ros2Bridge::init(const std::string& node_name,
                      const std::string& node_namespace,
                      int                argc,
                      char**             argv)
{
    const BridgeState current = state_.load(std::memory_order_acquire);

    // Already initialised — idempotent if same name/ns.
    if (current == BridgeState::Initialized || current == BridgeState::Spinning)
    {
        return (node_name_ == node_name && node_namespace_ == node_namespace);
    }

    // Can re-init from Stopped.
    if (current != BridgeState::Uninitialized && current != BridgeState::Stopped)
    {
        return false;   // ShuttingDown — caller must wait
    }

    // Initialise rclcpp if needed (safe to call multiple times per docs).
    if (!rclcpp::ok())
    {
        rclcpp::init(argc, argv);
    }

    node_name_      = node_name;
    node_namespace_ = node_namespace;

    rclcpp::NodeOptions opts;
    opts.automatically_declare_parameters_from_overrides(true);

    node_ = std::make_shared<rclcpp::Node>(node_name_, node_namespace_, opts);

    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);

    set_state(BridgeState::Initialized);
    return true;
}

// ---------------------------------------------------------------------------
// start_spin
// ---------------------------------------------------------------------------

bool Ros2Bridge::start_spin()
{
    if (state_.load(std::memory_order_acquire) != BridgeState::Initialized)
        return false;

    stop_requested_.store(false, std::memory_order_release);
    spin_thread_ = std::thread(&Ros2Bridge::spin_thread_func, this);
    return true;
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------

void Ros2Bridge::shutdown()
{
    const BridgeState current = state_.load(std::memory_order_acquire);
    if (current == BridgeState::Uninitialized || current == BridgeState::Stopped)
        return;

    set_state(BridgeState::ShuttingDown);

    // Signal the spin thread to exit.
    stop_requested_.store(true, std::memory_order_release);

    // cancel() wakes the executor out of spin_some() / wait_for_work().
    if (executor_)
        executor_->cancel();

    // If rclcpp is still running, signal shutdown so any blocking spin ends.
    if (rclcpp::ok())
        rclcpp::shutdown();

    if (spin_thread_.joinable())
        spin_thread_.join();

    // Tear down resources.
    if (executor_ && node_)
        executor_->remove_node(node_);

    executor_.reset();
    node_.reset();

    set_state(BridgeState::Stopped);
}

// ---------------------------------------------------------------------------
// spin_thread_func  (runs on background thread)
// ---------------------------------------------------------------------------

void Ros2Bridge::spin_thread_func()
{
    set_state(BridgeState::Spinning);

    while (rclcpp::ok() && !stop_requested_.load(std::memory_order_acquire))
    {
        executor_->spin_once(std::chrono::milliseconds(10));
    }

    // State transition to ShuttingDown/Stopped is handled by shutdown().
}

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------

void Ros2Bridge::set_state(BridgeState s)
{
    state_.store(s, std::memory_order_release);
    if (state_cb_)
        state_cb_(s);
}

}   // namespace spectra::adapters::ros2
