#ifndef ABERRATION_NET_H
#define ABERRATION_NET_H

/*
 * aberration_net.h
 *
 * Neural network pipelines for optical aberration analysis.
 * Built on OpenCV's DNN module — no external deep-learning
 * framework dependency beyond what OpenCV already requires.
 *
 * Two independent models are provided:
 *
 *   Model A — ImageNet  (image → Seidel coefficients)
 *   -------------------------------------------------------
 *   Input  : normalised 8-bit grayscale interferogram patch
 *            resized to NET_IMG_W × NET_IMG_H
 *   Output : SEIDEL_COUNT regression outputs (W040…W311)
 *            + 1 auxiliary RMS output  → total NET_IMG_OUT
 *
 *   Model B — SeidelNet (Seidel coefficients → quality class)
 *   -------------------------------------------------------
 *   Input  : SEIDEL_COUNT floats  (W[0..4])
 *   Output : NET_SEIDEL_CLASSES softmax probabilities
 *            representing discrete quality bands
 *            (e.g. diffraction-limited / mild / severe aberration)
 *
 * Both models share the same lightweight training loop implemented
 * in pure C++ using OpenCV Mat operations (no CUDA required, but
 * GPU path is available via cv::dnn::Net::setPreferableBackend).
 *
 * Naming / style conventions follow the existing project exactly:
 *   - C linkage wrapper functions (extern "C") callable from main.c
 *   - Heap-allocated opaque handles with create_ / free_ functions
 *   - Return codes: 0 = success, -1 = failure (consistent with
 *     allocatePixelMemory in bmp.c)
 */

#include <aberration.h>   /* SeidelCoeffs, SEIDEL_COUNT */
#include <bmp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================
 * COMPILE-TIME CONSTANTS
 * ========================================================= */

/* Image model input resolution */
#define NET_IMG_W        128
#define NET_IMG_H        128

/* Output sizes */
#define NET_IMG_OUT      (SEIDEL_COUNT + 1)   /* 5 Seidel + 1 RMS    */
#define NET_SEIDEL_CLASSES  3                 /* quality band count  */

/* Training hyper-parameters (overridable at runtime via NetConfig) */
#define NET_DEFAULT_LR          1e-3
#define NET_DEFAULT_EPOCHS      50
#define NET_DEFAULT_BATCH       16
#define NET_DEFAULT_MOMENTUM    0.9
#define NET_DEFAULT_WD          1e-4   /* weight decay */

/* =========================================================
 * TRAINING CONFIGURATION
 * ========================================================= */

typedef struct {
    double learning_rate;
    int    epochs;
    int    batch_size;
    double momentum;
    double weight_decay;
    int    use_gpu;          /* 1 = try CUDA/OpenCL backend, 0 = CPU */
    int    verbose;          /* print loss every N epochs (0 = silent)*/
    int    save_every;       /* checkpoint every N epochs (0 = off)   */
    const char *checkpoint_dir;
} NetConfig;

/* Fill cfg with the compile-time defaults */
void net_default_config(NetConfig *cfg);

/* =========================================================
 * TRAINING SAMPLE TYPES
 * ========================================================= */

/*
 * ImageSample — one training example for Model A.
 *   img       — BMPImage* (not owned; must outlive the dataset)
 *   label     — ground-truth Seidel coefficients + RMS
 *               label[0..4] = W040, W131, W222, W220, W311
 *               label[5]    = rms_wavefront
 */
typedef struct {
    BMPImage *img;
    float     label[NET_IMG_OUT];
} ImageSample;

/*
 * SeidelSample — one training example for Model B.
 *   features  — SEIDEL_COUNT Seidel coefficients
 *   class_id  — integer quality class in [0, NET_SEIDEL_CLASSES)
 */
typedef struct {
    float features[SEIDEL_COUNT];
    int   class_id;
} SeidelSample;

/* =========================================================
 * OPAQUE MODEL HANDLES
 * (implementation in aberration_net.cpp uses cv::dnn internals)
 * ========================================================= */

typedef struct ImageNetHandle  ImageNet;
typedef struct SeidelNetHandle SeidelNet;

/* =========================================================
 * MODEL A — ImageNet (interferogram → Seidel regression)
 * ========================================================= */

/*
 * create_image_net
 *
 * Builds the model-A architecture:
 *   Conv2D(1,16,3,pad=1) → BN → ReLU → MaxPool(2)
 *   Conv2D(16,32,3,pad=1) → BN → ReLU → MaxPool(2)
 *   Conv2D(32,64,3,pad=1) → BN → ReLU → MaxPool(2)
 *   Conv2D(64,128,3,pad=1) → BN → ReLU → AdaptiveAvgPool → flatten
 *   FC(128,64) → ReLU → Dropout(0.3)
 *   FC(64, NET_IMG_OUT)           ← linear regression head
 *
 * Weights initialised with He normal.
 */
ImageNet *create_image_net(void);
void      free_image_net(ImageNet *net);

