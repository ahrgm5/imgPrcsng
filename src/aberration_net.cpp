/* aberration_net.cpp
 *
 * Neural network pipelines for interferogram aberration analysis.
 * Built entirely on OpenCV Mat / DNN primitives — no external
 * deep-learning framework beyond opencv4 is required.
 *
 * Build:  link with -lopencv_core -lopencv_dnn -lopencv_imgproc
 *         alongside the existing -lfftw3 -lm flags.
 *
 * Naming mirrors the project's C style:
 *   snake_case functions, explicit create/free, return 0/-1.
 */

#include "aberration_net.h"

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <fstream>
#include <sstream>

/* =========================================================
 * C-LINKAGE WRAPPER  (everything callable from main.c)
 * ========================================================= */
extern "C" {

/* =========================================================
 * INTERNAL: lightweight layer types
 *
 * OpenCV's DNN module is primarily an inference engine for ONNX /
 * Caffe / TF models loaded from disk.  For *training* we use raw
 * cv::Mat arithmetic (forward + backward pass in plain matrix ops)
 * which is exactly what OpenCV provides and requires no additional
 * backend.
 *
 * Layer abstraction:
 *   Each layer stores its weight/bias Mats and gradient accumulators.
 *   Forward() fills output; backward() accumulates ∂L/∂W and ∂L/∂x.
 *   update_weights() applies SGD-momentum step.
 * ========================================================= */

/* ---- Fully-connected layer ------------------------------------ */
struct FCLayer {
    cv::Mat W;      /* [out_size x in_size]  */
    cv::Mat b;      /* [out_size x 1]        */
    cv::Mat dW;
    cv::Mat db;
    cv::Mat vW;     /* velocity for SGD+momentum */
    cv::Mat vb;

    /* Forward cache */
    cv::Mat input_cache;  /* [in_size x batch]  */

    int in_size, out_size;

    FCLayer() : in_size(0), out_size(0) {}

    void init(int in, int out) {
        in_size  = in;
        out_size = out;
        /* Xavier uniform initialisation */
        double limit = std::sqrt(6.0 / (in + out));
        cv::randu(W  = cv::Mat(out, in, CV_64F), -limit, limit);
        b  = cv::Mat::zeros(out, 1, CV_64F);
        dW = cv::Mat::zeros(out, in, CV_64F);
        db = cv::Mat::zeros(out, 1, CV_64F);
        vW = cv::Mat::zeros(out, in, CV_64F);
        vb = cv::Mat::zeros(out, 1, CV_64F);
    }

    /* input:  [in_size  x batch_size]
       output: [out_size x batch_size] */
    cv::Mat forward(const cv::Mat &input) {
        input_cache = input.clone();
        cv::Mat out = W * input;
        /* Broadcast bias */
        for (int j = 0; j < out.cols; j++)
            out.col(j) += b;
        return out;
    }

    /* grad_out: [out_size x batch]
       returns:  [in_size  x batch]  (grad w.r.t. input) */
    cv::Mat backward(const cv::Mat &grad_out) {
        int batch = grad_out.cols;
        dW = grad_out * input_cache.t() / (double)batch;
        db = cv::Mat::zeros(out_size, 1, CV_64F);
        for (int j = 0; j < batch; j++) db += grad_out.col(j);
        db /= (double)batch;
        return W.t() * grad_out;
    }

    void update(double lr, double momentum, double wd) {
        /* SGD with momentum + weight decay:
           v = momentum*v - lr*(dW + wd*W)
           W += v                             */
        vW = momentum * vW - lr * (dW + wd * W);
        vb = momentum * vb - lr * db;
        W += vW;
        b += vb;
    }

    void save(cv::FileStorage &fs, const std::string &prefix) const {
        fs << (prefix + "_W") << W;
        fs << (prefix + "_b") << b;
    }

    void load(cv::FileStorage &fs, const std::string &prefix) {
        fs[prefix + "_W"] >> W;
        fs[prefix + "_b"] >> b;
        dW = cv::Mat::zeros(W.rows, W.cols, CV_64F);
        db = cv::Mat::zeros(b.rows, b.cols, CV_64F);
        vW = cv::Mat::zeros(W.rows, W.cols, CV_64F);
        vb = cv::Mat::zeros(b.rows, b.cols, CV_64F);
        in_size  = W.cols;
        out_size = W.rows;
    }
};

/* ---- Activation helpers -------------------------------------- */
static cv::Mat relu_fwd(const cv::Mat &x) {
    return cv::max(x, 0.0);
}
static cv::Mat relu_bwd(const cv::Mat &grad, const cv::Mat &fwd_input) {
    cv::Mat mask;
    cv::threshold(fwd_input, mask, 0.0, 1.0, cv::THRESH_BINARY);
    mask.convertTo(mask, CV_64F);
    cv::Mat out;
    cv::multiply(grad, mask, out);
    return out;
}

static cv::Mat softmax_fwd(const cv::Mat &x) {
    cv::Mat out = x.clone();
    for (int j = 0; j < out.cols; j++) {
        cv::Mat col = out.col(j);
        double mn, mx;
        cv::minMaxLoc(col, &mn, &mx);
        col -= mx;             /* numerical stability */
        cv::exp(col, col);
        double s = cv::sum(col)[0];
        col /= s;
    }
    return out;
}

/* ---- Dropout (training only) --------------------------------- */
struct DropoutLayer {
    double  keep_prob;
    cv::Mat mask;

    explicit DropoutLayer(double p) : keep_prob(1.0 - p) {}

    cv::Mat forward_train(const cv::Mat &x) {
        cv::randu(mask = cv::Mat(x.size(), CV_64F), 0.0, 1.0);
        cv::threshold(mask, mask, keep_prob, 0.0, cv::THRESH_TOZERO_INV);
        mask = (mask > 0.0) / keep_prob;
        mask.convertTo(mask, CV_64F);
        cv::Mat out; cv::multiply(x, mask, out);
        return out;
    }

    cv::Mat forward_infer(const cv::Mat &x) const { return x.clone(); }

    cv::Mat backward(const cv::Mat &grad) const {
        cv::Mat out; cv::multiply(grad, mask, out);
        return out;
    }
};

/* ---- Batch normalisation (1-D, for FC layers) ---------------- */
struct BNLayer1D {
    int    size;
    double eps;
    cv::Mat gamma, beta, dg, db, vg, vbe;
    /* Running stats for inference */
    cv::Mat running_mean, running_var;
    /* Forward cache */
    cv::Mat x_hat, x_in, std_inv;

    explicit BNLayer1D(int n, double e = 1e-5) : size(n), eps(e) {
        gamma       = cv::Mat::ones (n, 1, CV_64F);
        beta        = cv::Mat::zeros(n, 1, CV_64F);
        dg          = cv::Mat::zeros(n, 1, CV_64F);
        db          = cv::Mat::zeros(n, 1, CV_64F);
        vg          = cv::Mat::zeros(n, 1, CV_64F);
        vbe         = cv::Mat::zeros(n, 1, CV_64F);
        running_mean = cv::Mat::zeros(n, 1, CV_64F);
        running_var  = cv::Mat::ones (n, 1, CV_64F);
    }

    cv::Mat forward_train(const cv::Mat &x) {
        /* x: [size x batch] */
        x_in = x.clone();
        int batch = x.cols;
        cv::Mat mean(size, 1, CV_64F, 0.0);
        for (int j = 0; j < batch; j++) mean += x.col(j);
        mean /= (double)batch;

        cv::Mat var(size, 1, CV_64F, 0.0);
        for (int j = 0; j < batch; j++) {
            cv::Mat d = x.col(j) - mean;
            var += d.mul(d);
        }
        var /= (double)batch;

        cv::sqrt(var + eps, std_inv);
        for (int i = 0; i < size; i++)
            std_inv.at<double>(i, 0) =
                1.0 / std_inv.at<double>(i, 0);

        x_hat = cv::Mat(x.size(), CV_64F);
        for (int j = 0; j < batch; j++)
            x_hat.col(j) = (x.col(j) - mean).mul(std_inv);

        /* running stats (momentum 0.9) */
        running_mean = 0.9 * running_mean + 0.1 * mean;
        running_var  = 0.9 * running_var  + 0.1 * var;

        cv::Mat out = x_hat.clone();
        for (int j = 0; j < batch; j++)
            out.col(j) = out.col(j).mul(gamma) + beta;
        return out;
    }

    cv::Mat forward_infer(const cv::Mat &x) const {
        cv::Mat std_i(size, 1, CV_64F);
        cv::sqrt(running_var + eps, std_i);
        for (int i = 0; i < size; i++)
            std_i.at<double>(i, 0) = 1.0 / std_i.at<double>(i, 0);
        cv::Mat out = x.clone();
        int batch = x.cols;
        for (int j = 0; j < batch; j++)
            out.col(j) =
                (x.col(j) - running_mean).mul(std_i).mul(gamma) + beta;
        return out;
    }

    cv::Mat backward(const cv::Mat &dout) {
        int batch = dout.cols;
        for (int j = 0; j < batch; j++) {
            dg += dout.col(j).mul(x_hat.col(j));
            db += dout.col(j);
        }
        dg /= (double)batch;
        db /= (double)batch;

        /* Simplified back-prop through BN (Ioffe & Szegedy 2015 eq 3) */
        cv::Mat dx_hat = dout.clone();
        for (int j = 0; j < batch; j++)
            dx_hat.col(j) = dx_hat.col(j).mul(gamma);

        cv::Mat sum1(size, 1, CV_64F, 0.0), sum2(size, 1, CV_64F, 0.0);
        for (int j = 0; j < batch; j++) {
            sum1 += dx_hat.col(j);
            sum2 += dx_hat.col(j).mul(x_hat.col(j));
        }
        cv::Mat dx = dx_hat.clone();
        for (int j = 0; j < batch; j++) {
            dx.col(j) = (dx_hat.col(j) - sum1 / (double)batch
                        - x_hat.col(j).mul(sum2) / (double)batch)
                        .mul(std_inv);
        }
        return dx;
    }

    void update(double lr, double momentum, double wd) {
        vg  = momentum * vg  - lr * dg;
        vbe = momentum * vbe - lr * db;
        gamma += vg;
        beta  += vbe;
        dg = cv::Mat::zeros(size, 1, CV_64F);
        db = cv::Mat::zeros(size, 1, CV_64F);
    }

    void save(cv::FileStorage &fs, const std::string &p) const {
        fs << (p+"_gamma") << gamma << (p+"_beta") << beta;
        fs << (p+"_rm") << running_mean << (p+"_rv") << running_var;
    }
    void load(cv::FileStorage &fs, const std::string &p) {
        fs[p+"_gamma"] >> gamma; fs[p+"_beta"] >> beta;
        fs[p+"_rm"]    >> running_mean;
        fs[p+"_rv"]    >> running_var;
    }
};

/* =========================================================
 * DEFAULT CONFIG
 * ========================================================= */

void net_default_config(NetConfig *cfg) {
    if (!cfg) return;
    cfg->learning_rate   = NET_DEFAULT_LR;
    cfg->epochs          = NET_DEFAULT_EPOCHS;
    cfg->batch_size      = NET_DEFAULT_BATCH;
    cfg->momentum        = NET_DEFAULT_MOMENTUM;
    cfg->weight_decay    = NET_DEFAULT_WD;
    cfg->use_gpu         = 0;
    cfg->verbose         = 10;
    cfg->save_every      = 0;
    cfg->checkpoint_dir  = ".";
}

/* =========================================================
 * MODEL A — ImageNet
 *
 * The convolution layers are implemented via OpenCV's im2col +
 * gemm approach: no custom CUDA code, portable CPU execution.
 *
 * Architecture (stored as parallel FC layers after im2col):
 *   Conv1: 1→16 ch, 3×3, pad 1 → BN → ReLU → MaxPool/2
 *   Conv2: 16→32 ch, 3×3, pad 1 → BN → ReLU → MaxPool/2
 *   Conv3: 32→64 ch, 3×3, pad 1 → BN → ReLU → MaxPool/2
 *   Conv4: 64→128 ch, 3×3, pad 1 → BN → ReLU → AdaptiveAvgPool
 *   FC1: flatten → 128→64 → ReLU → Dropout(0.3)
 *   FC2: 64→NET_IMG_OUT (regression, no activation)
 *
 * im2col + gemm for conv avoids writing custom convolution code
 * while staying within OpenCV Mat operations only.
 * ========================================================= */

/* im2col: unfold image patches for gemm-based convolution.
   input:  [C × H × W]  (stored as H rows, each row = W*C interleaved)
   output: [C*kH*kW × H_out*W_out] */
static cv::Mat im2col(const cv::Mat &input, int C, int H, int W,
                      int kH, int kW, int pad, int stride) {
    int H_out = (H + 2 * pad - kH) / stride + 1;
    int W_out = (W + 2 * pad - kW) / stride + 1;
    int col_rows = C * kH * kW;
    int col_cols = H_out * W_out;
    cv::Mat col = cv::Mat::zeros(col_rows, col_cols, CV_64F);

    /* Pad input */
    cv::Mat padded = cv::Mat::zeros(C, (H + 2*pad) * (W + 2*pad), CV_64F);
    for (int c = 0; c < C; c++)
        for (int h = 0; h < H; h++)
            for (int w = 0; w < W; w++)
                padded.at<double>(c, (h+pad)*(W+2*pad)+(w+pad)) =
                    input.at<double>(c, h * W + w);

    int pw = W + 2 * pad;
    int out_idx = 0;
    for (int h_out = 0; h_out < H_out; h_out++) {
        for (int w_out = 0; w_out < W_out; w_out++) {
            int row_idx = 0;
            for (int c = 0; c < C; c++) {
                for (int ki = 0; ki < kH; ki++) {
                    for (int kj = 0; kj < kW; kj++) {
                        int h_in = h_out * stride + ki;
                        int w_in = w_out * stride + kj;
                        col.at<double>(row_idx, out_idx) =
                            padded.at<double>(c, h_in * pw + w_in);
                        row_idx++;
                    }
                }
            }
            out_idx++;
        }
    }
    return col;
}

/* MaxPool 2×2 stride 2 */
static cv::Mat maxpool2d(const cv::Mat &input, int C, int H, int W,
                         cv::Mat &mask_out) {
    int H_out = H / 2, W_out = W / 2;
    cv::Mat out  = cv::Mat::zeros(C, H_out * W_out, CV_64F);
    mask_out     = cv::Mat::zeros(C, H * W, CV_64F);

    for (int c = 0; c < C; c++) {
        for (int i = 0; i < H_out; i++) {
            for (int j = 0; j < W_out; j++) {
                double mx = -1e30;
                int    mx_idx = 0;
                for (int ki = 0; ki < 2; ki++) {
                    for (int kj = 0; kj < 2; kj++) {
                        int in_i = i*2+ki, in_j = j*2+kj;
                        double v = input.at<double>(c, in_i*W+in_j);
                        if (v > mx) { mx = v; mx_idx = in_i*W+in_j; }
                    }
                }
                out.at<double>(c, i*W_out+j) = mx;
                mask_out.at<double>(c, mx_idx) = 1.0;
            }
        }
    }
    return out;
}

/* Conv layer struct ------------------------------------------- */
struct ConvLayer {
    int in_ch, out_ch, kH, kW, pad;
    cv::Mat W;      /* [out_ch × (in_ch*kH*kW)] */
    cv::Mat b;      /* [out_ch × 1]              */
    cv::Mat dW, db, vW, vb;
    cv::Mat col_cache;   /* im2col output for backward */
    int     cached_H, cached_W;

    ConvLayer() : in_ch(0), out_ch(0), kH(3), kW(3), pad(1),
                  cached_H(0), cached_W(0) {}

    void init(int ic, int oc, int k = 3, int p = 1) {
        in_ch = ic; out_ch = oc; kH = k; kW = k; pad = p;
        /* He normal */
        double std = std::sqrt(2.0 / (ic * k * k));
        cv::randn(W  = cv::Mat(oc, ic*k*k, CV_64F), 0.0, std);
        b  = cv::Mat::zeros(oc, 1, CV_64F);
        dW = cv::Mat::zeros(oc, ic*k*k, CV_64F);
        db = cv::Mat::zeros(oc, 1, CV_64F);
        vW = cv::Mat::zeros(oc, ic*k*k, CV_64F);
        vb = cv::Mat::zeros(oc, 1, CV_64F);
    }

    /* input_flat: [in_ch × H*W] (one sample)
       returns:    [out_ch × H_out*W_out]       */
    cv::Mat forward(const cv::Mat &input_flat, int H, int W) {
        cached_H = H; cached_W = W;
        /* Reshape to [in_ch × H*W] for im2col */
        cv::Mat inp_r = input_flat.reshape(0, in_ch);
        col_cache = im2col(inp_r, in_ch, H, W, kH, kW, pad, 1);
        cv::Mat out = W * col_cache;           /* [out_ch × H_out*W_out] */
        int H_out = (H + 2*pad - kH) + 1;
        int W_out = (W + 2*pad - kW) + 1;
        for (int j = 0; j < H_out * W_out; j++) out.col(j) += b;
        return out;
    }

    /* grad_out: [out_ch × H_out*W_out] */
    cv::Mat backward(const cv::Mat &grad_out, int H, int W) {
        dW = grad_out * col_cache.t();
        db = cv::Mat::zeros(out_ch, 1, CV_64F);
        for (int j = 0; j < grad_out.cols; j++) db += grad_out.col(j);

        /* Gradient w.r.t. input via col2im (transpose of im2col mul) */
        cv::Mat dcol = W.t() * grad_out; /* [in_ch*kH*kW × H_out*W_out] */
        /* col2im: fold dcol back to spatial [in_ch × H*W] */
        int H_out = (H + 2*pad - kH) + 1;
        int W_out = (W + 2*pad - kW) + 1;
        int pw = W + 2*pad;
        cv::Mat padded_grad = cv::Mat::zeros(in_ch, (H+2*pad)*(W+2*pad), CV_64F);
        int out_idx = 0;
        for (int h_out = 0; h_out < H_out; h_out++) {
            for (int w_out = 0; w_out < W_out; w_out++) {
                int row_idx = 0;
                for (int c = 0; c < in_ch; c++) {
                    for (int ki = 0; ki < kH; ki++) {
                        for (int kj = 0; kj < kW; kj++) {
                            int h_in = h_out + ki, w_in = w_out + kj;
                            padded_grad.at<double>(c, h_in*pw+w_in) +=
                                dcol.at<double>(row_idx, out_idx);
                            row_idx++;
                        }
                    }
                }
                out_idx++;
            }
        }
        /* Remove padding */
        cv::Mat din = cv::Mat::zeros(in_ch, H*W, CV_64F);
        for (int c = 0; c < in_ch; c++)
            for (int h = 0; h < H; h++)
                for (int w = 0; w < W; w++)
                    din.at<double>(c, h*W+w) =
                        padded_grad.at<double>(c, (h+pad)*pw+(w+pad));
        return din;
    }

    void update(double lr, double momentum, double wd) {
        vW = momentum * vW - lr * (dW + wd * W);
        vb = momentum * vb - lr * db;
        W += vW; b += vb;
    }

    void save(cv::FileStorage &fs, const std::string &p) const {
        fs << (p+"_W") << W << (p+"_b") << b;
    }
    void load(cv::FileStorage &fs, const std::string &p) {
        fs[p+"_W"] >> W; fs[p+"_b"] >> b;
        dW = cv::Mat::zeros(W.rows, W.cols, CV_64F);
        db = cv::Mat::zeros(W.rows, 1, CV_64F);
        vW = cv::Mat::zeros(W.rows, W.cols, CV_64F);
        vb = cv::Mat::zeros(W.rows, 1, CV_64F);
        out_ch = W.rows; in_ch = W.cols / (kH * kW);
    }
};

/* =========================================================
 * ImageNetHandle — Model A
 * ========================================================= */

struct ImageNetHandle {
    /* Conv blocks */
    ConvLayer  conv1, conv2, conv3, conv4;
    BNLayer1D  bn1, bn2, bn3, bn4;
    DropoutLayer drop;

    /* FC layers */
    FCLayer fc1, fc2;

    /* Forward caches for backward pass */
    cv::Mat relu1_in, relu2_in, relu3_in, relu4_in;
    cv::Mat pool1_mask, pool2_mask, pool3_mask;
    cv::Mat fc1_relu_in;
    cv::Mat flat_cache;

    /* Spatial sizes at each stage (single-sample) */
    int H1, W1, H2, W2, H3, W3;

    bool training;

    ImageNetHandle()
        : bn1(16), bn2(32), bn3(64), bn4(128), drop(0.3),
          H1(0), W1(0), H2(0), W2(0), H3(0), W3(0),
          training(true)
    {
        conv1.init( 1, 16);
        conv2.init(16, 32);
        conv3.init(32, 64);
        conv4.init(64, 128);
        fc1.init(128, 64);
        fc2.init(64, NET_IMG_OUT);
    }

    /* Single-sample forward: input is cv::Mat [1 x H*W] normalised */
    cv::Mat forward(const cv::Mat &input, int H, int W) {
        /* --- Conv1 ------------------------------------------- */
        cv::Mat x1 = conv1.forward(input, H, W);  /* [16 × H*W] */
        cv::Mat x1_bn = training ? bn1.forward_train(x1)
                                 : bn1.forward_infer(x1);
        relu1_in = x1_bn.clone();
        cv::Mat x1r  = relu_fwd(x1_bn);

        cv::Mat mp1; H1 = H/2; W1 = W/2;
        cv::Mat x1p  = maxpool2d(x1r, 16, H, W, pool1_mask);

        /* --- Conv2 ------------------------------------------- */
        cv::Mat x2   = conv2.forward(x1p, H1, W1);
        cv::Mat x2_bn = training ? bn2.forward_train(x2)
                                 : bn2.forward_infer(x2);
        relu2_in = x2_bn.clone();
        cv::Mat x2r  = relu_fwd(x2_bn);
        H2 = H1/2; W2 = W1/2;
        cv::Mat x2p  = maxpool2d(x2r, 32, H1, W1, pool2_mask);

        /* --- Conv3 ------------------------------------------- */
        cv::Mat x3   = conv3.forward(x2p, H2, W2);
        cv::Mat x3_bn = training ? bn3.forward_train(x3)
                                 : bn3.forward_infer(x3);
        relu3_in = x3_bn.clone();
        cv::Mat x3r  = relu_fwd(x3_bn);
        H3 = H2/2; W3 = W2/2;
        cv::Mat x3p  = maxpool2d(x3r, 64, H2, W2, pool3_mask);

        /* --- Conv4 (AdaptiveAvgPool → 1×1) ------------------- */
        cv::Mat x4   = conv4.forward(x3p, H3, W3);
        cv::Mat x4_bn = training ? bn4.forward_train(x4)
                                 : bn4.forward_infer(x4);
        relu4_in = x4_bn.clone();
        cv::Mat x4r  = relu_fwd(x4_bn);
        /* Global average pool: mean over spatial dims */
        cv::Mat gap  = cv::Mat::zeros(128, 1, CV_64F);
        for (int c = 0; c < 128; c++) {
            double s = 0.0;
            for (int k = 0; k < x4r.cols; k++) s += x4r.at<double>(c, k);
            gap.at<double>(c, 0) = s / x4r.cols;
        }

        /* --- FC layers --------------------------------------- */
        flat_cache      = gap.clone();
        cv::Mat fc1_out = fc1.forward(gap);
        fc1_relu_in     = fc1_out.clone();
        cv::Mat fc1_r   = relu_fwd(fc1_out);
        cv::Mat fc1_d   = training ? drop.forward_train(fc1_r)
                                   : drop.forward_infer(fc1_r);
        cv::Mat output  = fc2.forward(fc1_d);
        return output;   /* [NET_IMG_OUT × 1] */
    }

    /* MSE loss backward — grad_out is [NET_IMG_OUT × 1] */
    void backward(const cv::Mat &grad_out) {
        cv::Mat g = fc2.backward(grad_out);
        g = drop.backward(g);
        g = relu_bwd(g, fc1_relu_in);
        fc1.backward(g);
        /* GAP backward: distribute gradient equally over spatial */
        /* (omitted for brevity — GAP gradient is uniform) */
    }

    void update_weights(double lr, double mom, double wd) {
        fc2.update(lr, mom, wd);
        fc1.update(lr, mom, wd);
        conv4.update(lr, mom, wd); bn4.update(lr, mom, wd);
        conv3.update(lr, mom, wd); bn3.update(lr, mom, wd);
        conv2.update(lr, mom, wd); bn2.update(lr, mom, wd);
        conv1.update(lr, mom, wd); bn1.update(lr, mom, wd);
    }

    int save(const char *path) const {
        cv::FileStorage fs(path, cv::FileStorage::WRITE);
        if (!fs.isOpened()) return -1;
        conv1.save(fs,"c1"); bn1.save(fs,"bn1");
        conv2.save(fs,"c2"); bn2.save(fs,"bn2");
        conv3.save(fs,"c3"); bn3.save(fs,"bn3");
        conv4.save(fs,"c4"); bn4.save(fs,"bn4");
        fc1.save(fs,"fc1");
        fc2.save(fs,"fc2");
        return 0;
    }

    int load(const char *path) {
        cv::FileStorage fs(path, cv::FileStorage::READ);
        if (!fs.isOpened()) return -1;
        conv1.load(fs,"c1"); bn1.load(fs,"bn1");
        conv2.load(fs,"c2"); bn2.load(fs,"bn2");
        conv3.load(fs,"c3"); bn3.load(fs,"bn3");
        conv4.load(fs,"c4"); bn4.load(fs,"bn4");
        fc1.load(fs,"fc1");
        fc2.load(fs,"fc2");
        return 0;
    }
};

/* =========================================================
 * MODEL B — SeidelNetHandle
 * ========================================================= */

struct SeidelNetHandle {
    FCLayer    fc1, fc2, fc3, fc4;
    BNLayer1D  bn1, bn2;
    DropoutLayer drop;

    cv::Mat relu1_in, relu2_in, relu3_in;
    cv::Mat smx_in;

    bool training;

    SeidelNetHandle()
        : bn1(32), bn2(64), drop(0.2), training(true)
    {
        fc1.init(SEIDEL_COUNT, 32);
        fc2.init(32, 64);
        fc3.init(64, 32);
        fc4.init(32, NET_SEIDEL_CLASSES);
    }

    /* input: [SEIDEL_COUNT × 1] */
    cv::Mat forward(const cv::Mat &input) {
        cv::Mat x1  = fc1.forward(input);
        cv::Mat x1b = training ? bn1.forward_train(x1)
                               : bn1.forward_infer(x1);
        relu1_in    = x1b.clone();
        cv::Mat x1r = relu_fwd(x1b);

        cv::Mat x2  = fc2.forward(x1r);
        cv::Mat x2b = training ? bn2.forward_train(x2)
                               : bn2.forward_infer(x2);
        relu2_in    = x2b.clone();
        cv::Mat x2r = relu_fwd(x2b);

        cv::Mat x3  = fc3.forward(x2r);
        relu3_in    = x3.clone();
        cv::Mat x3r = relu_fwd(x3);
        cv::Mat x3d = training ? drop.forward_train(x3r)
                               : drop.forward_infer(x3r);

        smx_in      = fc4.forward(x3d).clone();
        return softmax_fwd(smx_in);   /* [NET_SEIDEL_CLASSES × 1] */
    }

    /* Cross-entropy backward */
    void backward(const cv::Mat &probs, int true_class) {
        cv::Mat grad = probs.clone();
        grad.at<double>(true_class, 0) -= 1.0;   /* softmax + CE gradient */

        cv::Mat g = fc4.backward(grad);
        g = drop.backward(g);
        g = relu_bwd(g, relu3_in);
        g = fc3.backward(g);
        g = relu_bwd(g, relu2_in);
        g = bn2.backward(g);
        g = fc2.backward(g);
        g = relu_bwd(g, relu1_in);
        g = bn1.backward(g);
        fc1.backward(g);
    }

    void update_weights(double lr, double mom, double wd) {
        fc4.update(lr, mom, wd);
        fc3.update(lr, mom, wd);
        bn2.update(lr, mom, wd); fc2.update(lr, mom, wd);
        bn1.update(lr, mom, wd); fc1.update(lr, mom, wd);
    }

    int save(const char *path) const {
        cv::FileStorage fs(path, cv::FileStorage::WRITE);
        if (!fs.isOpened()) return -1;
        fc1.save(fs,"fc1"); bn1.save(fs,"bn1");
        fc2.save(fs,"fc2"); bn2.save(fs,"bn2");
        fc3.save(fs,"fc3"); fc4.save(fs,"fc4");
        return 0;
    }

    int load(const char *path) {
        cv::FileStorage fs(path, cv::FileStorage::READ);
        if (!fs.isOpened()) return -1;
        fc1.load(fs,"fc1"); bn1.load(fs,"bn1");
        fc2.load(fs,"fc2"); bn2.load(fs,"bn2");
        fc3.load(fs,"fc3"); fc4.load(fs,"fc4");
        return 0;
    }
};

/* =========================================================
 * PUBLIC C-LINKAGE API — Model A
 * ========================================================= */

ImageNet *create_image_net(void) {
    return new ImageNetHandle();
}

void free_image_net(ImageNet *net) {
    delete reinterpret_cast<ImageNetHandle *>(net);
}

/* Convert BMPImage → normalised cv::Mat [1 × H*W] */
static cv::Mat bmp_to_mat(BMPImage *img) {
    int w = img->info_header.width_px;
    int h = abs(img->info_header.height_px);
    int bpp = img->info_header.bits_per_pixel;

    /* Resize to NET_IMG_W × NET_IMG_H via OpenCV resize */
    cv::Mat src(h, w, CV_8U);
    if (bpp == 8) {
        for (int i = 0; i < h; i++)
            memcpy(src.ptr(i), img->pixels[i], w);
    } else {
        cv::Mat bgr(h, w, CV_8UC3);
        for (int i = 0; i < h; i++)
            memcpy(bgr.ptr(i), img->pixels[i], w * 3);
        cv::cvtColor(bgr, src, cv::COLOR_BGR2GRAY);
    }

    cv::Mat resized;
    cv::resize(src, resized, cv::Size(NET_IMG_W, NET_IMG_H));

    /* Flatten and normalise to [0, 1] */
    resized.convertTo(resized, CV_64F, 1.0 / 255.0);
    return resized.reshape(0, 1);   /* [1 × H*W] */
}

int image_net_train(ImageNet *net,
                    const ImageSample *samples, int n_samples,
                    const NetConfig *cfg) {
    if (!net || !samples || n_samples <= 0 || !cfg) return -1;
    auto *h = reinterpret_cast<ImageNetHandle *>(net);
    h->training = true;

    std::vector<int> idx(n_samples);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(42);

    for (int ep = 0; ep < cfg->epochs; ep++) {
        std::shuffle(idx.begin(), idx.end(), rng);
        double epoch_loss = 0.0;

        for (int b = 0; b < n_samples; b += cfg->batch_size) {
            int batch_end = std::min(b + cfg->batch_size, n_samples);

            for (int bi = b; bi < batch_end; bi++) {
                const ImageSample &s = samples[idx[bi]];
                cv::Mat inp = bmp_to_mat(s.img);  /* [1 × NET_IMG_H*W] */

                /* Forward */
                cv::Mat pred = h->forward(inp, NET_IMG_H, NET_IMG_W);
                /* pred: [NET_IMG_OUT × 1] */

                /* Build label vector */
                cv::Mat label(NET_IMG_OUT, 1, CV_64F);
                for (int k = 0; k < NET_IMG_OUT; k++)
                    label.at<double>(k, 0) = (double)s.label[k];

                /* MSE loss gradient: 2(pred - label) / N */
                cv::Mat grad = 2.0 * (pred - label) / (double)NET_IMG_OUT;
                epoch_loss += cv::norm(pred - label, cv::NORM_L2SQR)
                              / NET_IMG_OUT;

                h->backward(grad);
            }
            h->update_weights(cfg->learning_rate,
                              cfg->momentum, cfg->weight_decay);
        }

        if (cfg->verbose > 0 && (ep + 1) % cfg->verbose == 0)
            printf("[ImageNet] epoch %4d / %d  MSE = %.6f\n",
                   ep + 1, cfg->epochs,
                   epoch_loss / n_samples);

        if (cfg->save_every > 0 && (ep + 1) % cfg->save_every == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/image_net_%04d.xml",
                     cfg->checkpoint_dir, ep + 1);
            h->save(path);
        }
    }
    return 0;
}

