#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

using namespace std;

int main(int argc, char* argv[]) {
    // Vérification des arguments : usage: programme input.txt output.txt
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " input.txt output.txt" << endl;
        return 1;
    }
    
    string inputFilename = argv[1];
    string outputFilename = argv[2];
    
    // Définition des paramètres de l'image et des canaux
    const int width = 416;
    const int height = 416;
    const int imageChannels = 1; // par exemple pour une image mono-canal
    const int numPixels = width * height * imageChannels; // nombre total de pixels
    const int numChannels = 16; // nombre de filtres par pixel
    const int totalValues = numPixels * numChannels;
    
    // Lecture des valeurs depuis le fichier d'entrée
    vector<float> data;
    data.reserve(totalValues);
    
    ifstream infile(inputFilename);
    if (!infile) {
        cerr << "Erreur lors de l'ouverture du fichier " << inputFilename << endl;
        return 1;
    }
    
    float value;
    while (infile >> value) {
        data.push_back(value);
    }
    infile.close();
    
    // Vérification du nombre de valeurs lues
    if (data.size() != totalValues) {
        cerr << "Nombre de valeurs incorrect (" << data.size() 
             << "). Attendu: " << totalValues << endl;
        return 1;
    }
    
    // Réorganisation des données : passage de pixel-first à channel-first
    // Dans data, pour un pixel p et un canal c, la valeur est à l'index: p * numChannels + c.
    // On souhaite obtenir reordered tel que pour un canal c et un pixel p:
    // reordered[c * numPixels + p] = data[p * numChannels + c]
    
    vector<float> reordered(totalValues);
    for (int c = 0; c < numChannels; c++) {
        for (int x = 0; x < 416; x++) {
            for (int y = 0; y < 416; y++) {
                reordered[c*16*416+y*416+x] = data[y*416*16+x*16+c];
            }

        }
    }
    
    
    // Écriture des données réorganisées dans le fichier de sortie
    ofstream outfile(outputFilename);
    if (!outfile) {
        cerr << "Erreur lors de l'ouverture du fichier " << outputFilename << endl;
        return 1;
    }
    
    for (const auto& v : reordered) {
        outfile << v << "\n";
    }
    outfile.close();
    
    cout << "Réorganisation terminée avec succès." << endl;
    return 0;
}
