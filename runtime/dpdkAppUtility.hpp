#pragma once

#include "../utility.hpp"
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include <sys/stat.h>
#include <netinet/in.h>

#include <rte_malloc.h>

#include <pcapplusplus/Packet.h>
#include <pcapplusplus/PacketUtils.h>
#include <pcapplusplus/DpdkDevice.h>
#include <pcapplusplus/DpdkDeviceList.h>
#include <pcapplusplus/PcapFileDevice.h>
#include <pcapplusplus/SystemUtils.h>
#include <pcapplusplus/Logger.h>

#include <pcapplusplus/DpdkDevice.h>
#include <pcapplusplus/TcpLayer.h>
#include <pcapplusplus/TablePrinter.h>
#include <pcapplusplus/IPv4Layer.h>
#include <pcapplusplus/UdpLayer.h>
#include <pcapplusplus/IcmpLayer.h>
#include <pcapplusplus/Logger.h>

#include <tbb/concurrent_hash_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/parallel_for_each.h>
#include <tbb/parallel_invoke.h>
#include <tbb/concurrent_queue.h>

#include <bits/stdc++.h>

using namespace std;
using namespace pcpp;


namespace Reaper
{
// nic->(port id -> queue id)
using nic_port_id_t = uint16_t;
using nic_queue_id_t = uint16_t;
    
// cpu->(core id)
using cpu_core_id_t = uint16_t;

// memory->(memory pool size)
using mem_pool_size_t = uint16_t;

// record groups of queue from all DpdkDevices
using dpdk_dev_map_t = unordered_map<DpdkDevice *, vector<nic_queue_id_t> >;

// Type: The list of Dpdk Devices -> All Dpdk Device 
using dpdk_dev_list_t = vector<DpdkDevice *>;

// rx queues map
struct DpdkDevMap final {

    cpu_core_id_t core_id;
    dpdk_dev_map_t  dpdk_dev_map;

    DpdkDevMap(): core_id(MAX_NUM_OF_CORES + 1) {}
    virtual ~DpdkDevMap() {}
    // 禁止赋值运算符
    DpdkDevMap & operator=(const DpdkDevMap &) = delete;
    // 禁止默认的拷贝构造函数
    DpdkDevMap(const DpdkDevMap &) = delete;

    // void add_dpdk_dev_queue(dpdk_dev_map_t::key_type dpdk_dev, dpdk_dev_map_t::value_type queue_id) {
    //     dpdk_dev_map.emplace(dpdk_dev, queue_id);
    // }
};

// Type: The list of DpdkDevConfigs -> All cores for parsing
using dpdk_dev_map_list_t = vector<shared_ptr<DpdkDevMap > >;

struct PacketMetaData final {

	uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_port;
    uint32_t dst_port;
    uint8_t proto;
	uint32_t pkt_attr;
	uint16_t pkt_length;
	uint64_t time_stamp;

	PacketMetaData() {};

	explicit PacketMetaData(uint32_t s_i, uint32_t d_i, 
                            uint32_t s_p, uint32_t d_p, 
                            uint8_t p, uint32_t p_a, 
                            uint16_t p_l, uint64_t ts):
			                src_ip(s_i), dst_ip(d_i), 
                            src_port(s_p), dst_port(d_p), 
                            proto(p), pkt_attr(p_a), 
                            pkt_length(p_l), time_stamp(ts) {}
	
	virtual ~PacketMetaData() {};
    PacketMetaData & operator=(const PacketMetaData &) = default;
    PacketMetaData(const PacketMetaData &) = default;

};

using PktMetaDataArray = vector<uint64_t >;
using PktMetaDataArrayOutput = pair<shared_ptr<PktMetaDataArray >, uint32_t >;

struct FlowID {

    uint32_t low_ip; uint32_t high_ip;
    uint32_t low_port; uint32_t high_port;
    uint8_t proto;

    bool operator==(const FlowID & flow_id) const {
        
        return (low_ip == flow_id.low_ip && 
                high_ip == flow_id.high_ip &&
                low_port == flow_id.low_port &&
                high_port == flow_id.high_port &&
                proto == flow_id.proto);

    }

};

// 如果是icmp流(最高仅到网络层), 那就为icmp流保留一个不存在的端口

struct FlowIDHashCompare {

