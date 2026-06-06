#include "LDPC.hh"
#include <unistd.h>
namespace SimpleSSD{
namespace ERROR{

    LDPC::LDPC(){
        read_csv();
    }

    void LDPC::read_csv(){
        // char path[_MAX_PATH];
        // getcwd(path, _MAX_PATH);
        // std::string pathstr = path;
        // int pos = pathstr.find("simplessd-standalone");
        // pathstr = pathstr.substr(0, pos + 21);
        std::string model_path;
        model_path = getcwd(NULL, 0);
        
        std::string file_name = "./simplessd/error/config/ldpc.csv";
        std::ifstream csv_data(file_name, std::ios::in);
        std::string line;

        if (!csv_data.is_open())
        {
            std::cout << file_name << " Error: opening file fail" << std::endl;
            std::exit(1);
        }
        // std::cout<<"LDPC"<<std::endl;

        std::istringstream sin;         // Read the entire line into istringstream
        std::vector<std::string> words; // String vector for CSV fields
        std::string word;

        // Read header row
        std::getline(csv_data, line);
        // Read data rows
        int layer_ = 0;
        ldpcType ldpc_;
        while (std::getline(csv_data, line))
        {
            sin.clear();
            sin.str(line);
            words.clear();
            words.shrink_to_fit();
            int num = 0;
            while (std::getline(sin, word, ',')) // Read comma-delimited fields from sin
            {
                switch (num)
                {
                case 0:
                    ldpc_.bit = std::stoi(word);
                    break;
                case 1:
                    if(word == "NA"){
                        ldpc_.UBER = 0;
                    }
                    else{
                        ldpc_.UBER = std::stof(word);
                    }
                    break;
                case 2:
                    ldpc_.latency = std::stof(word) * 1000000;
                    num = -1;
                    ldpcs.push_back(ldpc_);
                    break;
                
                default:
                    break;
                }
                num++;
                // std::cout << atol(word.c_str());
            }
            layer_++;
            // do something。。�?
        }
        csv_data.close();
    }

    double LDPC::get_UBER(uint32_t b){
        if(b <= ldpcs[0].bit){
            return ldpcs[0].UBER;
        }
        for(uint32_t i=1;i<ldpcs.size();i++){
            if(b < ldpcs[i].bit){
                return ldpcs[i-1].UBER;
            }
        }
        return ldpcs[ldpcs.size()-1].UBER;
    }

    uint64_t LDPC::get_latency(uint32_t b){
        if(b <= ldpcs[0].bit){
            return ldpcs[0].latency;
        }
        for(uint32_t i=1;i<ldpcs.size();i++){
            if(b < ldpcs[i].bit){
                return ldpcs[i-1].latency;
            }
        }
        return uint64_t(ldpcs[ldpcs.size()-1].latency);
    }
    
}

}

