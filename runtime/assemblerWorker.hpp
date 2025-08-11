#pragma once

#include <torch/torch.h>
#include <torch/script.h>
#include "dpdkAppUtility.hpp"

namespace Reaper
{

class ParserWorkerThread;
class InspectorWorkerThread;
class ConfigReaper;

struct AssemblerThreadParam final {

    bool tracing_mode = true;
    double_t report_interval = 5.0;
    uint32_t pause_time = 5e4;

    // params for pkt meta buffer
    #define MAX_PKT_META_BUFFER_SIZE (1 << 25)
    size_t pkt_meta_buffer_size = 2e6;
    size_t max_fetch = 1 << 17;

    uint32_t trunc_flow_len = 1e3;


    void inline display_params() const {

        printf("[ ***AssemblerThreadParam*** ]\n");

        if (tracing_mode) printf("Tracing Mode is Up, Report Interval: %4.4lf.\n", report_interval);
        else printf("Tracing Mode is Down.\n");
        printf("Thread Pause Time: %d us.\n", pause_time);

        printf("Packet Meta Buffer Size: %ld.\n", pkt_meta_buffer_size);
        printf("Maximum Fetch from Buffer at One Time: %ld.\n", max_fetch);
        printf("Truncation Length for Flow: %d.\n", trunc_flow_len);


    }

};

class AssemblerWorkerThread final : pcpp::DpdkWorkerThread {

    friend class ConfigReaper;
    friend class InspectorWorkerThread;

private:

    volatile bool m_stop = false;
    cpu_core_id_t m_core_id;

    shared_ptr<AssemblerThreadParam > p_assembler_param;

	uint32_t sum_update_pkt_len = 0;
    uint32_t sum_fetch_pkt_len = 0;
    // vector<double_t > update_throughput;
    double_t update_active_time;
    double_t fetch_active_time;

    // assembler管理的parsers, assembler线程能够通过指针访问所管理的parser线程
    vector<shared_ptr<ParserWorkerThread > > p_parser_vec;

    mutable size_t buffer_next = 0;
    shared_ptr<PacketMetaData[] > pkt_meta_buffer;

    FlowTable flow_tbl;

    // next_pool, last_pool, historical_pool, next_historical_pool作为非临时变量
    // 涉及到资源的转移, 使用unique_ptr进行管理

    // main thread
    unique_ptr<vector<FlowID > > next_pool;
    uint64_t last_enqueue_ts;

    // main <-> inspector
    tbb::concurrent_queue<unique_ptr<vector<FlowID > > > last_pool_queue;
    tbb::concurrent_queue<uint64_t > snapshot_ts_queue;


    size_t fetch_from_parser(const shared_ptr<ParserWorkerThread > pt) const;

    void update_flow_tbl();

public:

    AssemblerWorkerThread(const vector<shared_ptr<ParserWorkerThread > > & _pv): p_parser_vec(_pv) {

        next_pool = make_unique<vector<FlowID > >();

    }

    AssemblerWorkerThread(const vector<shared_ptr<ParserWorkerThread > > & _pv, const json & _j): p_parser_vec(_pv) {

        load_params_via_json(_j);

        next_pool = make_unique<vector<FlowID > >();

    }

    virtual bool run(uint32_t coreId) override;

    virtual void stop() override;

    virtual uint32_t getCoreId() const override {return m_core_id;}

    void load_params_via_json(const json & jin);

    pair<double_t, double_t > get_overall_performance() const;

};

}