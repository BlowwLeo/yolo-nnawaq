#!/usr/bin/env python3
import json
import math
import argparse

# DIVISER PAR 2^16

def sigmoid(x):
    """
    Calcule la sigmoïde de x de manière numériquement stable.
    """
    if x >= 0:
        z = math.exp(-x)
        return 1 / (1 + z)
    else:
        z = math.exp(x)
        return z / (1 + z)

def softmax(scores):
    """
    Applique la fonction softmax sur une liste de scores de manière numériquement stable.
    """
    max_score = max(scores)
    exp_scores = [math.exp(s - max_score) for s in scores]
    sum_exp = sum(exp_scores)
    return [s / sum_exp for s in exp_scores]

def safe_exp(x, max_val=50):
    """
    Renvoie math.exp(x) en limitant x à max_val pour éviter un overflow.
    """
    if x > max_val:
        x = max_val
    return math.exp(x)

def parse_sw_out(file_path, factor, grid_size=13, num_values_per_cell=425):
    """
    Lit le fichier sw-out.txt contenant toutes les valeurs séparées par des virgules,
    et reconstruit une liste de dictionnaires, un par cellule de la grille.
    
    On suppose que les valeurs sont déjà au format float.
    Pour chaque cellule, on extrait 425 valeurs, correspondant aux 5 prédictions
    (chaque prédiction ayant 85 valeurs : [tx, ty, tw, th, tc] suivi de 80 scores de classes).
    """
    with open(file_path, 'r') as f:
        content = f.read()
    # Séparation des valeurs par virgule et conversion en float
    values = [float(v.strip()) for v in content.split(',') if v.strip() != '']
    
    if len(values) != grid_size * grid_size * num_values_per_cell:
        raise ValueError("Le nombre de valeurs lues ne correspond pas à 13*13*425")
    
    grid_data = []
    for i in range(grid_size * grid_size):
        cell_values = values[i * num_values_per_cell : (i + 1) * num_values_per_cell]
        grid_y = i // grid_size
        grid_x = i % grid_size
        cell_dict = {"grid_x": grid_x, "grid_y": grid_y, "predictions": []}
        # Pour YOLOv2-tiny : 5 prédictions par cellule, chacune avec 85 valeurs
        for b in range(5):
            start = b * 85
            tx = cell_values[start]
            ty = cell_values[start + 1]
            tw = cell_values[start + 2]
            th = cell_values[start + 3]
            tc = cell_values[start + 4]
            classes_raw = cell_values[start + 5 : start + 85]
            cell_dict["predictions"].append({
                "tx": tx,
                "ty": ty,
                "tw": tw,
                "th": th,
                "tc": tc,
                "classes": classes_raw
            })
        grid_data.append(cell_dict)
    return grid_data

def extract_detections(grid_data, input_dim=416, grid_size=13):
    """
    Pour chaque cellule de la grille, calcule les coordonnées des bounding boxes 
    en appliquant les transformations (sigmoïde, exponentielle, softmax) sur les valeurs brutes.
    
    On utilise ici des anchors fixes pour YOLOv2-tiny
    
    Calculs:
        - boxXY = (sigmoid(tx) + grid_x, sigmoid(ty) + grid_y) * stride
        - boxWH = (anchor_w * safe_exp(tw), anchor_h * safe_exp(th)) * stride
        - objScore = sigmoid(tc)
        - classProb = softmax(classes)
    
    Le "stride" est égal à input_dim / grid_size.
    """
    anchors = [
        [0.57273, 0.677385],
        [1.87446, 2.06253],
        [3.33843, 5.47434],
        [7.88282, 3.52778],
        [9.77052, 9.16828]
    ]
    stride = input_dim / grid_size
    output_data = []
    
    for cell in grid_data:
        grid_x = cell["grid_x"]
        grid_y = cell["grid_y"]
        cell_result = {"grid_x": grid_x, "grid_y": grid_y, "detections": []}
        
        for idx, pred in enumerate(cell["predictions"]):
            tx = pred["tx"]
            ty = pred["ty"]
            tw = pred["tw"]
            th = pred["th"]
            tc = pred["tc"]
            classes_raw = pred["classes"]
            
            # Calcul du centre de la bounding box en pixels
            x_center = (sigmoid(tx) + grid_x) * stride
            y_center = (sigmoid(ty) + grid_y) * stride
            
            # Calcul de la largeur et hauteur en utilisant safe_exp pour éviter l'overflow
            width = anchors[idx][0] * safe_exp(tw) * stride
            height = anchors[idx][1] * safe_exp(th) * stride
            
            # Score d'objectivité et probabilités de classes
            obj_score = sigmoid(tc)
            class_prob = softmax(classes_raw)
            
            detection = {
                "boxXY": [x_center, y_center],
                "boxWH": [width, height],
                "objScore": obj_score,
                "classProb": class_prob
            }
            cell_result["detections"].append(detection)
        output_data.append(cell_result)
    return output_data

def main():
    parser = argparse.ArgumentParser(
        description="Convertis les sorties de YOLOv2-tiny en détections JSON"
    )
    parser.add_argument("facteur", type=str, help="puissance de 2")
    factor = parser.parse_args().facteur
    input_file = "out-sw.txt"
    output_file = "detections.json"
    
    # Extraction des données de la grille depuis sw-out.txt (valeurs déjà en float)
    grid_data = parse_sw_out(input_file, factor)
    
    # Calcul des détections (bounding boxes, scores, etc.)
    detections = extract_detections(grid_data)
    
    # Sauvegarde des résultats dans un fichier JSON
    with open(output_file, 'w') as f:
        json.dump(detections, f, indent=4)
    print("Les détections ont été sauvegardées dans", output_file)



if __name__ == "__main__":
    main()
