#ifndef SIMEG_EDGE_DEVICE_H
#define SIMEG_EDGE_DEVICE_H

#include "resource.h"
#include "udp_application.h"


namespace okec
{

class task;


class edge_device
{
    using callback_type  = std::function<void(edge_device*, Ptr<Packet>, const Address&)>;

public:
    edge_device();

    // 返回当前设备的IP地址
    auto get_address() const -> ns3::Ipv4Address;

    // 返回当前设备的端口号
    auto get_port() const -> uint16_t;

    auto get_node() -> ns3::Ptr<ns3::Node>;

    // 获取当前设备绑定的资源
    auto get_resource() -> Ptr<resource>;

    // 为当前设备安装资源
    auto install_resource(Ptr<resource> res) -> void;

    auto set_position(double x, double y, double z) -> void;
    auto get_position() -> Vector;

    auto set_request_handler(std::string_view msg_type, callback_type callback) -> void;

    auto write(Ptr<Packet> packet, Ipv4Address destination, uint16_t port) const -> void;

private:
    auto on_get_resource_information(ns3::Ptr<ns3::Packet> packet, const ns3::Address& remote_address) -> void;

public:
    ns3::Ptr<Node> m_node;
    ns3::Ptr<okec::udp_application> m_udp_application;
};


class edge_device_container
{
    using value_type   = edge_device;
    using pointer_type = std::shared_ptr<value_type>;

public:
    edge_device_container(std::size_t n);

    // 获取所有Nodes
    auto get_nodes(ns3::NodeContainer &nodes) -> void;

    auto get_device(std::size_t index) -> pointer_type;

    auto begin() -> std::vector<pointer_type>::iterator {
        return m_devices.begin();
    }

    auto end() -> std::vector<pointer_type>::iterator {
        return m_devices.end();
    }

    auto size() -> std::size_t;

    auto install_resources(resource_container& res, int offset = 0) -> void;

private:
    std::vector<pointer_type> m_devices;
};


} // namespace simeg


#endif // SIMEG_EDGE_DEVICE_H