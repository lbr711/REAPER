#pragma once

#include <torch/torch.h>
#include <torch/script.h>
// #include "dpdkAppUtility.hpp"
#include "inspectorWorker.hpp"
#include "aggregatorWorker.hpp"

namespace Reaper
{

class ConfigReaper;
class InspectorWorkerThread;
class AggregatorWorkerThread;


struct DetectorThreadParam final {

    // report detection result
    bool tracing_mode = true;
    double_t report_interval = 5.0;

    // Pre-processing
    uint32_t trunc_flow_len = 1000;
    uint32_t slice_len = 40;
    uint32_t stride = slice_len >> 2;

    // Loading Model
    string model = "1001";
    string aggr_model_path = "../models/1001_aggr.pt";
    string long_model_path = "../models/1001_long.pt";

    void inline display_params() const {

        printf("[ ***DetectorThreadParam*** ]\n");

        if (tracing_mode) printf("Tracing Mode is Up, Report Interval: %4.4lf.\n", report_interval);
        else printf("Tracing Mode is Down.\n"); 

        printf("Truncation Length for Flow: %d.\n", trunc_flow_len);

        printf("Slice Length: %d.\n", slice_len);
        printf("Slice Stride: %d.\n", stride);

        printf("Deployed Aggr Flow Model from: %s.\n", aggr_model_path.c_str());
        printf("Deployed Long Flow Model from: %s.\n", long_model_path.c_str());

    }

};

class DetectorWorkerThread final : pcpp::DpdkWorkerThread {

    friend class ConfigReaper;

private:

    volatile bool m_stop = false;

    cpu_core_id_t m_core_id;

    shared_ptr<DetectorThreadParam > p_detector_param; // load_param_json 初始化

    torch::jit::script::Module long_model; // run方法中初始化, 在while循环开始前
    torch::jit::script::Module aggr_model; // run方法中初始化, 在while循环开始前

    // 1001 ******************************************************************************************************
    // aggr
    torch::Tensor aggr_scale_ = torch::tensor({2.97384503e-08, 5.04719124e-05, 1.38765157e-06}, torch::kFloat64);
    torch::Tensor aggr_min_ = torch::tensor({0.0, -0.00141321, -0.54635728}, torch::kFloat64);
    // long
    torch::Tensor long_scale_ = torch::tensor({5.54635692e-08, 4.27789185e-05, 1.38765157e-06}, torch::kFloat64);
    torch::Tensor long_min_ = torch::tensor({2.93956917e-06, -1.02669405e-03, -5.46357276e-01}, torch::kFloat64);
    // ***********************************************************************************************************

    vector<torch::jit::IValue > long_inference_inputs; // 临时变量
    vector<torch::jit::IValue > aggr_inference_inputs; // 临时变量

    // vector<double > kl_losses;  

    uint32_t sum_inference_pkt_len = 0;
    double_t inference_active_time;

    uint32_t sum_pre_pkt_len = 0;
    double_t pre_active_time;

    vector<double_t > inference_latency;

    // shared_ptr<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > > p_short_aggr_queue;
    // shared_ptr<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > > p_long_queue;

    // 与detector关联的一系列inspectors, aggregators
    vector<shared_ptr<InspectorWorkerThread > > p_inspector_vec;
    vector<shared_ptr<AggregatorWorkerThread > > p_aggregator_vec;


public:

    DetectorWorkerThread(const vector<shared_ptr<InspectorWorkerThread > > & _pv0, 
                            const vector<shared_ptr<AggregatorWorkerThread > > & _pv1): p_inspector_vec(_pv0), p_aggregator_vec(_pv1) {

        // p_long_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();
        // p_short_aggr_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();

        // for (const auto & p_inspector : p_inspector_vec) {

        //     p_inspector->p_long_queue = p_long_queue;

        // }

        // for (const auto & p_aggregator : p_aggregator_vec) {

        //     p_aggregator->p_short_aggr_queue = p_short_aggr_queue;

        // }

    }

    DetectorWorkerThread(const vector<shared_ptr<InspectorWorkerThread > > & _pv0, 
                            const vector<shared_ptr<AggregatorWorkerThread > > & _pv1,
                            const json & _j): p_inspector_vec(_pv0), p_aggregator_vec(_pv1) {

        load_params_via_json(_j);

        // p_long_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();
        // p_short_aggr_queue = make_shared<tbb::concurrent_queue<shared_ptr<PktMetaDataArrayOutput > > >();

        // for (const auto & p_inspector : p_inspector_vec) {

        //     p_inspector->p_long_queue = p_long_queue;

        // }

        // for (const auto & p_aggregator : p_aggregator_vec) {

        //     p_aggregator->p_short_aggr_queue = p_short_aggr_queue;

        // }

    }

    virtual bool run(uint32_t coreId) override;

    virtual void stop() override;

    virtual uint32_t getCoreId() const override {return m_core_id;}

    void load_params_via_json(const json & jin);

    pair<double_t, double_t > get_overall_performance() const;

};

}