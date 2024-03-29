#include "okec.hpp"


using namespace ns3;

int main(int argc, char **argv)
{
    // fmt::print("C++ version: {}\n", __cplusplus);

    // Time::SetResolution(Time::NS);
    // LogComponentEnable("udp_application", LOG_LEVEL_INFO);
    okec::simulator simulator;

    okec::base_station_container base_stations(2);
    okec::edge_device_container edge_devices1(3);
    okec::edge_device_container edge_devices2(5);
    base_stations.connect_device(edge_devices1, edge_devices2);

    okec::client_device_container client_devices1(2);
    okec::client_device_container client_devices2(2);
    std::vector<okec::client_device_container> client_devices;
    client_devices.push_back(std::move(client_devices1));
    client_devices.push_back(std::move(client_devices2));

    okec::multiple_and_single_LAN_WLAN_network_model net_model;
    okec::network_initializer(net_model, client_devices, base_stations);

    okec::resource_container client_rcontainer(client_devices.size());
    okec::resource_container edge1_rcontainer(edge_devices1.size());
    okec::resource_container edge2_rcontainer(edge_devices2.size());
    client_rcontainer.initialize([](auto res) {
        res->attribute("cpu", "0.1");
    });
    edge1_rcontainer.initialize([](auto res) {
        res->attribute("cpu", fmt::format("{:.2f}", torch::rand({1}).uniform_(0.1, 0.5).item<double>()));
    });
    edge2_rcontainer.initialize([](auto res) {
        res->attribute("cpu", fmt::format("{:.2f}", torch::rand({1}).uniform_(0.1, 0.5).item<double>()));
    });

    client_rcontainer.print();
    edge1_rcontainer.print();
    edge2_rcontainer.print();

    // client_devices.install_resources(client_rcontainer); // 一键为所有用户设备配置资源
    edge_devices1.install_resources(edge1_rcontainer);   // 一键为所有边缘设备安装资源
    edge_devices2.install_resources(edge2_rcontainer);   // 一键为所有边缘设备安装资源

    auto decision_engine = std::make_shared<okec::scene1_decision_engine>(client_devices, &base_stations);
    base_stations.set_decision_engine(decision_engine);
    client_devices[0].set_decision_engine(decision_engine);

    okec::task t;
    t.emplace_back({
            { "task_id", okec::task::get_unique_id() },
            { "group", "one" },
            { "cpu_cycle", fmt::format("{:.2f}", torch::rand({1}).uniform_(0, 0.5).item<double>()) }
        });

    auto device_1 = client_devices[0].get_device(0);
    device_1->send_to(t);
    // device_1->when_done([](okec::response res) {
    
    //     fmt::print("task is done!\n");
    // });







    Simulator::Stop(Seconds(300));
    Simulator::Run();
    Simulator::Destroy();
}