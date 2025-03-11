import os
import numpy as np
import argparse

def parse_cfg(cfg_path):
    """Parse le fichier .cfg pour identifier les couches convolutionnelles."""
    with open(cfg_path, 'r') as f:
        layers = []
        current_layer = {}
        for line in f:
            line = line.strip()
            # On ignore les lignes vides ou les commentaires
            if not line or line.startswith("#") or line.startswith(";"):
                continue
            if line.startswith('['):
                if current_layer:
                    layers.append(current_layer)
                current_layer = {'type': line[1:-1].strip()}
            elif current_layer:
                if '=' in line:
                    key, val = line.split('=', 1)
                    current_layer[key.strip()] = val.strip()
                else:
                    print(f"Attention : ligne ignorée (pas de '='): {line}")
        if current_layer:
            layers.append(current_layer)
    return [l for l in layers if l['type'] == 'convolutional']

def read_weights(weights_path, conv_layers):
    """Lit les paramètres binaires et les organise par couche."""
    params = []
    with open(weights_path, 'rb') as f:
        _ = np.fromfile(f, dtype=np.int32, count=5)
        
        in_channels = 3  # Entrée initiale (RGB)
        for idx, layer in enumerate(conv_layers):
            layer_params = {}
            filters = int(layer['filters'])
            size = int(layer['size'])
            batch_norm = 'batch_normalize' in layer and layer['batch_normalize'] == '1'
            if batch_norm:
                # Lire BatchNorm : gamma, beta, mean, var
                layer_params['gamma'] = np.fromfile(f, dtype=np.float32, count=filters)
                layer_params['beta'] = np.fromfile(f, dtype=np.float32, count=filters)
                layer_params['mean'] = np.fromfile(f, dtype=np.float32, count=filters)
                layer_params['var'] = np.fromfile(f, dtype=np.float32, count=filters)
            
            # Lire les poids de la convolution
            n_weights = filters * in_channels * size * size
            conv_weights = np.fromfile(f, dtype=np.float32, count=n_weights)
            conv_weights = conv_weights.reshape((filters, in_channels, size, size))
            layer_params['weights'] = conv_weights

            if not batch_norm:
                # Lire les biais si pas de BatchNorm
                layer_params['biases'] = np.fromfile(f, dtype=np.float32, count=filters)

            params.append(layer_params)
            in_channels = filters  # Mise à jour pour la couche suivante
    return params

def array_to_initializer(arr, indent=0):
    """Convertit un tableau numpy en une chaîne C initialisant un tableau (avec accolades imbriquées)."""
    indent_str = "    " * indent
    if arr.ndim == 1:
        # Tableau 1D : on retourne une liste simple
        inner = ", ".join(f"{x:.10f}f" for x in arr)
        return "{ " + inner + " }"
    else:
        lignes = []
        for sous_arr in arr:
            lignes.append(array_to_initializer(sous_arr, indent+1))
        inner = ",\n".join(lignes)
        return "{\n" + inner + "\n" + indent_str + "}"

def write_layer_file(layer, layer_index, output_folder, epsilon=0.001):
    """Écrit les paramètres d'une couche dans un fichier .h séparé."""
    # Création du nom du fichier, ex : yolo_params/layer0.h
    filename = os.path.join(output_folder, f"layer{layer_index}.h")
    with open(filename, 'w') as f:
        f.write("/*************************************/\n")
        f.write("/* Fichier .h généré automatiquement */\n")
        f.write("/*     par create_yolo_params.py     */\n")
        f.write("/*************************************/\n\n")
        guard = f"YOLO_LAYER{layer_index}_H"
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write("#include <stdint.h>\n\n")

        # Écriture des poids de convolution
        weights = layer['weights']
        dims = "".join(f"[{d}]" for d in weights.shape)
        initializer = array_to_initializer(weights)
        f.write(f"float layer{layer_index}_weights{dims} = {initializer};\n\n")
        
        # Écriture des biais (si présents)
        if 'biases' in layer:
            biases = layer['biases']
            dims = "".join(f"[{d}]" for d in biases.shape)
            initializer = array_to_initializer(biases)
            f.write(f"float layer{layer_index}_biases{dims} = {initializer};\n\n")

        # Écriture des paramètres BatchNorm (si présents)
        if 'gamma' in layer:
            dims = "".join(f"[{d}]" for d in layer['gamma'].shape)      
            scale=[]
            bias=[]
            if (layer_index == 0):
                for i in range(len(layer['gamma'])):
                    scale.append(layer['gamma'][i]/(255*np.sqrt(layer['var'][i]+epsilon)))
                    bias.append(layer['beta'][i] - layer['gamma'][i]*layer['mean'][i]/(np.sqrt(layer['var'][i]+epsilon)))
                
            else:
                for i in range(len(layer['gamma'])):
                    scale.append(layer['gamma'][i]/(np.sqrt(layer['var'][i]+epsilon)))
                    bias.append(layer['beta'][i] - layer['gamma'][i]*layer['mean'][i]/(np.sqrt(layer['var'][i]+epsilon)))
            scale_txt = "{" + ", ".join(map(str, scale)) + "}"
            bias_txt = "{" + ", ".join(map(str, bias)) + "}"
            f.write(f"float layer{layer_index}_scale_norm{dims} = {scale_txt};\n\n")
            f.write(f"float layer{layer_index}_biases{dims} = {bias_txt};\n\n")
        
        f.write(f"#endif // {guard}\n")

def write_all_layers(params, output_folder):
    """Écrit chaque couche dans un fichier .h séparé dans le dossier output_folder."""
    # Crée le dossier s'il n'existe pas
    if not os.path.exists(output_folder):
        os.makedirs(output_folder)
    
    for i, layer in enumerate(params):
        write_layer_file(layer, i, output_folder)
        print(f"Couche {i} écrite dans {output_folder}/layer{i}.h")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--cfg', type=str, default="yolo_weights_cfg/yolov2-tiny.cfg", help='Chemin vers yolov2-tiny.cfg')
    parser.add_argument('--weights', type=str, default="yolo_weights_cfg/quantized_model.weights", help='Chemin vers yolov2-tiny.weights')
    parser.add_argument('--output_folder', type=str, default="yolo_params_qat", help='Dossier de sortie pour les fichiers .h de chaque couche')
    args = parser.parse_args()

    # Extraction des paramètres depuis le .cfg et le fichier .weights
    conv_layers = parse_cfg(args.cfg)
    params = read_weights(args.weights, conv_layers)
    write_all_layers(params, args.output_folder)

    print(f"Paramètres écrits dans le dossier {args.output_folder}!")
