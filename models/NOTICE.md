# Models

The models in this directory are converted from open models released by Google MediaPipe under the
Apache License 2.0 (full text in `LICENSE-Apache-2.0.txt`). Weights and network structure are unchanged;
only the file format was converted from TFLite to ONNX.

| File | Source model | Original file | Model card |
|---|---|---|---|
| `face_detector.onnx` | MediaPipe BlazeFace (short range) | `face_detector.tflite` in `face_landmarker.task` | https://storage.googleapis.com/mediapipe-assets/MediaPipe%20BlazeFace%20Model%20Card%20(Short%20Range).pdf |
| `face_landmarks.onnx` | MediaPipe Face Mesh V2 (478 points) | `face_landmarks_detector.tflite` in `face_landmarker.task` | https://storage.googleapis.com/mediapipe-assets/Model%20Card%20MediaPipe%20Face%20Mesh%20V2.pdf |
| `selfie_segmenter.onnx` | MediaPipe Selfie Segmentation (landscape) | `selfie_segmenter_landscape.tflite` | https://storage.googleapis.com/mediapipe-assets/Model%20Card%20MediaPipe%20Selfie%20Segmentation.pdf |
| `selfie_multiclass.onnx` | MediaPipe Selfie Multiclass Segmentation (256x256) | `selfie_multiclass_256x256.tflite` | https://storage.googleapis.com/mediapipe-assets/Model%20Card%20Multiclass%20Segmentation.pdf |

Downloads:

- https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task
  (SHA-256 `64184e229b263107bc2b804c6625db1341ff2bb731874b0bcc2fe6544e0bc9ff`)
- https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_segmenter_landscape/float16/latest/selfie_segmenter_landscape.tflite
  (SHA-256 `490e9ea734313e0de10fa0cd9e3c6133e36ea4db2b7a49bde9ef019f72796b8e`)
- https://storage.googleapis.com/mediapipe-models/image_segmenter/selfie_multiclass_256x256/float32/latest/selfie_multiclass_256x256.tflite
  (SHA-256 `c6748b1253a99067ef71f7e26ca71096cd449baefa8f101900ea23016507e0e0`)

## Conversion

With tf2onnx 1.17.0 (opset 17):

```
python -m tf2onnx.convert --tflite face_detector.tflite --output face_detector.onnx --opset 17
python -m tf2onnx.convert --tflite face_landmarks_detector.tflite --output face_landmarks.onnx --opset 17
python -m tf2onnx.convert --tflite selfie_multiclass_256x256.tflite --output selfie_multiclass.onnx --opset 17
```

The landscape selfie segmenter uses MediaPipe's custom op `Convolution2DTransposeBias`, which tf2onnx
cannot translate. `convert_selfie_segmenter.py` replaces it with the equivalent standard `ConvTranspose`
(weights transposed from [out, h, w, in] to [in, out, h, w], stride 2, same bias):

```
python -m tf2onnx.convert --tflite selfie_segmenter_landscape.tflite --output tmp.onnx --opset 17
python convert_selfie_segmenter.py tmp.onnx selfie_segmenter.onnx
```

## Intended use

Per the model cards, these models are not meant for identification, surveillance or decisions affecting
people's safety. This project only uses them locally, in real time, to place visual effects on video
frames. Nothing is stored or uploaded.
