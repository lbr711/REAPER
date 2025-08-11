#pragma once

#include <torch/torch.h>
#include <torch/script.h>
#include "dpdkAppUtility.hpp"
// #include "assemblerWorker.hpp"

namespace Reaper
{

class ConfigReaper;
class DetectorWorkerThread;
class InspectorWorkerThread;

struct AggregatorThreadParam final {

    // report detection result
    bool tracing_mode = true;
    double_t report_interval = 5.0;

    // params for dynamic IP prefix aggregation
    uint32_t shortest_prefix_len = 24;
    uint32_t aggr_len_th = 5e2;
    uint32_t aggr_cycle = 1e4;

    uint32_t trunc_flow_len = 1e3;

    void inline display_params() const {

        printf("[ ***AggregatorThreadParam*** ]\n");

        if (tracing_mode) printf("Tracing Mode is Up, Report Interval: %4.4lf.\n", report_interval);
        else printf("Tracing Mode is Down.\n");

        printf("Truncation Length for Flow: %d.\n", trunc_flow_len);

        printf("Shortest IP Prefix Length: %d.\n", shortest_prefix_len);
        printf("Aggregation Flow Length Threshold: %d.\n", aggr_len_th);
        printf("Aggregation Cycle: %d.\n", aggr_cycle);

    }

};

class AggregatorWorkerThread final : pcpp::DpdkWorkerThread {

    friend class ConfigReaper;
    friend class DetectorWorkerThread;

private:

    volatile bool m_stop = false;

    cpu_core_id_t m_core_id;

    shared_ptr<AggregatorThreadParam > p_aggregator_param;

    uint32_t sum_create_pkt_len = 0;
    vector<double_t > create_throughput;
    double_t create_active_time;

    uint32_t sum_aggr_pkt_len = 0;
    vector<double_t > aggr_throughput;
    double_t aggr_active_time;

    // inspector <-> aggregator
    // 与aggregator关联的一系列inspectors, visit their short_flow_queue
    vector<shared_ptr<InspectorWorkerThread > > p_inspector_vec;

    tbb::concurrent_queue<unique_ptr<IPTrie > > ip_trie_queue;

    // aggregator <-> detector
    shared_ptr<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > > p_short_aggr_queue;

    void aggregator_exec();

public:

    AggregatorWorkerThread(const vector<shared_ptr<InspectorWorkerThread > > & _pv): p_inspector_vec(_pv) {

        p_short_aggr_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();

    }

    AggregatorWorkerThread(const vector<shared_ptr<InspectorWorkerThread > > & _pv, const json & _j): p_inspector_vec(_pv) {

        p_short_aggr_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();
        
        load_params_via_json(_j);

    }

    virtual bool run(uint32_t coreId) override;

    virtual void stop() override;

    virtual uint32_t getCoreId() const override {return m_core_id;}

    void load_params_via_json(const json & jin);

    pair<double_t, double_t > get_overall_performance() const;

};

}