	size_t hash(const FlowID & flow_id) const {

        size_t h1 = std::hash<uint32_t >()(flow_id.low_ip);
        size_t h2 = std::hash<uint32_t >()(flow_id.high_ip);
        size_t h3 = std::hash<uint32_t >()(flow_id.low_port);
        size_t h4 = std::hash<uint32_t >()(flow_id.high_port);
        size_t h5 = std::hash<uint8_t >()(flow_id.proto);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);

    }
 
	bool equal(const FlowID & x, const FlowID & y) const {
		
        return x == y;
	
    }
};

struct FlowIDHash {

	size_t operator()(const FlowID & flow_id) const {

        size_t h1 = std::hash<uint32_t >()(flow_id.low_ip);
        size_t h2 = std::hash<uint32_t >()(flow_id.high_ip);
        size_t h3 = std::hash<uint32_t >()(flow_id.low_port);
        size_t h4 = std::hash<uint32_t >()(flow_id.high_port);
        size_t h5 = std::hash<uint8_t >()(flow_id.proto);

        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);

    }
 
};

struct FlowDataStats {

    shared_ptr<PktMetaDataArray > p_flat_vec;

    uint32_t len = 0;
    uint32_t vol = 0;

    uint64_t first_ts = 0;
    uint64_t last_ts = 0;

    FlowDataStats() { p_flat_vec = make_shared<PktMetaDataArray >(); }

};

struct FlowEntry {

    // 0-> bidirectional, 1-> forward, 2-> backward

    vector<FlowDataStats > dirs = vector<FlowDataStats >(3);

    bool forward_init = false;
    bool backward_init = false;

};


// PktMetaDataArray: vector<{curr_ts, length, type}>

// 所有流对应的元数据数组都由shared_ptr管理

class MTS {

    private:

    // 记录没一个数据包的元数据: 到达时间戳, 数据包长度, 类型
    shared_ptr<PktMetaDataArray > p_flat_vec;

    //
    shared_ptr<PktMetaDataArray > get_flat_vec() const { return p_flat_vec; } 

    void sort2mts() {

        PktMetaDataArray & _flat_vec = *p_flat_vec;

        auto merge = [&_flat_vec] (uint32_t left, uint32_t mid, uint32_t right) -> void {

            uint32_t i = left, j = mid + 1;

            while (i <= mid && j <= right) {

                if (_flat_vec[i * 3] <= _flat_vec[j * 3]) { i ++;
                } else {
                    
                    for (uint32_t k = 0; k < 3; k ++) {

                        uint64_t temp = _flat_vec[j * 3 + k];
                        
                        for (uint32_t l = j * 3 + k; l > i * 3 + k; l--) {
                            
                            _flat_vec[l] = _flat_vec[l - 1];
                        
                        }
                        
                        _flat_vec[i * 3 + k] = temp;
                    }

                    i ++; mid ++; j ++;

                }

            }

        };

        if (_flat_vec.size() % 3 != 0) { return; }

        uint32_t numTriplets = _flat_vec.size() / 3;

        // 从子数组大小为1开始，逐步扩大到整个数组
        for (uint32_t size = 1; size < numTriplets; size *= 2) {

            for (uint32_t left = 0; left < numTriplets - size; left += 2 * size) {

                uint32_t mid = left + size - 1;
                uint32_t right = min(left + 2 * size - 1, numTriplets - 1);

                merge(left, mid, right);
            }

        }
    }

    public:

    MTS() { p_flat_vec = make_shared<PktMetaDataArray >(); }

    // 按值传递shared_ptr更安全, 会使其引用计数自增
    void insert(const shared_ptr<PktMetaDataArray > & _p_flat_vec) {

        if (_p_flat_vec && !_p_flat_vec->empty()) { p_flat_vec->insert(p_flat_vec->end(), _p_flat_vec->begin(), _p_flat_vec->end()); }

    }

    void insert(const MTS & _mts) { 

        shared_ptr<PktMetaDataArray > _p_flat_vec = _mts.get_flat_vec();
        
        insert(_p_flat_vec);
        
    }

    shared_ptr<PktMetaDataArray > get_mts() {

        sort2mts();

        return p_flat_vec;

    }