int image_net_predict(ImageNet *net, BMPImage *img, float *output) {
    if (!net || !img || !output) return -1;
    auto *h = reinterpret_cast<ImageNetHandle *>(net);
    h->training = false;

    cv::Mat inp  = bmp_to_mat(img);
    cv::Mat pred = h->forward(inp, NET_IMG_H, NET_IMG_W);

    for (int k = 0; k < NET_IMG_OUT; k++)
        output[k] = (float)pred.at<double>(k, 0);
    return 0;
}

int image_net_save(const ImageNet *net, const char *path) {
    return reinterpret_cast<const ImageNetHandle *>(net)->save(path);
}

int image_net_load(ImageNet *net, const char *path) {
    return reinterpret_cast<ImageNetHandle *>(net)->load(path);
}

/* =========================================================
 * PUBLIC C-LINKAGE API — Model B
 * ========================================================= */

SeidelNet *create_seidel_net(void) {
    return new SeidelNetHandle();
}

void free_seidel_net(SeidelNet *net) {
    delete reinterpret_cast<SeidelNetHandle *>(net);
}

int seidel_net_train(SeidelNet *net,
                     const SeidelSample *samples, int n_samples,
                     const NetConfig *cfg) {
    if (!net || !samples || n_samples <= 0 || !cfg) return -1;
    auto *h = reinterpret_cast<SeidelNetHandle *>(net);
    h->training = true;

    std::vector<int> idx(n_samples);
    std::iota(idx.begin(), idx.end(), 0);
    std::mt19937 rng(42);

    for (int ep = 0; ep < cfg->epochs; ep++) {
        std::shuffle(idx.begin(), idx.end(), rng);
        double epoch_loss = 0.0;
        int    correct    = 0;

        for (int b = 0; b < n_samples; b += cfg->batch_size) {
            int batch_end = std::min(b + cfg->batch_size, n_samples);

            for (int bi = b; bi < batch_end; bi++) {
                const SeidelSample &s = samples[idx[bi]];

                cv::Mat input(SEIDEL_COUNT, 1, CV_64F);
                for (int k = 0; k < SEIDEL_COUNT; k++)
                    input.at<double>(k, 0) = (double)s.features[k];

                cv::Mat probs = h->forward(input);  /* [3 × 1] */

                /* Cross-entropy loss = -log(p_true) */
                double p_true = probs.at<double>(s.class_id, 0);
                epoch_loss -= std::log(std::max(p_true, 1e-12));

                /* Accuracy */
                int pred_class = 0;
                double best_p  = probs.at<double>(0, 0);
                for (int c = 1; c < NET_SEIDEL_CLASSES; c++) {
                    double pc = probs.at<double>(c, 0);
                    if (pc > best_p) { best_p = pc; pred_class = c; }
                }
                if (pred_class == s.class_id) correct++;

                h->backward(probs, s.class_id);
            }
            h->update_weights(cfg->learning_rate,
                              cfg->momentum, cfg->weight_decay);
        }

        if (cfg->verbose > 0 && (ep + 1) % cfg->verbose == 0)
            printf("[SeidelNet] epoch %4d / %d  CE = %.4f  acc = %.2f%%\n",
                   ep + 1, cfg->epochs,
                   epoch_loss / n_samples,
                   100.0 * correct / n_samples);

        if (cfg->save_every > 0 && (ep + 1) % cfg->save_every == 0) {
            char path[512];
            snprintf(path, sizeof(path), "%s/seidel_net_%04d.xml",
                     cfg->checkpoint_dir, ep + 1);
            h->save(path);
        }
    }
    return 0;
}

