#pragma once

#include "./dpdkAppUtility.hpp"

using namespace std;
using namespace pcpp;

namespace Reaper
{

// farward declaration
class AssemblerWorkerThread;
class ConfigReaper;

struct ParserThreadParam final {

    double_t report_interval = 5.0;
    
    #define MAX_PKT_META_RING_QUEUE_SIZE (1 << 25)
    size_t pkt_meta_ring_queue_size = 1 << 24;
    
    #define MAX_BURST_PKT_NUM (1 << 16)
    size_t burst_pkt_num = 64;

    bool tracing_mode = true; 

    ParserThreadParam() = default;
    virtual ~ParserThreadParam() {}
    ParserThreadParam & operator=(const ParserThreadParam &) = delete;
    ParserThreadParam(const ParserThreadParam &) = delete;

    void inline display_params() const {

        printf("[ ***ParserThreadParam*** ]\n");

        printf("Packet MetaData Ring Queue Size: %ld\n", pkt_meta_ring_queue_size);
        printf("Number of Burst Packets: %ld\n", burst_pkt_num);
        if (tracing_mode) printf("Tracing Mode is Up, Report Interval: %4.4lf\n", report_interval);
        else printf("Tracing Mode is Down\n");

    }
};

class ParserWorkerThread final : public DpdkWorkerThread {

    friend class AssemblerWorkerThread; // AssemblerWorkerThread友元类, 能够访问本类的私有成员
    friend class ConfigReaper;
    
private:

    const shared_ptr<DpdkDevMap > p_dpdk_dev_map;
    shared_ptr<ParserThreadParam > p_parser_param;

    mutable bool m_stop = false;
    cpu_core_id_t m_core_id;

    // mutable表示: 可被const条件修饰的成员方法修改
    mutable vector<size_t > dpdk_dev_parsed_pkt_len;
    mutable vector<size_t > dpdk_dev_parsed_pkt_num;
    mutable vector<size_t > dpdk_dev_sum_parsed_pkt_len;
    mutable vector<size_t > dpdk_dev_sum_parsed_pkt_num;

    mutable double_t parser_start_time, parser_end_time;

    // Ring Queue Access
    shared_ptr<PacketMetaData[]> ring_queue;

    size_t ring_queue_size = 0;
    volatile size_t ring_queue_begin = 0; // next dequeue index 
    volatile size_t ring_queue_end = 0; // next enqueue index
    volatile size_t ring_queue_count = 0; // number of elements in ring queue 
    size_t ring_queue_dropped = 0; // number of elements to be dropped when ring queue is full

    mutable sem_t semaphore;
    void inline acquire_semaphore() const {sem_wait(&semaphore);}
    void inline release_semaphore() const {sem_post(&semaphore);}

    // Statistic Report Thread
    void stat_tracer_exec() const;

public:

    ParserWorkerThread(const shared_ptr<DpdkDevMap> p_m, const json & j_p): p_dpdk_dev_map(p_m), m_core_id(p_m != nullptr ? p_m->core_id : MAX_NUM_OF_CORES + 1) {

        if (p_dpdk_dev_map == nullptr) {
            
            FATAL_ERROR("Cannot Initialize Parser Thread with NULL Map of DPDK Devices.");

        }

        sem_init(&semaphore, 0, 1);

        if (j_p.size()) {

            load_params_via_json(j_p);

        }

        dpdk_dev_parsed_pkt_len.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_parsed_pkt_num.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_sum_parsed_pkt_len.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_sum_parsed_pkt_num.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        
    }  

    ParserWorkerThread(const shared_ptr<DpdkDevMap> p_m): p_dpdk_dev_map(p_m), m_core_id(p_m != nullptr ? p_m->core_id : MAX_NUM_OF_CORES + 1) {

        if (p_dpdk_dev_map == nullptr) {
            
            FATAL_ERROR("Cannot Initialize Parser Thread with NULL Map of DPDK Devices.");

        }

        sem_init(&semaphore, 0, 1);

        dpdk_dev_parsed_pkt_len.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_parsed_pkt_num.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_sum_parsed_pkt_len.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        dpdk_dev_sum_parsed_pkt_num.resize(p_dpdk_dev_map->dpdk_dev_map.size(), 0);
        
    }  

    virtual ~ParserWorkerThread() {}
    ParserWorkerThread & operator=(const ParserWorkerThread &) = delete;
    ParserWorkerThread(const ParserWorkerThread &) = delete;

    virtual bool run(uint32_t coreId) override;

    virtual void stop() override;

    virtual uint32_t getCoreId() const override {return m_core_id;}

    void load_params_via_json(const json & jin);

    pair<double_t, double_t> get_overall_performance() const;

};

}