    uint32_t size() { return p_flat_vec->size(); }

};

// 左0右1
// 记录所有的is_end节点

// 最大的深度

struct TrieNode {

    vector<shared_ptr<TrieNode > > children = vector<shared_ptr<TrieNode > >(2, nullptr);
    weak_ptr<TrieNode > parent;

    // 节点自身属性
    bool is_end; // 截止至当前节点是否构成一个IP前缀, 是否为空节点
    uint16_t type = 2; // 2代表根节点, 0代表左节点, 1代表右节点

    // 节点包含数据, 与聚合流关联(多维时间序列数据, 聚合的个数)
    MTS aggr_flow;
    uint64_t aggr_len = 0;
    uint64_t aggr_vol = 0;

    TrieNode() : is_end(false) {}
    TrieNode(uint16_t t) : is_end(false), type(t) {}

};

// unique_ptr<IPTrie > 
// 按批次聚合, 聚合后传递给检测模块
class IPTrie {

    // using IPTrieLeaves = tbb::concurrent_hash_map<shared_ptr<TrieNode >, uint8_t >;
    using IPTrieLeaves = unordered_set<shared_ptr<TrieNode > >;

    private:

    IPTrieLeaves left_leaves; // 包含了所有左叶子节点
    IPTrieLeaves right_leaves; // 包含了所有没有左兄弟的右叶子节点

    unordered_map<uint32_t, shared_ptr<TrieNode > > path2bound;

    uint32_t bound_prefix_length = 24;
    uint32_t bound_prefix_mask = 0xffffff00;
    uint32_t traverse_init_mask = 0x00000080;

    uint64_t aggr_len_th = 500;
    uint32_t trunc_flow_len = 1000;

    shared_ptr<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > > p_output;

    public:

    IPTrie() { 
        
        p_output = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();
        
    }

    IPTrie(uint32_t b, uint64_t a, uint32_t t,
            shared_ptr<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > > p_o) : 
                 bound_prefix_length(b), aggr_len_th(a), trunc_flow_len(t), p_output(p_o) { 
        
        bound_prefix_mask = ~((uint32_t(1) << (32 - bound_prefix_length)) - uint32_t(1));
        if (bound_prefix_length == 32) traverse_init_mask = 0x0;
        else traverse_init_mask = uint32_t(0x80000000) >> bound_prefix_length;  
        
    }

    void insert(const uint32_t & ip, const FlowDataStats & _stats) {

        uint32_t bound_prefix = ip & bound_prefix_mask;

        if (path2bound.find(bound_prefix) == path2bound.end()) {

            path2bound[bound_prefix] = make_shared<TrieNode >(2);

        }

        shared_ptr<TrieNode > node = path2bound[bound_prefix];

        uint32_t _bit;

        // 时间复杂度: O(32 - bound_prefix_length)
        for (uint32_t m = traverse_init_mask; m > 0; m >>= 1) {

            _bit = (m & ip) == 0 ? 0 : 1;

            if (!node->children[_bit]) { 
                
                node->children[_bit] = make_shared<TrieNode >(_bit);
                node->children[_bit]->parent = node; 

            }

            node = node->children[_bit];

        }

        // 更新当前节点的mts
        if (!node->is_end) { node->is_end = true; }

        if (node->aggr_len < trunc_flow_len) {

            node->aggr_flow.insert(_stats.p_flat_vec); node->aggr_len += _stats.len;
            node->aggr_vol += _stats.vol;

        } else {

            node->aggr_len += _stats.len;
            node->aggr_vol += _stats.vol;

        }

        // 维护叶子节点集合
        if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {

            // IPTrieLeaves::accessor acc;

            if (node->type == 0) { // 当前叶子节点是左节点

                // 直接插入左叶子节点集合                
                // if (!left_leaves.find(acc, node)) left_leaves.insert(acc, node);
                left_leaves.insert(node);
                
                // 同时, 如果当前左叶子节点有右兄弟, 则从右叶子节点集合中删除
                shared_ptr<TrieNode > sibling = node_parent->children[1];
                
                if (sibling) { 

                    // if (right_leaves.find(acc, sibling)) right_leaves.erase(acc); 
                    right_leaves.erase(sibling);

                }
            
            } else if (node->type == 1) { // 当前叶子节点是右节点

                shared_ptr<TrieNode > sibling = node_parent->children[0];
                
                if (!sibling) { 

                    // if (!right_leaves.find(acc, node)) right_leaves.insert(acc, node); 
                    right_leaves.insert(node);

                }

            }

        } else {

            // IPTrieLeaves::accessor acc;
            // if (!left_leaves.find(acc, node)) left_leaves.insert(acc, node);
            left_leaves.insert(node);

        }

    }

