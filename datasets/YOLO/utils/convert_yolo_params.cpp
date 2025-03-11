#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <filesystem>
#include <limits>
#include <algorithm>
#include <cstdint>
#include <cmath>

namespace fs = std::filesystem;

#define MAX_SCALE 63

std::tuple<std::string, std::vector<float>, std::vector<size_t>> extract_variable_info(const std::string &block_text) {
    
    std::regex var_name_regex(R"(float\s+(layer\d+_\w+)\s*((\[\d+\])+))");
    std::smatch var_name_match;
    if (!std::regex_search(block_text, var_name_match, var_name_regex)) {
        return { "", {}, {} };
    }

    std::string var_name = var_name_match[1];
    std::string dimensions_str = var_name_match[2];
    if (var_name_match[3].matched) {
        dimensions_str += var_name_match[3];
    }

    std::vector<size_t> dimensions;
    std::regex dim_regex(R"(\d+)");
    auto dims_begin = std::sregex_iterator(dimensions_str.begin(), dimensions_str.end(), dim_regex);
    auto dims_end = std::sregex_iterator();

    for (std::sregex_iterator i = dims_begin; i != dims_end; ++i) {
        std::smatch match = *i;
        dimensions.push_back(std::stoul(match.str()));
    }

    size_t init_start = block_text.find('{');
    size_t init_end = block_text.rfind('}');
    if (init_start == std::string::npos || init_end == std::string::npos) {
        return { var_name, {}, dimensions };
    }
    std::string initializer_text = block_text.substr(init_start, init_end - init_start + 1);

    // Remove 'f' suffix from numbers
    initializer_text = std::regex_replace(initializer_text, std::regex(R"(([0-9]+\.[0-9]+)f)"), "$1");

    // Remove extra spaces and newlines
    initializer_text.erase(std::remove(initializer_text.begin(), initializer_text.end(), '\n'), initializer_text.end());
    initializer_text.erase(std::remove(initializer_text.begin(), initializer_text.end(), ' '), initializer_text.end());

    // Extract float values
    std::vector<float> array_data;
    std::regex float_regex(R"([-+]?[0-9]*\.?[0-9]+)");
    auto floats_begin = std::sregex_iterator(initializer_text.begin(), initializer_text.end(), float_regex);
    auto floats_end = std::sregex_iterator();

    for (std::sregex_iterator i = floats_begin; i != floats_end; ++i) {
        std::smatch match = *i;
        array_data.push_back(std::stof(match.str()));
    }



    


    return { var_name, array_data, dimensions };
}

std::tuple<std::string, std::string, std::string> process_h_file(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filepath << std::endl;
        return { "", "" , ""};
    }

    std::string weights_block;
    std::string biases_block;
    std::string current_block;
    std::string norm_block;
    bool inside_block = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("float") != std::string::npos) {
            inside_block = true;
        }
        if (inside_block) {
            current_block += line;
            if (line.find("};") != std::string::npos) {
                if (current_block.find("weights") != std::string::npos) {
                    weights_block = current_block;
                } else if (current_block.find("biases") != std::string::npos) {
                    biases_block = current_block;
                } else if (current_block.find("scale") != std::string::npos) {
                    norm_block = current_block;
                }
                current_block.clear();
                inside_block = false;
            }
        }
    }
    return { weights_block, biases_block, norm_block };
}

std::pair<std::vector<int>, std::vector<float>> quantize_per_channel(const std::vector<float> &array_data, size_t num_channels, std::vector<int> sign, int layer_0) {
    std::vector<int> quantized_data;
    std::vector<float> scales;

    size_t channel_size = array_data.size() / num_channels;

    for (size_t c = 0; c < num_channels; ++c) {
        auto channel_begin = array_data.begin() + c * channel_size;
        auto channel_end = channel_begin + channel_size;

        float min_val = std::abs(*std::min_element(channel_begin, channel_end));
        float max_val = std::abs(*std::max_element(channel_begin, channel_end));

        float scale = (std::max(min_val,max_val) / 127.0f); // passer en 4 bits un jour (c'est fait)
        
        // ajouter zero point
        scales.push_back(scale);
        for (auto it = channel_begin; it != channel_end; ++it) {
            int quantized_val = std::clamp(static_cast<int>((*it /*- min_val*/) / scale),-127,127);
            int verif = *it / scale;
            if (quantized_val != verif){
                std::cout << "Erreur de quantification" << std::endl;
            }
            quantized_data.push_back(quantized_val/**sign[c]*/);
        }
    }

    return { quantized_data, scales };
}

std::vector<int> quantize_biases(const std::vector<float> &array_data, std::vector<float> &scales) { // Juste prendre valeur int en ^2 pour avoir juste un shift
    std::vector<int> quantized_data;
    
    for (size_t i = 0; i < array_data.size(); ++i) {
        int quantized_val = static_cast<int>(array_data[i] / scales[i]);
        quantized_data.push_back(quantized_val);
    }

    return quantized_data;
}

