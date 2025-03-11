#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <regex>
#include <sstream>
#include <algorithm>

struct Parameters {
    std::vector<std::vector<int>> weights;
    std::vector<int> biases;
    std::vector<std::vector<int>> mult_shift;
    std::vector<int> dim;
};

// Fonction pour écrire les paramètres dans un fichier CSV
void write_csv(const std::string& filename, const std::vector<std::vector<int>>& data, std::vector<int> dim) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filename << std::endl;
        return;
    }

    ///std::cout<<"data.size(): "<<data.size()<<std::endl;
    for (size_t i = 0; i < data.size(); ++i) {
        for (size_t j = 0; j < data[i].size(); ++j) {
            file << data[i][j] << ",";
        }
        file << "\n";
    }
    file.close();
}

// Fonction pour écrire les biais dans un fichier CSV
void write_norm_csv(const std::string& filename, const std::vector<std::vector<int>> mult_shift, const std::vector<int>& biases) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filename << std::endl;
        return;
    }
    for (int i = 2; i < static_cast<int>(biases.size())-1; ++i) {
        file << biases[i+1] <<","<< mult_shift[i-2][0] <<","<< mult_shift[i-2][1]<<"\n";
    }
    file << "\n";

    file.close();
}

// Fonction pour lire les blocs de paramètres à partir d'un fichier d'en-tête
std::tuple<std::string, std::string, std::string> process_h_file(const std::string &filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Unable to open file: " << filepath << std::endl;
        return { "", "" , ""};
    }

    std::string weights_block;
    std::string biases_block;
    std::string norm_block;
    std::string current_block;
    bool inside_block = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("int8_t") != std::string::npos || line.find("int16_t") != std::string::npos || line.find("int32_8_t") != std::string::npos) {
            inside_block = true;
        }
        if (inside_block) {
            current_block += line;
            if (line.find("};") != std::string::npos) {
                if (current_block.find("weights_q") != std::string::npos) {
                    weights_block = current_block;
                } else if (current_block.find("biases_q[") != std::string::npos) {
                    biases_block = current_block;
                } else if (current_block.find("mult_shift[") != std::string::npos) {
                    norm_block = current_block;
                }
                current_block.clear();
                inside_block = false;
            }
        }
    }
    return { weights_block, biases_block, norm_block };
}

// Fonction pour lire les paramètres à partir des blocs extraits
Parameters read_parameters(const std::string& filename) {
    Parameters params;
    auto [weights_block, biases_block, norm_block] = process_h_file(filename);

    // Extraire les poids
    if (!weights_block.empty()) {
        std::regex value_regex(R"([-+]?\d+)");
        auto values_begin = std::sregex_iterator(weights_block.begin(), weights_block.end(), value_regex);
        auto values_end = std::sregex_iterator();

        std::vector<int> values;
        for (std::sregex_iterator i = values_begin; i != values_end; ++i) {
            std::smatch match = *i;
            values.push_back(std::stoi(match.str()));
        }

        std::vector<std::vector<int>> organized_weights;
        size_t index = 8;
        for (size_t i = 0; i < values[4]; ++i) {
            std::vector<int> sublist;
            for (size_t j = 0; j < values[5]*values[6]*values[7]; ++j) {
                sublist.push_back(values[index++]);
            }
            organized_weights.push_back(sublist);
        }
        params.weights = organized_weights;
        params.dim={values[4],values[5],values[6],values[7]};
        //<<"params.dim: "<<values[5]<<","<<values[6]<<","<<values[7]<<std::endl;
    }

    // Extraire les biais
    if (!biases_block.empty()) {
        ///std::cout<<"biases_block: "<<biases_block<<std::endl;
        std::regex value_regex(R"([-+]?\d+)");
        auto values_begin = std::sregex_iterator(biases_block.begin(), biases_block.end(), value_regex);
        auto values_end = std::sregex_iterator();

        for (std::sregex_iterator i = values_begin; i != values_end; ++i) {
            std::smatch match = *i;
            params.biases.push_back(std::stoi(match.str()));
        }
    }

    // Extraire les valeurs de normalisation
    if (!norm_block.empty()) {
        //std::cout << "norm_block: " << norm_block << std::endl;
        std::regex pair_regex(R"(\{\s*([-+]?\d+)\s*,\s*([-+]?\d+)\s*\})");
        auto pairs_begin = std::sregex_iterator(norm_block.begin(), norm_block.end(), pair_regex);
        auto pairs_end = std::sregex_iterator();

        for (std::sregex_iterator i = pairs_begin; i != pairs_end; ++i) {
            std::smatch match = *i;
            std::vector<int> pair = {std::stoi(match[1].str()), std::stoi(match[2].str())};
            params.mult_shift.push_back(pair);
        }
    }

    return params;
}

int main(int argc, char *argv[]) {
    
    std::string input_dir = "int_params";
    std::string csv_dir = "csv_params";
    if (argc > 1) {
        input_dir = argv[1];
    }
    if (argc > 2) {
        csv_dir = argv[2];
    }

    // Créer le répertoire csv_params s'il n'existe pas
    std::filesystem::create_directory(csv_dir);

    // Liste des fichiers dans le répertoire d'entrée
    std::vector<std::string> layer_files;
    for (const auto& entry : std::filesystem::directory_iterator(input_dir)) {
        if (entry.path().extension() == ".h") {
            layer_files.push_back(entry.path().string());
        }
    }

    // Traiter chaque fichier de couche
    for (const auto& filename : layer_files) {
        std::string layer = filename.substr(filename.find("q_layer") + 7, filename.find(".h") - filename.find("q_layer") - 7);
        std::cout << "Processing layer " << layer <<"..."<< std::endl;

        // Lire les paramètres à partir des fichiers d'en-tête
        Parameters params = read_parameters(filename);
        // Écrire les paramètres dans des fichiers CSV
        if (params.mult_shift.empty()) {
            std::cerr << "mult_shift is empty for layer " << layer << std::endl;
        } else {

            //Debug
            #if 0
            std::cout<<"weights ==:"<<std::endl;
            for (int i = 0; i < static_cast<int>(params.weights.size()); ++i) {
                for (int j = 0; j < static_cast<int>(params.weights[i].size()); ++j) {
                    
                    std::cout << params.weights[i][j]<<",";
                    
                }
                std::cout<<std::endl;
            }
            
            std::cout<<"mult_shift :";
            for (int i = 0; i < static_cast<int>(params.mult_shift.size()); ++i) {
                std::cout << params.mult_shift[i][0] <<","<< params.mult_shift[i][1]<<"\n"; 
            }

           std::cout<<"biases :";
            for (int i = 2; i < static_cast<int>(params.biases.size()); ++i) {
                std::cout << params.biases[i] <<"\n"; 
            }
            #endif

            write_norm_csv(csv_dir+"/norm" + layer + ".csv", params.mult_shift, params.biases);
        }
        if (!params.weights.empty()) {
            write_csv(csv_dir+"/weights" + layer + ".csv", params.weights, params.dim);
        }
    }

    std::cout << "CSV files generated successfully." << std::endl;

    return 0;
}