    // 叶子节点聚合时, 成对出现的兄弟节点考虑是否将数据聚合传递给父节点; 而落单的叶子节点, 直接将数据传递给父节点 
    uint32_t aggregate() {

        // sem_t semaphore;
        // sem_init(&semaphore, 0, 1);

        uint32_t curr_aggr_pkt_len = 0;

        while (!left_leaves.empty() || !right_leaves.empty()) {

            IPTrieLeaves next_left_leaves, next_right_leaves;
            IPTrieLeaves next_all_leaves;

            // 当前轮次所有新聚合的叶子节点先加入next_all_leaves
            // 之后再分类至next_left_leaves, next_right_leaves

            for (const auto & node : left_leaves) {

                if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {

                    if (shared_ptr<TrieNode > sibling = node_parent->children[1]) {

                        // 当前的左叶子节点node具有兄弟节点sibling, 执行条件聚合

                        // 满足聚合条件, 将node与sibling的MTS加入它们的父节点, 父节点加入叶子节点集合
                        // node从p_left_leaves中删去, node_parent考虑加入p_next_left/right_leaves

                        if (node->aggr_len >= aggr_len_th && sibling->aggr_len >= aggr_len_th) {

                            shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                            shared_ptr<PktMetaDataArrayOutput > p1 = make_shared<PktMetaDataArrayOutput >(sibling->aggr_flow.get_mts(), sibling->aggr_vol);
                            
                            if (p_output) p_output->push(p0);
                            if (p_output) p_output->push(p1);

                            curr_aggr_pkt_len += node->aggr_vol;
                            curr_aggr_pkt_len += sibling->aggr_vol;

                        } else {

                            node_parent->aggr_flow.insert(node->aggr_flow);
                            node_parent->aggr_flow.insert(sibling->aggr_flow);

                            node_parent->is_end = true;
                            node_parent->aggr_len = node->aggr_len + sibling->aggr_len; 
                            node_parent->aggr_vol = node->aggr_vol + sibling->aggr_vol; 

                            next_all_leaves.insert(node_parent);

                        }

                    } else { // 当前的左叶子节点node不具有兄弟节点sibling

                        if (node->aggr_len >= aggr_len_th) {
                            
                            shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                            
                            if (p_output) p_output->push(p0);

                            curr_aggr_pkt_len += node->aggr_vol;

                        } else {

                            // 聚合数量不够, 将数据传递至父节点, 继续聚合
                            // node_parent加入next_all_leaves

                            node_parent->aggr_flow.insert(node->aggr_flow);

                            node_parent->is_end = true;
                            node_parent->aggr_len = node->aggr_len;
                            node_parent->aggr_vol = node->aggr_vol; 

                            next_all_leaves.insert(node_parent);

                        }

                    }
                
                } else {

                    if (node->aggr_len != 0) { 
                        
                        shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                            
                        if (p_output) p_output->push(p0);

                        curr_aggr_pkt_len += node->aggr_vol;

                    }

                } 

            }

            for (const auto & node : right_leaves) {

                if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {

                    if (node->aggr_len >= aggr_len_th) {

                        shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                            
                        if (p_output) p_output->push(p0);

                        curr_aggr_pkt_len += node->aggr_vol;

                    } else { 

                        node_parent->aggr_flow.insert(node->aggr_flow);
                            
                        node_parent->is_end = true;
                        node_parent->aggr_len = node->aggr_len;
                        node_parent->aggr_vol = node->aggr_vol; 

                        next_all_leaves.insert(node_parent);

                    }

                } else {

                    if (node->aggr_len != 0) { 
                        
                        shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                            
                        if (p_output) p_output->push(p0);

                        curr_aggr_pkt_len += node->aggr_vol;
                        
                    }

                }

            }



            // auto aggr_left_leaves = [&, this] () {

            //     tbb::parallel_for_each(left_leaves.begin(), left_leaves.end(), [&] (auto && item) {

            //         shared_ptr<TrieNode > node = item.first;

            //         if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {

            //             if (shared_ptr<TrieNode > sibling = node_parent->children[1]) {

            //                 // 当前的左叶子节点node具有兄弟节点sibling, 执行条件聚合

            //                 // 满足聚合条件, 将node与sibling的MTS加入它们的父节点, 父节点加入叶子节点集合
            //                 // node从p_left_leaves中删去, node_parent考虑加入p_next_left/right_leaves

            //                 if (node->aggr_len >= aggr_len_th && sibling->aggr_len >= aggr_len_th) {

            //                     shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
            //                     shared_ptr<PktMetaDataArrayOutput > p1 = make_shared<PktMetaDataArrayOutput >(sibling->aggr_flow.get_mts(), sibling->aggr_vol);
                                
            //                     if (p_output) p_output->push(p0);
            //                     if (p_output) p_output->push(p1);

            //                     sem_wait(&semaphore);
            //                     curr_aggr_pkt_len += node->aggr_vol;
            //                     curr_aggr_pkt_len += sibling->aggr_vol;
            //                     sem_post(&semaphore);

            //                 } else {

            //                     node_parent->aggr_flow.insert(node->aggr_flow);
            //                     node_parent->aggr_flow.insert(sibling->aggr_flow);

            //                     node_parent->is_end = true;
            //                     node_parent->aggr_len = node->aggr_len + sibling->aggr_len; 
            //                     node_parent->aggr_vol = node->aggr_vol + sibling->aggr_vol; 
                                
            //                     // cout << 1 << endl;

            //                     IPTrieLeaves::accessor acc;
            //                     if (!next_all_leaves.find(acc, node_parent)) next_all_leaves.insert(acc, node_parent);

            //                 }

            //             } else { // 当前的左叶子节点node不具有兄弟节点sibling

            //                 if (node->aggr_len >= aggr_len_th) {
                                
            //                     shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
            //                     if (p_output) p_output->push(p0);

            //                     sem_wait(&semaphore);
            //                     curr_aggr_pkt_len += node->aggr_vol;
            //                     sem_post(&semaphore);


            //                 } else {

            //                     // 聚合数量不够, 将数据传递至父节点, 继续聚合
            //                     // node_parent加入next_all_leaves

            //                     node_parent->aggr_flow.insert(node->aggr_flow);

            //                     node_parent->is_end = true;
            //                     node_parent->aggr_len = node->aggr_len;
            //                     node_parent->aggr_vol = node->aggr_vol; 

            //                     IPTrieLeaves::accessor acc;
            //                     if (!next_all_leaves.find(acc, node_parent)) next_all_leaves.insert(acc, node_parent);

            //                 }

            //             }
                    
            //         } else {

            //             if (node->aggr_len != 0) { 
                            
            //                 shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
            //                 if (p_output) p_output->push(p0);

            //                 sem_wait(&semaphore);
            //                 curr_aggr_pkt_len += node->aggr_vol;
            //                 sem_post(&semaphore);

            //             }

            //         }

            //     });

            // };

            // auto aggr_right_leaves = [&, this] () {
            
            //     tbb::parallel_for_each(right_leaves.begin(), right_leaves.end(), [&] (auto && item) {

            //         shared_ptr<TrieNode > node = item.first;

            //         if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {

            //             if (node->aggr_len >= aggr_len_th) {

            //                 shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
            //                 if (p_output) p_output->push(p0);

            //                 sem_wait(&semaphore);
            //                 curr_aggr_pkt_len += node->aggr_vol;
            //                 sem_post(&semaphore);

            //             } else { 

            //                 node_parent->aggr_flow.insert(node->aggr_flow);
                                
            //                 node_parent->is_end = true;
            //                 node_parent->aggr_len = node->aggr_len;
            //                 node_parent->aggr_vol = node->aggr_vol; 

            //                 IPTrieLeaves::accessor acc;
            //                 if (!next_all_leaves.find(acc, node_parent)) next_all_leaves.insert(acc, node_parent);

            //             }

            //         } else {

            //             if (node->aggr_len != 0) { 
                            
            //                 shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
            //                 if (p_output) p_output->push(p0);

            //                 sem_wait(&semaphore);
            //                 curr_aggr_pkt_len += node->aggr_vol;
            //                 sem_post(&semaphore);
                            
            //             }

            //         }

            //     });
            
            // };

            // try {

            //     tbb::parallel_invoke(
            //         [&]() { aggr_left_leaves(); },
            //         [&]() { aggr_right_leaves(); }
            //     );

            // } catch (const std::exception& e) {
                
            //     std::cerr << "Error during aggr leaves: " << e.what() << std::endl;
            
            // }

            for (const auto & node : next_all_leaves) {

                if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {
               
                    if (node->type == 0) { 
                        
                        // 不论当前左节点的右兄弟情况如何, 当前左节点加入next_left_leaves
                        next_left_leaves.insert(node);
                    
                    } else if (node->type == 1) {
                        
                        shared_ptr<TrieNode > sibling = node_parent->children[0];

                        // sibling && !sibling-is_end代表聚合没有进行到当前右节点的左兄弟, 当前右节点可以加入next_right_leaves
                        // !sibling代表当前右节点本身没有左兄弟
                        if ((sibling && !sibling->is_end) || !sibling) {
                            
                            next_right_leaves.insert(node);

                        }

                    }

                } else {

                    // 聚合至aggr_bound的节点不会加入至next_all_leaves
                    // 当前节点已经位于aggr_bound, 直接向output添加聚合的结果
                    if (node->aggr_len != 0) { 
                        
                        shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
                        if (p_output) p_output->push(p0);

                        curr_aggr_pkt_len += node->aggr_vol;
                    
                    }

                }

            }

            // auto add2leaves = [&, this] (const shared_ptr<TrieNode > & node) -> void {

            //     if (shared_ptr<TrieNode > node_parent = node->parent.lock()) {
               
            //         if (node->type == 0) { 
                        
            //             // 不论当前左节点的右兄弟情况如何, 当前左节点加入next_left_leaves
            //             IPTrieLeaves::accessor l_acc;
            //             if (!next_left_leaves.find(l_acc, node)) next_left_leaves.insert(l_acc, node);
                    
            //         } else if (node->type == 1) {
                        
            //             shared_ptr<TrieNode > sibling = node_parent->children[0];

            //             // sibling && !sibling-is_end代表聚合没有进行到当前右节点的左兄弟, 当前右节点可以加入next_right_leaves
            //             // !sibling代表当前右节点本身没有左兄弟
            //             if ((sibling && !sibling->is_end) || !sibling) {
                            
            //                 IPTrieLeaves::accessor r_acc;
            //                 if (!next_right_leaves.find(r_acc, node)) next_right_leaves.insert(r_acc, node);

            //             }

            //         }

            //     } else {

            //         // 聚合至aggr_bound的节点不会加入至next_all_leaves
            //         // 当前节点已经位于aggr_bound, 直接向output添加聚合的结果
            //         if (node->aggr_len != 0) { 
                        
            //             shared_ptr<PktMetaDataArrayOutput > p0 = make_shared<PktMetaDataArrayOutput >(node->aggr_flow.get_mts(), node->aggr_vol);
                                
            //             if (p_output) p_output->push(p0);
                        
            //             sem_wait(&semaphore);
            //             curr_aggr_pkt_len += node->aggr_vol;
            //             sem_post(&semaphore);
                    
            //         }

            //     }

            // };

            // tbb::parallel_for_each(next_all_leaves.begin(), next_all_leaves.end(), [&] (auto && item) {

            //     shared_ptr<TrieNode > node = item.first;
            //     add2leaves(node);

            // });

            // 移动语义, 资源转移, 不会增加shared_ptr中的引用计数
            left_leaves = move(next_left_leaves);
            right_leaves = move(next_right_leaves);

        }

        return curr_aggr_pkt_len;

    }

};

using FlowTable = tbb::concurrent_hash_map<FlowID, FlowEntry, FlowIDHashCompare >;

}