std::vector<int> float_to_poweroftwo(const std::vector<float> &array_data, std::vector<int> sign) {
    std::vector<int> q_biases;
    for (std::size_t i = 0; i < array_data.size(); ++i) {
        if (array_data[i] == 0.0f) {
            q_biases.push_back(0);
        }else{
            q_biases.push_back(std::pow(2,static_cast<int>(std::round(std::log2(array_data[i]))))/**sign[i]*/);
        }
    }
    return q_biases;
}

/*
std::vector<std::pair<int32_t, int>> calc_mult_shift(const std::vector<float>& scale_weights, const std::vector<float>& scales_norm) {
    std::vector<std::pair<int32_t, int>> mult_shift;
    for (size_t i = 0; i < scale_weights.size(); ++i) {
        float scale = scale_weights[i] * scales_norm[i];
        int8_t shift = 0;

        while (scale < 1.0f && shift < 31) {
            scale *= 2.0f;
            shift++;
        }
        int32_t quantized_scale = static_cast<int32_t>(scale * (1 << 30));
        mult_shift.push_back({quantized_scale, shift});
    }


    return mult_shift;
}
*/

std::vector<std::pair<int32_t, int>> calc_mult_shift(const std::vector<float>& scale_weights, const std::vector<float>& scales_norm) {
    std::vector<std::pair<int32_t, int>> mult_shift;
    for (size_t i = 0; i < scale_weights.size(); ++i) {
        float scale = scale_weights[i] * scales_norm[i];
        int8_t shift = 0;
        if (scale < 0){
            scale = -scale;
        }
        while (scale < MAX_SCALE/2 && shift < 31) {
            scale *= 2.0f;
            shift++;
        }
        int32_t quantized_scale = static_cast<int32_t>(scale);
        mult_shift.push_back({quantized_scale, shift});
    }


    return mult_shift;
}

std::vector<std::pair<int32_t, int>> calc_mult_shift_scale(const std::vector<float>& scale_weights){
    std::vector<std::pair<int32_t, int>> mult_shift;
    for (size_t i = 0; i < scale_weights.size(); ++i) {
        float scale = scale_weights[i];//*0.0866; //scale à parti de l'output de la couche norm
        int8_t shift = 0;
        while (scale < MAX_SCALE/2 && shift < 31) {
            scale *= 2.0f;
            shift++;
        }
        int32_t quantized_scale = static_cast<int32_t>(scale);
        mult_shift.push_back({quantized_scale, shift});
    }


    return mult_shift;
}


void write_data_recursive(std::ofstream &outfile, const std::vector<int> &data, const std::vector<size_t> &dimensions, size_t dim_index, size_t &data_index) {
    outfile << "{";
    if (dim_index == dimensions.size() - 2) {
        for (size_t i = 0; i < dimensions[dim_index]; ++i) {
            outfile << data[data_index++];
            if (i != dimensions[dim_index] - 1) {
                outfile << ", ";
            }
        }
        
    } else {
        for (size_t i = 0; i < dimensions[dim_index]; ++i) {
            outfile << "\n";
            write_data_recursive(outfile, data, dimensions, dim_index + 1, data_index);
            
            if (i != dimensions[dim_index] - 1) {
                outfile << ", ";
                
                
            }
            
        }
        
    }
    outfile << "}";
    
}

void write_h_file(  const std::string &output_folder, const std::string &filename, const std::string &var_name,
                    const std::string &var_name_b, const std::vector<int> &quantized_data,
                    const std::vector<int> &quantized_data_b, const std::vector<float> &scales,
                    const std::vector<size_t> &dimensions, std::vector<std::pair<int32_t ,int >> multshift, int layer_number) {

    
    std::string output_filepath = output_folder + "/" + filename;
    std::ofstream outfile(output_filepath);
    if (!outfile.is_open()) {
        std::cerr << "Unable to open file for writing: " << output_filepath << std::endl;
        return;
    }

    int rescale_shift_output = 0; //shift à ajouter pour récupérer des valeurs entre -127 et 127 à la sortie de la couche norm.
    /*                           // Amélioration: Inférence pour chaque couche pour trouver le shift optimal
    if (layer_number == 0){
        rescale_shift_output = 0; //4
    }else if (layer_number == 1){
        rescale_shift_output = -2;
    }else if (layer_number == 2){
        rescale_shift_output = -3;
    }else if (layer_number == 3){
        rescale_shift_output = -2;
    }else if (layer_number == 4){
        rescale_shift_output = -2;
    }else if (layer_number == 5){
        rescale_shift_output = -1;
    }else if (layer_number == 7){
        rescale_shift_output = 12;//13
    }
    */
    std::string quantized_var_name = var_name + "_q";
    std::string quantized_var_name_b = var_name_b + "_q";
    outfile << "/***************************************/\n";
    outfile << "/*  Fichier .h généré automatiquement  */\n";
    outfile << "/*     par convert_yolo_params.cpp     */\n";
    outfile << "/***************************************/\n\n";
    outfile << "#include <stdint.h>\n\n";

    outfile << "typedef struct {\n";
    outfile << "    int32_t mult;\n";
    outfile << "    int32_t shift;\n";
    outfile << "} int32_8_t;\n\n";



    outfile << "int8_t " << quantized_var_name << "[";
    for (size_t i = 0; i < dimensions.size()-1; ++i) {
        outfile << dimensions[i];
        if (i != dimensions.size() - 2) {
            outfile << "][";
        }
    }
    outfile << "] = ";

    size_t data_index = 0;
    write_data_recursive(outfile, quantized_data, dimensions, 0, data_index);

    outfile << ";\n\n";

    outfile << "float " << var_name << "_scales["<<dimensions[0]<<"] = {";
    for (size_t i = 0; i < scales.size(); ++i) {
        outfile << scales[i];
        if (i != scales.size() - 1) {
            outfile << ", ";
        }
    }
    outfile << "};\n\n";

    outfile << "int16_t " << quantized_var_name_b << "["<<dimensions[0]<<"] = {";
    for (size_t i = 0; i < quantized_data_b.size(); ++i) {
        outfile << quantized_data_b[i];
        if (i != quantized_data_b.size() - 1) {
            outfile << ", ";
        }
    }

    outfile << "};\n\n";
/*
    outfile << "float " << quantized_var_name_b << "_scales["<<1<<"] = {";
    outfile << scales_b;
    outfile << "};\n\n";
*/
    outfile << "int32_8_t mult_shift["<<dimensions[0]<<"] = {";
    for (size_t i = 0; i < multshift.size(); ++i) {
        outfile << "{" << multshift[i].first << ", " << multshift[i].second + rescale_shift_output << "}";
        if (i != multshift.size() - 1) {
            outfile << ", ";
        }
    }
    outfile << "};\n\n";


    outfile.close();
}