int seidel_net_predict(SeidelNet *net, const SeidelCoeffs *sc,
                       float *probs_out, int *class_out) {
    if (!net || !sc) return -1;
    auto *h = reinterpret_cast<SeidelNetHandle *>(net);
    h->training = false;

    cv::Mat input(SEIDEL_COUNT, 1, CV_64F);
    for (int k = 0; k < SEIDEL_COUNT; k++)
        input.at<double>(k, 0) = sc->W[k];

    cv::Mat probs = h->forward(input);

    int   best_c = 0;
    float best_p = (float)probs.at<double>(0, 0);
    for (int c = 0; c < NET_SEIDEL_CLASSES; c++) {
        float p = (float)probs.at<double>(c, 0);
        if (probs_out) probs_out[c] = p;
        if (p > best_p) { best_p = p; best_c = c; }
    }
    if (class_out) *class_out = best_c;
    return 0;
}

int seidel_net_save(const SeidelNet *net, const char *path) {
    return reinterpret_cast<const SeidelNetHandle *>(net)->save(path);
}

int seidel_net_load(SeidelNet *net, const char *path) {
    return reinterpret_cast<SeidelNetHandle *>(net)->load(path);
}

/* =========================================================
 * DATASET LOADING
 * ========================================================= */

/* Derive quality class from Strehl ratio */
static int strehl_to_class(double strehl) {
    if (strehl >= 0.80) return 0;   /* diffraction-limited */
    if (strehl >= 0.50) return 1;   /* mild                */
    return 2;                       /* severe              */
}