/*
 * image_net_train
 *
 * Trains model A on the provided dataset (mini-batch SGD with
 * momentum + L2 weight decay).  Loss = MSE over NET_IMG_OUT outputs.
 *
 *   samples   — array of ImageSample structs
 *   n_samples — dataset size
 *   cfg       — training hyper-parameters
 *
 * Returns 0 on success, -1 on failure.
 */
int image_net_train(ImageNet *net,
                    const ImageSample *samples, int n_samples,
                    const NetConfig *cfg);

/*
 * image_net_predict
 *
 * Run a forward pass on a single BMPImage.
 *   output — caller-allocated float[NET_IMG_OUT]
 * Returns 0 on success, -1 on failure.
 */
int image_net_predict(ImageNet *net, BMPImage *img, float *output);

/*
 * image_net_save / image_net_load
 *
 * Persist model weights to / from an OpenCV FileStorage XML/YAML file.
 * Path convention: "<checkpoint_dir>/image_net_<epoch>.xml"
 */
int image_net_save(const ImageNet *net, const char *path);
int image_net_load(ImageNet *net, const char *path);

/* =========================================================
 * MODEL B — SeidelNet (Seidel coefficients → quality class)
 * ========================================================= */

/*
 * create_seidel_net
 *
 * Builds the model-B architecture:
 *   FC(SEIDEL_COUNT, 32) → ReLU → BatchNorm1D
 *   FC(32, 64)           → ReLU → BatchNorm1D
 *   FC(64, 32)           → ReLU → Dropout(0.2)
 *   FC(32, NET_SEIDEL_CLASSES) → Softmax
 *
 * Weights initialised with Xavier uniform.
 */
SeidelNet *create_seidel_net(void);
void       free_seidel_net(SeidelNet *net);

/*
 * seidel_net_train
 *
 * Trains model B.  Loss = Cross-entropy over NET_SEIDEL_CLASSES.
 * Optimiser: SGD with momentum and weight decay (same as model A).
 *
 * Returns 0 on success, -1 on failure.
 */
int seidel_net_train(SeidelNet *net,
                     const SeidelSample *samples, int n_samples,
                     const NetConfig *cfg);

/*
 * seidel_net_predict
 *
 * Forward pass for a single SeidelCoeffs input.
 *   probs_out — caller-allocated float[NET_SEIDEL_CLASSES] (softmax)
 *   class_out — pointer to int; receives argmax class id (may be NULL)
 * Returns 0 on success, -1 on failure.
 */
int seidel_net_predict(SeidelNet *net, const SeidelCoeffs *sc,
                       float *probs_out, int *class_out);

int seidel_net_save(const SeidelNet *net, const char *path);
int seidel_net_load(SeidelNet *net, const char *path);

/* =========================================================
 * HIGH-LEVEL TRAINING PIPELINE
 * ========================================================= */

/*
 * AberrationDataset
 *
 * Paired training dataset built from a directory of interferogram BMPs
 * and their corresponding ground-truth Seidel labels (CSV).
 *
 * CSV format (one row per image):
 *   filename, W040, W131, W222, W220, W311, rms
 *
 * Class labels for SeidelNet are derived automatically:
 *   Strehl >= 0.80  → class 0  (diffraction-limited)
 *   Strehl  0.50..0.80 → class 1  (mild aberration)
 *   Strehl <  0.50  → class 2  (severe aberration)
 */
typedef struct {
    ImageSample  *image_samples;
    SeidelSample *seidel_samples;
    int           count;
    BMPImage    **images;          /* owns the loaded BMPImages */
} AberrationDataset;

/*
 * load_aberration_dataset
 *
 * Loads all BMP images from `img_dir` and matches them to labels in
 * `csv_path`.  Returns NULL on failure.
 */
AberrationDataset *load_aberration_dataset(const char *img_dir,
                                           const char *csv_path);
void free_aberration_dataset(AberrationDataset *ds);

/*
 * train_aberration_models
 *
 * Convenience function: trains both Model A and Model B on `ds`.
 * Splits dataset 80/20 train/val internally and prints val loss.
 * Saves final weights to `save_dir`.
 *
 * Returns 0 on success, -1 on any failure.
 */
int train_aberration_models(AberrationDataset *ds,
                            const NetConfig *cfg,
                            const char *save_dir);

/*
 * run_inference_pipeline
 *
 * Given a raw interferogram and pre-loaded models, runs:
 *   1. run_aberration_pipeline()  → SeidelCoeffs
 *   2. image_net_predict()        → regressed Seidel estimate
 *   3. seidel_net_predict()       → quality classification
 * and prints a summary report to stdout.
 *
 * Either net may be NULL to skip that branch.
 */
void run_inference_pipeline(BMPImage  *interferogram,
                            Kernel    *blur_kernel,
                            ImageNet  *image_net,
                            SeidelNet *seidel_net);

#ifdef __cplusplus
}
#endif

#endif /* ABERRATION_NET_H */
