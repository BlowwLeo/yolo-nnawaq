# Répertoires de travail
MODEL_DIR ?= ./yolo_weights_cfg
UTILS_DIR ?= ./utils
PARAMS_DIR ?= ./params
BIN_DIR ?= $(UTILS_DIR)/bin

# Noms des exécutables
CONVERT_BIN = $(BIN_DIR)/convert_yolo_params
CSV_BIN     = $(BIN_DIR)/convert_params_csv


.PHONY: all clone yolo config create convert csv params run_convert run_csv

TARGET ?= all


# Cible par défaut : réaliser tout le pipeline
all: compile

clone: yolo
	@echo "Aucune opération de clone nécessaire pour yolo."

yolo:
	@echo "Cible yolo (dépendance commune)."

config: yolo
	@echo "Téléchargement des fichiers de configuration..."
	wget -P $(MODEL_DIR) https://pjreddie.com/media/files/yolov2-tiny.weights
	wget -P $(MODEL_DIR) https://raw.githubusercontent.com/pjreddie/darknet/master/cfg/yolov2-tiny.cfg

create: yolo
	@echo "Création des paramètres float..."
	python3 $(UTILS_DIR)/create_yolo_params.py --cfg $(MODEL_DIR)/yolov2-tiny.cfg --weights $(MODEL_DIR)/yolov2-tiny.weights --output_folder $(PARAMS_DIR)/float_params

# Pour lancer les exécutables indépendamment sans recompilation :
run_convert: $(CONVERT_BIN)
	$(CONVERT_BIN) $(PARAMS_DIR)/float_params $(PARAMS_DIR)/int_params

run_csv: $(CSV_BIN)
	$(CSV_BIN) $(PARAMS_DIR)/int_params $(PARAMS_DIR)/csv_params

params: create run_convert run_csv
	@echo "Paramètres convertis et enregistrés dans $(PARAMS_DIR)/int_params et $(PARAMS_DIR)/csv_params."

# Compilation de convert_yolo_params.cpp uniquement si nécessaire
$(CONVERT_BIN): $(UTILS_DIR)/convert_yolo_params.cpp
	@mkdir -p $(BIN_DIR)
	@echo "Compilation de convert_yolo_params..."
	g++ -o $(CONVERT_BIN) $(UTILS_DIR)/convert_yolo_params.cpp

# Compilation de convert_params_csv.cpp uniquement si nécessaire
$(CSV_BIN): $(UTILS_DIR)/convert_params_csv.cpp
	@echo "Compilation de convert_params_csv..."
	g++ -o $(CSV_BIN) $(UTILS_DIR)/convert_params_csv.cpp

# Nouvelle cible pour compiler tout ou partie des exécutables sans lancer leur exécution
compile:
	@if [ "$(TARGET)" = "convert" ] || [ "$(TARGET)" = "all" ]; then \
		echo "Compilation de convert_yolo_params..."; \
		$(MAKE) $(CONVERT_BIN); \
	fi
	@if [ "$(TARGET)" = "csv" ] || [ "$(TARGET)" = "all" ]; then \
		echo "Compilation de convert_params_csv..."; \
		$(MAKE) $(CSV_BIN); \
	fi
	@echo "Compilation terminée pour TARGET=$(TARGET)."





# Cible pour nettoyer les exécutables compilés
clean:
	rm -rf $(BIN_DIR)
	@echo "Nettoyage terminé : exécutables supprimés."