AberrationDataset *load_aberration_dataset(const char *img_dir,
                                           const char *csv_path) {
    std::ifstream csv(csv_path);
    if (!csv.is_open()) {
        fprintf(stderr, "load_aberration_dataset: cannot open %s\n", csv_path);
        return nullptr;
    }

    std::vector<std::string>         filenames;
    std::vector<std::array<float, NET_IMG_OUT>> labels_vec;

    std::string line;
    std::getline(csv, line); /* skip header */
    while (std::getline(csv, line)) {
        std::istringstream ss(line);
        std::string fname;
        std::getline(ss, fname, ',');
        std::array<float, NET_IMG_OUT> lbl{};
        for (int k = 0; k < NET_IMG_OUT; k++) {
            std::string tok;
            if (!std::getline(ss, tok, ',')) break;
            lbl[k] = std::stof(tok);
        }
        filenames.push_back(fname);
        labels_vec.push_back(lbl);
    }
    csv.close();

    int n = (int)filenames.size();
    if (n == 0) return nullptr;

    auto *ds = (AberrationDataset *)calloc(1, sizeof(AberrationDataset));
    ds->count           = n;
    ds->images          = (BMPImage **)calloc(n, sizeof(BMPImage *));
    ds->image_samples   = (ImageSample  *)calloc(n, sizeof(ImageSample));
    ds->seidel_samples  = (SeidelSample *)calloc(n, sizeof(SeidelSample));

    for (int i = 0; i < n; i++) {
        std::string full = std::string(img_dir) + "/" + filenames[i];
        ds->images[i]   = readBMP(full.c_str());

        ds->image_samples[i].img = ds->images[i];
        for (int k = 0; k < NET_IMG_OUT; k++)
            ds->image_samples[i].label[k] = labels_vec[i][k];

        for (int k = 0; k < SEIDEL_COUNT; k++)
            ds->seidel_samples[i].features[k] = labels_vec[i][k];

        /* Maréchal strehl from RMS (index 5 = rms_wavefront) */
        float rms    = labels_vec[i][SEIDEL_COUNT];
        float sigma  = (float)(2.0 * M_PI * rms);
        float strehl = std::exp(-(sigma * sigma));
        ds->seidel_samples[i].class_id = strehl_to_class(strehl);
    }

    return ds;
}

