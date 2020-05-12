// Copyright (c) Microsoft Corporation.
// Licensed under the MIT license.

#include "spdk_dpdk_queue.hh"

#include <arpa/inet.h>
#include <boost/chrono.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <cassert>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dmtr/annot.h>
#include <dmtr/cast.h>
#include <dmtr/latency.h>
#include <dmtr/libos.h>
#include <dmtr/sga.h>
#include <iostream>
#include <dmtr/libos/mem.h>
#include <dmtr/libos/raii_guard.hh>
#include <netinet/in.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ip.h>
#include <rte_lcore.h>
#include <rte_memcpy.h>
#include <rte_udp.h>
#include <spdk/env.h>
#include <spdk/log.h>
#include <spdk/nvme.h>
#include <unistd.h>

namespace bpo = boost::program_options;

bool dmtr::spdk_dpdk_queue::our_init_flag = false;

dmtr::spdk_dpdk_queue::spdk_dpdk_queue(int qd, dmtr::io_queue::category_id cid) :
    io_queue(qd, cid)
{
    switch (cid) {
    case NETWORK_Q:
        lwip_queue::new_object(net_queue, qd);
        break;
    case FILE_Q:
        spdk_queue::new_object(file_queue, qd);
        break;
    default:
        DMTR_UNREACHABLE();
    }
}
 
int dmtr::spdk_dpdk_queue::init_spdk_dpdk(int argc, char *argv[]) {
    DMTR_TRUE(ERANGE, argc >= 0);
    if (argc > 0) {
        DMTR_NOTNULL(EINVAL, argv);
    }
    DMTR_TRUE(EPERM, !our_dpdk_init_flag);

    std::string config_path;
    bpo::options_description desc("Allowed options");
    desc.add_options()
            ("help", "display usage information")
        ("config-path,c", bpo::value<std::string>(&config_path)->default_value("./config.yaml"), "specify configuration file");
    
    bpo::variables_map vm;
    bpo::store(bpo::command_line_parser(argc, argv).options(desc).allow_unregistered().run(), vm);
    bpo::notify(vm);
        
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    if (access(config_path.c_str(), R_OK) == -1) {
        std::cerr << "Unable to find config file at `" << config_path << "`." << std::endl;
        return ENOENT;
    }
    YAML::Node config = YAML::LoadFile(config_path);

    struct spdk_env_opts opts;
    spdk_env_opts_init(opts);
    opts->name = "Demeter";
    opts->mem_channel = 4;
    opts->core_mask = "0x4";
    struct spdk_pci_addr nic = {0,0x37,0, 0};
    opts.pci_whitelist = &nic;
    opts.num_pci_addr = 1;
    std::string eal_args = "--proc-type=auto";
    opts.env_context = (void*)eal_args.c_str();

    // use SPDK to init DPDK
    DMTR_OK(spdk_queue::init_spdk(config, &opts));
    DMTR_OK(lwip_queue::finish_init_dpdk(config));
    our_init_flag = true;
    return 0;
}

int dmtr::spdk_dpdk_queue::new_net_object(std::unique_ptr<io_queue> &q_out, int qd) {
    q_out = NULL;
    DMTR_TRUE(EPERM, our_init_flag);
    q_out = std::unique_ptr<io_queue>(new spdk_dpdk_queue(qd, NETWORK_Q));
    DMTR_NOTNULL(ENOMEM, q_out);
    return 0;
}

int dmtr::spdk_dpdk_queue::new_file_object(std::unique_ptr<io_queue> &q_out, int qd) {
    q_out = NULL;
    DMTR_TRUE(EPERM, our_init_flag);
    q_out = std::unique_ptr<io_queue>(new spdk_dpdk_queue(qd, FILE_Q));
    DMTR_NOTNULL(ENOMEM, q_out);
    return 0;
}

int dmtr::lwip_queue::socket(int domain, int type, int protocol) {
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->socket(domain, type, protocol);
}

int
dmtr::lwip_queue::getsockname(struct sockaddr * const saddr, socklen_t * const size) {
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->getsockname(saddr, size);
}

int dmtr::lwip_queue::accept(std::unique_ptr<io_queue> &q_out, dmtr_qtoken_t qt, int new_qd)
{
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->accept(q_out, qt, new_qd);
}

int dmtr::lwip_queue::listen(int backlog)
{
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->listen(backlog);
}

int dmtr::lwip_queue::bind(const struct sockaddr * const saddr, socklen_t size) {
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->bind(saddr, size);
}

int dmtr::lwip_queue::connect(dmtr_qtoken_t qt, const struct sockaddr * const saddr, socklen_t size) {
    DMTR_TRUE(EPERM, our_dpdk_flag);
    DMTR_TRUE(NETWORK_Q, my_cid);
    DMTR_NOTNULL(EINVAL, net_queue);
    return net_queue->connect(qt, saddr, size);
}

int dmtr::lwip_queue::close() {
    if (my_cid == NETWORK_Q) return net_queue->close();
    else return file_queue->close();
}

int dmtr::spdk_dpdk_queue::open(const char *pathname, int flags)
{
    DMTR_TRUE(EPERM, our_init_flag);
    DMTR_TRUE(FILE_Q, my_cid);
    DMTR_NOTNULL(file_queue);
    //TODO(ashmrtnz): Yay for only supporing a single file, so we do nothing! If
    // we choose to support multiple files we will need to so some sort of
    // lookup or something here.
    // TODO(ashmrtnz): O_TRUNC?
    file_queue->start_threads();
    return 0;
}

int dmtr::spdk_dpdk_queue::open(const char *pathname, int flags, mode_t mode)
{
    DMTR_TRUE(EPERM, our_init_flag);
    DMTR_TRUE(FILE_Q, my_cid);
    DMTR_NOTNULL(file_queue);
    //TODO(ashmrtnz): Yay for only supporing a single file, so we do nothing! If
    // we choose to support multiple files we will need to so some sort of
    // lookup or something here.
    // TODO(ashmrtnz): O_TRUNC?
    file_queue->start_threads();
    return 0;
}

int dmtr::spdk_dpdk_queue::creat(const char *pathname, mode_t mode)
{
    DMTR_TRUE(EPERM, our_init_flag);
    DMTR_TRUE(FILE_Q, my_cid);
    DMTR_NOTNULL(file_queue);
    //TODO(ashmrtnz): Yay for only supporing a single file, so we do nothing! If
    // we choose to support multiple files we will need to so some sort of
    // lookup or something here.
    // TODO(ashmrtnz): O_TRUNC?
    file_queue->start_threads();
    return 0;
}

int dmtr::spdk_dpdk_queue::push(dmtr_qtoken_t qt, const dmtr_sgarray_t &sga)
{
    DMTR_TRUE(EPERM, our_init_flag);
    if (my_cid == NETWORK_Q) return net_queue->push(qt, sga);
    else return file_queue->push(qt, sga);    
}

int dmtr::spdk_dpdk_queue::pop(dmtr_qtoken_t qt) {
    DMTR_TRUE(EPERM, our_init_flag);
    if (my_cid == NETWORK_Q) return net_queue->pop(qt, sga);
    else return file_queue->pop(qt, sga);    
}

int dmtr::spdk_dpdk_queue::poll(dmtr_qresult_t &qr_out, dmtr_qtoken_t qt)
{ 
    DMTR_TRUE(EPERM, our_init_flag);
    if (my_cid == NETWORK_Q) return net_queue->poll(qt, sga);
    else return file_queue->poll(qt, sga);    
}
