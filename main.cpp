// google提供的gflags库用于处理命令行参数
#include <gflags/gflags.h>

#include "runtime/configReaper.hpp"
#include "utility.hpp"


using namespace std;
using namespace Reaper;

// gflags库, 此时gflags库创建了一个标志flag, 即config，在命令行中通过"-config"使用该标志

// 而该标志可取的值的类型为字符串, 并在程序中可使用变量FLAGS_config进行访问, 该字符串具体为一个json文件的相对路径

// FLAGS_config的默认值是"../Parameters.json"

DEFINE_string(reaper_params, "../defaultParameters.json", "Load Parameters of Reaper via JSON file.");


int main(int argc, char** argv) {
    
    __START_TIMER__
    
    // 解析标志位, 这里仅须解析"-config", 如果该标志位存在, 则将该值覆盖默认值"../defaultParameters.json"
    google::ParseCommandLineFlags(&argc, &argv, true);
    
    json parameter_j;
    
    try {

        ifstream fin(FLAGS_reaper_params, ios::in);
        fin >> parameter_j;
    
    } catch (exception & e) {
        
        FATAL_ERROR(e.what());
    
    }

    const shared_ptr<ConfigReaper> p_reaper = make_shared<ConfigReaper>(parameter_j);

    p_reaper->enable_reaper();
    
    __STOP_TIMER__
  
    __PRINT_EXE_TIME__
   
}