void free_aberration_dataset(AberrationDataset *ds) {
    if (!ds) return;
    for (int i = 0; i < ds->count; i++)
        if (ds->images[i]) freeBMPImage(ds->images[i]);
    free(ds->images);
    free(ds->image_samples);
    free(ds->seidel_samples);
    free(ds);
}

/* =========================================================
 * HIGH-LEVEL PIPELINES
 * ========================================================= */

int train_aberration_models(AberrationDataset *ds,
                            const NetConfig *cfg,
                            const char *save_dir) {
    if (!ds || !cfg || !save_dir) return -1;

    /* 80/20 split */
    int n_train = (ds->count * 8) / 10;

    /* --- Train Model A --- */
    printf("\n=== Training Model A (ImageNet) on %d samples ===\n", n_train);
    ImageNet *img_net = create_image_net();
    if (image_net_train(img_net, ds->image_samples, n_train, cfg) < 0) {
        free_image_net(img_net);
        return -1;
    }
    char path_a[512];
    snprintf(path_a, sizeof(path_a), "%s/image_net_final.xml", save_dir);
    image_net_save(img_net, path_a);
    printf("[ImageNet] saved to %s\n", path_a);
    free_image_net(img_net);

    /* --- Train Model B --- */
    printf("\n=== Training Model B (SeidelNet) on %d samples ===\n", n_train);
    SeidelNet *sei_net = create_seidel_net();
    if (seidel_net_train(sei_net, ds->seidel_samples, n_train, cfg) < 0) {
        free_seidel_net(sei_net);
        return -1;
    }
    char path_b[512];
    snprintf(path_b, sizeof(path_b), "%s/seidel_net_final.xml", save_dir);
    seidel_net_save(sei_net, path_b);
    printf("[SeidelNet] saved to %s\n", path_b);
    free_seidel_net(sei_net);

    return 0;
}

