#include "decision_engine.h"
#include "base_station.h"
#include "cloud_server.h"
#include "format_helper.hpp"
#include <algorithm>
#include <charconv>
#include <ranges>


namespace okec
{

auto device_cache::begin() -> iterator
{
    return this->view().begin();
}

auto device_cache::end() -> iterator
{
    return this->view().end();
}

auto device_cache::dump(int indent) -> std::string
{
    return this->cache.dump(indent);
}

auto device_cache::data() const -> value_type
{
    return this->cache["device_cache"]["items"];
}

auto device_cache::view() -> value_type&
{
    return this->cache["device_cache"]["items"];
}

auto device_cache::size() const -> std::size_t
{
    return this->data().size();
}

auto device_cache::empty() const -> bool
{
    return this->size() < 1;
}

auto device_cache::emplace_back(attributes_type values) -> void
{
    value_type item;
    for (auto [key, value] : values) {
        item[key] = value;
    }

    this->emplace_back(std::move(item));
}

auto device_cache::find_if(unary_predicate_type pred) -> iterator
{
    auto& items = this->view();
    return std::find_if(items.begin(), items.end(), pred);
}

auto device_cache::sort(binary_predicate_type comp) -> void
{
    auto& items = this->view();
    std::sort(items.begin(), items.end(), comp);
}

auto device_cache::emplace_back(value_type item) -> void
{
    this->cache["device_cache"]["items"].emplace_back(std::move(item));
}

auto decision_engine::calculate_distance(const Vector& pos) -> double
{
    Vector this_pos = m_decision_device->get_position();
    double delta_x = this_pos.x - pos.x;
    double delta_y = this_pos.y - pos.y;
    double delta_z = this_pos.z - pos.z;

    return std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
}

auto decision_engine::initialize_device(base_station_container* bs_container, cloud_server* cs) -> void
{
    // Save a base station so we can utilize its communication component.
    if (!m_decision_device)
        m_decision_device = bs_container->get(0);

    // 记录云服务器信息
    if (cs) {
        auto cs_pos = cs->get_position();
        auto cs_res = cs->get_resource();

        if (cs_res && !cs_res->empty()) {
            m_device_cache.emplace_back({
                { "device_type", "cs" },
                { "ip", fmt::format("{:ip}", cs->get_address()) },
                { "port", std::to_string(cs->get_port()) },
                { "pos_x", std::to_string(cs_pos.x) },
                { "pos_y", std::to_string(cs_pos.y) },
                { "pos_z", std::to_string(cs_pos.z) }
            });

            auto find_pred = [](const device_cache::value_type& item) {
                return item["device_type"] == "cs";
            };
            auto item = m_device_cache.find_if(find_pred);
            if (item != m_device_cache.end())
                for (auto it = cs_res->begin(); it != cs_res->end(); ++it)
                    (*item)[it.key()] = it.value();

            print_info(fmt::format("The decision engine got the resource information of cloud({}).", (*item)["ip"]));
        } else {
            // 说明设备此时还未绑定资源，通过网络询问一下
            Simulator::Schedule(Seconds(1.0), +[](const std::shared_ptr<base_station> socket, const ns3::Ipv4Address& ip, uint16_t port) {
                message msg;
                msg.type("get_resource_information");
                socket->write(msg.to_packet(), ip, port);
            }, this->m_decision_device, cs->get_address(), cs->get_port());
        }
    }

    // 记录边缘服务器信息
    double delay = 1.0;
    std::for_each(bs_container->begin(), bs_container->end(),
    [&delay, this](const base_station_container::pointer_t bs) {
        for (const auto& device : bs->get_edge_devices()) {
            auto p_resource = device->get_resource();

            // 动态记录资源信息
            if (p_resource && !p_resource->empty()) {
                // 设备已经绑定资源，直接记录
                auto es_pos = device->get_position();
                auto ip = fmt::format("{:ip}", device->get_address());
                auto port = std::to_string(device->get_port());

                m_device_cache.emplace_back({
                    { "device_type", "es" },
                    { "ip", ip },
                    { "port", port },
                    { "pos_x", std::to_string(es_pos.x) },
                    { "pos_y", std::to_string(es_pos.y) },
                    { "pos_z", std::to_string(es_pos.z) }
                });

                auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                    return item["ip"] == ip && item["port"] == port;
                };
                auto item = m_device_cache.find_if(find_pred);
                if (item != m_device_cache.end()) {
                    for (auto it = p_resource->begin(); it != p_resource->end(); ++it) {
                        (*item)[it.key()] = it.value();
                    }
                }

                print_info(fmt::format("The decision engine got the resource information of edge device({}).", (*item)["ip"]));
            } else {
                // 说明设备此时还未绑定资源，通过网络询问一下
                Simulator::Schedule(Seconds(delay), +[](const std::shared_ptr<base_station> socket, const ns3::Ipv4Address& ip, uint16_t port) {
                    message msg;
                    msg.type(message_get_resource_information);
                    socket->write(msg.to_packet(), ip, port);
                }, this->m_decision_device, device->get_address(), device->get_port());
                delay += 0.1;
            }
        }
    });