std::vector<int> get_sign(std::vector<float> array_data, int empty, int dimension){
    std::vector<int> sign;
    for (std::size_t i = 0; i < dimension; ++i) {
        if (empty==1){
            sign.push_back(1);
        }else{
            if (array_data[i] < 0.0f) {
                sign.push_back(-1);
            }else{
                sign.push_back(1);
            }
        }
    }
    return sign;
}


int main(int argc, char *argv[]) {
    std::string params_folder = "yolo_params";
    std::string output_folder = "quantized_params";
    if (argc > 1) {
        params_folder = argv[1];
    }
    if (argc > 2) {
        output_folder = argv[2];
    }

    fs::create_directory(output_folder);
    int layer_number=99;
    std::vector<std::pair<int32_t, int>> mult_shift;
    for (const auto &entry : fs::directory_iterator(params_folder)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            std::smatch match;
            std::regex layer_regex(R"(layer(\d+))");
            if (std::regex_search(filename, match, layer_regex)) {
                layer_number = std::stoi(match[1]);
                //std::cout << "Layer number: " << layer_number << std::endl;
            }
            
            if (filename.rfind("layer", 0) == 0 && entry.path().extension() == ".h") {
                std::string filepath = entry.path().string();
                std::cout << "Processing file: " << filepath << std::endl;
                auto [block_w, block_b, block_n] = process_h_file(filepath);
                if (!block_w.empty() and !block_b.empty()) {
                    auto [var_name, array_data, dimensions] = extract_variable_info(block_w);
                    auto [var_name_b, array_data_b, dimensions_b] = extract_variable_info(block_b);
                    auto [var_name_n, array_data_n, dimensions_n] = extract_variable_info(block_n);
                    std::vector<int> sign;
                    if (array_data_n.empty()){
                        sign = get_sign(array_data_n, 1, dimensions[0]);
                    }else{
                        sign = get_sign(array_data_n, 0, dimensions[0]);
                    }
                    //std::cout<<" Dim et sign.size() "<<dimensions[0]<<" "<<sign.size()<<std::endl;

                    if (!var_name.empty() && !array_data.empty() && !var_name_b.empty() && !array_data_b.empty()) {

                        size_t num_channels = dimensions[0]; // Assuming the first dimension is the number of channels
                        std::vector<int> quantized_data;
                        std::vector<float> scales;
                        if (layer_number==0){
                            auto [quantized_data_, scales_] = quantize_per_channel(array_data, num_channels, sign, 1);
                            quantized_data = quantized_data_;
                            scales = scales_;
                        }else{
                            
                            auto [quantized_data_, scales_] = quantize_per_channel(array_data, num_channels, sign, 0);
                            quantized_data = quantized_data_;
                            scales = scales_;
                        }
                        
                            //auto [quantized_data_b, scales_b] = quantize_biases(array_data_b);
                        auto quantized_data_b = quantize_biases(array_data_b, scales);
                        
                        if (array_data_n.size() == 0){
                            mult_shift = calc_mult_shift_scale(scales);
                        }else{
                            //std::cout<<array_data_n.size()<<std::endl;
                            //std::cout<<scales.size()<<std::endl;
                            mult_shift = calc_mult_shift(array_data_n, scales);
                        }
                        
                        write_h_file(output_folder, "q_" + filename, var_name, var_name_b, quantized_data, quantized_data_b, scales, dimensions, mult_shift, layer_number);
                        
                    }
                } else {
                    std::cout << "No block found for keyword '"
                              << "' in file " << filepath << std::endl;
                }

                
            }
        }
    }
    return 0;
}