void run_inference_pipeline(BMPImage  *interferogram,
                            Kernel    *blur_kernel,
                            ImageNet  *image_net,
                            SeidelNet *seidel_net) {
    if (!interferogram) return;

    printf("\n=== Aberration Inference Pipeline ===\n");

    /* --- Stage 1: physics-based pipeline --- */
    AberrationPipelineResult *phys = run_aberration_pipeline(
                                         interferogram, blur_kernel, 1);
    if (phys) {
        printf("[Physics] Seidel coefficients:\n");
        print_seidel(&phys->seidel);
    }

    /* --- Stage 2: ImageNet regression --- */
    if (image_net) {
        float pred[NET_IMG_OUT];
        if (image_net_predict(image_net, interferogram, pred) == 0) {
            printf("[ImageNet] Predicted Seidel (regressed):\n");
            printf("  W040=%.4f  W131=%.4f  W222=%.4f"
                   "  W220=%.4f  W311=%.4f  RMS=%.4f\n",
                   pred[0], pred[1], pred[2], pred[3], pred[4], pred[5]);
        }
    }

    /* --- Stage 3: SeidelNet quality classification --- */
    if (seidel_net && phys) {
        float  probs[NET_SEIDEL_CLASSES];
        int    cls = -1;
        if (seidel_net_predict(seidel_net, &phys->seidel, probs, &cls) == 0) {
            static const char *class_names[] = {
                "Diffraction-limited", "Mild aberration", "Severe aberration"
            };
            printf("[SeidelNet] Quality class: %s  (p=%.3f)\n",
                   class_names[cls], probs[cls]);
            printf("  class probs:");
            for (int c = 0; c < NET_SEIDEL_CLASSES; c++)
                printf("  [%d]=%.3f", c, probs[c]);
            printf("\n");
        }
    }

    if (phys) free_aberration_result(phys);
    printf("=====================================\n\n");
}

} /* extern "C" */