    // fmt::print("Info: {}\n", m_device_cache.dump());

    // 捕获通过网络问询的信息，更新设备信息（能收到就一定存在资源信息）
    m_decision_device->set_request_handler(message_resource_information, 
        [this](okec::base_station* bs, Ptr<Packet> packet, const Address& remote_address) {
            print_info(fmt::format("The decision engine has received device resource information: {}", okec::packet_helper::to_string(packet)));
            auto msg = message::from_packet(packet);
            auto es_resource = resource::from_msg_packet(packet);
            auto ip = msg.get_value("ip");
            auto port = msg.get_value("port");

            m_device_cache.emplace_back({
                { "device_type", msg.get_value("device_type") },
                { "ip", ip },
                { "port", port },
                { "pos_x", msg.get_value("pos_x") },
                { "pos_y", msg.get_value("pos_y") },
                { "pos_z", msg.get_value("pos_z") }
            });

            auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                return item["ip"] == ip && item["port"] == port;
            };
            auto item = m_device_cache.find_if(find_pred);
            if (item != m_device_cache.end()) {
                for (auto it = es_resource.begin(); it != es_resource.end(); ++it) {
                    (*item)[it.key()] = it.value();
                }
            }
        });

    // 捕获资源变化信息
    // 资源更新(外部所指定的BS不一定是第0个，所以要为所有BS设置消息以确保捕获)
    bs_container->set_request_handler(message_resource_changed, 
        [this](okec::base_station* bs, Ptr<Packet> packet, const Address& remote_address) {
            fmt::print(fg(fmt::color::white), "At time {:.2f}s The decision engine got notified about device resource changes: {}\n", Simulator::Now().GetSeconds() , okec::packet_helper::to_string(packet));
            auto msg = message::from_packet(packet);
            auto es_resource = resource::from_msg_packet(packet);
            auto ip = msg.get_value("ip");
            auto port = msg.get_value("port");

            // 更新资源信息
            auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                return item["ip"] == ip && item["port"] == port;
            };
            auto item = m_device_cache.find_if(find_pred);
            if (item != m_device_cache.end()) {
                for (auto it = es_resource.begin(); it != es_resource.end(); ++it) {
                    (*item)[it.key()] = it.value();
                }
            }

            // 继续处理下一个任务的分发
            bs->handle_next_task();
        });
}

