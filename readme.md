**`THIS IMPLEMENTATION IS STILL IN CONSTRUCTION`**


Implementation of YOLOv2-tiny with NNawaq[1]
========================================================================

Objectives
---

This project consist to implement a version of the neural network `YOLOv2-tiny` from darknet[2].
It need to be cloned in an existing nnawaq clone.

The work is split in 3 folder:
- `datasets/YOLO` where we can found all YOLO's weights processing
- `hdl/nn` for the hardware implementation of the component `nnlayer_leaky.vhd`
- `tools` for the support of the new composant LeakyRelu by nnawaq and the addition of new layers in software simulation using the “channel-major” order.

datasets/YOLO
---
With this folder, we can quantize the weights of `YOLOv2-tiny` (COCO dataset).
In `utils`:
- `create_yolo_params.py` extract from `YOLOv2-tiny.weights` YOLO's weights and parameters (BatchNorm, Bias...) (in float32).
- `convert_yolo_params.cpp` quantize the weights (float32 -> int8) and the parameters.
- `convert_params_csv.cpp` convert the quantized weights in csv format (nnawaq format)

### Pipeline

To compile all targets or select one (if TARGET=all, it launches the processing of the weights):
```
make [TARGET=<all (default), convert, csv>]
```

To launch the download of the weights, the extractions of the weights, the quantization and the convertion from .h to .csv:
```
make <config, create, run_convert, run_csv, params (for all)>
```

tools and hdl/nn
---
These files come from the original nnawaq repo. Some of them have been modified to allow the implementation of the component `LeakyRelu` (in `hdl/nn/nnlayer_leaky.vhd`).

The software simulation of nnawaq should now work with new layers `LayerNeu_CM`, `LayerWin_CM` and `LayerNorm_CM` to directly use the channel-major order.

In `nnawaq-tcl`, `build_yolo.tcl` and `config_yolo.tcl` are used to build and config `YOLOv2-tiny` with the last layer region of YOLO. This layer is still software with `region_yolo.py`.

Areas for improvement
---
- The quantization of the weights and parameters of YOLO should be correct. However, it can be improved: Exclude extreme values when calculating the channel quantization scale.

- The last convolutional layer of YOLO doesn't have a BatchNorm layer associated to it, only bias. The calcul of the parameters of `norm8.csv` probably needs to be corrected to ensure a good dequantization.

- `convert_format_output.cpp` may be not correct, need more tests.

- Using darknet to get all the outputs of each layer and doing the same with nnawaq to create a heat map of theses values to check if the quantized values actually correspond to something.

- The implementation of the layers `_CM` may not be usefull, nnawaq can manage the calcul order.

- Hardware implementation of the layer region of YOLO.

References
---
[1] <https://gricad-gitlab.univ-grenoble-alpes.fr/prostboa/nnawaq/-/tree/main>

[2] <https://github.com/pjreddie/darknet>
