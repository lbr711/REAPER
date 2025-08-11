#pragma once

#include <torch/torch.h>
#include <torch/script.h>
#include "dpdkAppUtility.hpp"
// #include "assemblerWorker.hpp"

namespace Reaper
{

class ConfigReaper;
class DetectorWorkerThread;
class AssemblerWorkerThread;
class AggregatorWorkerThread;

struct InspectorThreadParam final {

    bool tracing_mode = true;
    double_t report_interval = 5.0;

    // params for net flow completion 
    uint64_t idle_time_out = 16e6;
    uint64_t hard_time_out = 50e6;
    uint32_t trunc_flow_len = 1e3;
    uint32_t long_th = 40;


    void inline display_params() const {

        printf("[ ***InspectorThreadParam*** ]\n");

        if (tracing_mode) printf("Tracing Mode is Up, Report Interval: %4.4lf.\n", report_interval);
        else printf("Tracing Mode is Down.\n");

        printf("Idle/Hard Timeout for Flow Table is %ld ns/%ld ns.\n", idle_time_out, hard_time_out);
        printf("Truncation Length for Flow: %d.\n", trunc_flow_len);
        printf("Long Flow Threshold: %d.\n", long_th);

    }

};

class InspectorWorkerThread final : pcpp::DpdkWorkerThread {

    friend class ConfigReaper;
    friend class DetectorWorkerThread;
    friend class AggregatorWorkerThread;

private:

    volatile bool m_stop = false;

    cpu_core_id_t m_core_id;

    shared_ptr<InspectorThreadParam > p_inspector_param;
    
    // 与inspector关联的一系列assemblers
    vector<shared_ptr<AssemblerWorkerThread > > p_assembler_vec;

    unique_ptr<tbb::concurrent_unordered_set<FlowID, FlowIDHash > > historical_pool;
    unique_ptr<tbb::concurrent_unordered_set<FlowID, FlowIDHash > > next_historical_pool;

    // unique_ptr<unordered_set<FlowID, FlowIDHash > > historical_pool;
    // unique_ptr<unordered_set<FlowID, FlowIDHash > > next_historical_pool;

    // inspector <-> aggregator
    tbb::concurrent_queue<pair<FlowID, FlowEntry > > short_flow_queue;

    // inspector <-> detector (set by detector)
    tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > long_queue;


public:

    InspectorWorkerThread(const vector<shared_ptr<AssemblerWorkerThread > > & _pv): p_assembler_vec(_pv) {

    }

    InspectorWorkerThread(const vector<shared_ptr<AssemblerWorkerThread > > & _pv, const json & _j): p_assembler_vec(_pv) {

        load_params_via_json(_j);

    }

    virtual bool run(uint32_t coreId) override;

    virtual void stop() override;

    virtual uint32_t getCoreId() const override {return m_core_id;}

    void load_params_via_json(const json & jin);

};

}