auto decision_engine::initialize_device(base_station_container* bs_container) -> void
{
    // Save a base station so we can utilize its communication component.
    if (!m_decision_device)
        m_decision_device = bs_container->get(0);

    // 记录边缘服务器信息
    double delay = 1.0;
    std::for_each(bs_container->begin(), bs_container->end(),
    [&delay, this](const base_station_container::pointer_t bs) {
        for (const auto& device : bs->get_edge_devices()) {
            auto p_resource = device->get_resource();

            // 动态记录资源信息
            if (p_resource && !p_resource->empty()) {
                // 设备已经绑定资源，直接记录
                auto es_pos = device->get_position();
                auto ip = fmt::format("{:ip}", device->get_address());
                auto port = std::to_string(device->get_port());

                m_device_cache.emplace_back({
                    { "device_type", "es" },
                    { "ip", ip },
                    { "port", port },
                    { "pos_x", std::to_string(es_pos.x) },
                    { "pos_y", std::to_string(es_pos.y) },
                    { "pos_z", std::to_string(es_pos.z) }
                });

                auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                    return item["ip"] == ip && item["port"] == port;
                };
                auto item = m_device_cache.find_if(find_pred);
                if (item != m_device_cache.end()) {
                    for (auto it = p_resource->begin(); it != p_resource->end(); ++it) {
                        (*item)[it.key()] = it.value();
                    }
                }

                print_info(fmt::format("The decision engine got the resource information of edge device({}).", (*item)["ip"]));
            } else {
                // 说明设备此时还未绑定资源，通过网络询问一下
                Simulator::Schedule(Seconds(delay), +[](const std::shared_ptr<base_station> socket, const ns3::Ipv4Address& ip, uint16_t port) {
                    message msg;
                    msg.type(message_get_resource_information);
                    socket->write(msg.to_packet(), ip, port);
                }, this->m_decision_device, device->get_address(), device->get_port());
                delay += 0.1;
            }
        }
    });

    // fmt::print("Info: {}\n", m_device_cache.dump());

    // 捕获通过网络问询的信息，更新设备信息（能收到就一定存在资源信息）
    m_decision_device->set_request_handler(message_resource_information, 
        [this](okec::base_station* bs, Ptr<Packet> packet, const Address& remote_address) {
            print_info(fmt::format("The decision engine has received device resource information: {}", okec::packet_helper::to_string(packet)));
            auto msg = message::from_packet(packet);
            auto es_resource = resource::from_msg_packet(packet);
            auto ip = msg.get_value("ip");
            auto port = msg.get_value("port");

            m_device_cache.emplace_back({
                { "device_type", msg.get_value("device_type") },
                { "ip", ip },
                { "port", port },
                { "pos_x", msg.get_value("pos_x") },
                { "pos_y", msg.get_value("pos_y") },
                { "pos_z", msg.get_value("pos_z") }
            });

            auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                return item["ip"] == ip && item["port"] == port;
            };
            auto item = m_device_cache.find_if(find_pred);
            if (item != m_device_cache.end()) {
                for (auto it = es_resource.begin(); it != es_resource.end(); ++it) {
                    (*item)[it.key()] = it.value();
                }
            }
        });

    // 捕获资源变化信息
    // 资源更新(外部所指定的BS不一定是第0个，所以要为所有BS设置消息以确保捕获)
    bs_container->set_request_handler(message_resource_changed, 
        [this](okec::base_station* bs, Ptr<Packet> packet, const Address& remote_address) {
            fmt::print(fg(fmt::color::white), "At time {:.2f}s The decision engine got notified about device resource changes: {}\n", Simulator::Now().GetSeconds() , okec::packet_helper::to_string(packet));
            auto msg = message::from_packet(packet);
            auto es_resource = resource::from_msg_packet(packet);
            auto ip = msg.get_value("ip");
            auto port = msg.get_value("port");

            // 更新资源信息
            auto find_pred = [&ip, &port](const device_cache::value_type& item) {
                return item["ip"] == ip && item["port"] == port;
            };
            auto item = m_device_cache.find_if(find_pred);
            if (item != m_device_cache.end()) {
                for (auto it = es_resource.begin(); it != es_resource.end(); ++it) {
                    (*item)[it.key()] = it.value();
                }
            }

            // 继续处理下一个任务的分发
            bs->handle_next_task();
        });
}

auto decision_engine::get_decision_device() const -> std::shared_ptr<base_station>
{
    return m_decision_device;
}

auto decision_engine::cache() -> device_cache&
{
    return m_device_cache;
}


} // namespace okec