/* aberration.c
 *
 * Implementation of the aberration ISP pipeline.
 *
 * Corrected stage order:
 *   1a. Wiener deconvolution  — on the raw interferogram, so the noise
 *       estimate reflects true acquisition noise, not a pre-smoothed image.
 *   1b. Wavelet denoise       — cleans up noise amplified by the Wiener
 *       filter near OTF nulls.  Threshold is auto-estimated from the
 *       deconvolved image, which is appropriate because noise statistics
 *       change after deconvolution.
 *   2.  WVD phase extraction
 *   3.  Phase unwrapping
 *   4.  Zernike fit → Seidel coefficients
 *
 * Build dependency: links against fftw3 (already a project dependency).
 * No new external libraries required.
 */

#define _POSIX_C_SOURCE 200809L

#include "aberration.h"
#include <transforms.h>   /* Kernel, Filter, WaveletTransform, etc.  */
#include <imgproc.h>       /* calculate_stats_generic, combine_images  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fftw3.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* =========================================================
 * INTERNAL HELPERS  (mirrors style in transforms.c / bmp.c)
 * ========================================================= */

static double clampd(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static double **alloc_2d_d(int rows, int cols) {
    double **arr;
    double  *block;
    int      i;
    arr   = (double **)malloc(rows * sizeof(double *));
    if (!arr) return NULL;
    block = (double *)calloc(rows * cols, sizeof(double));
    if (!block) { free(arr); return NULL; }
    for (i = 0; i < rows; i++) arr[i] = block + i * cols;
    return arr;
}

static void free_2d_d(double **arr) {
    if (!arr) return;
    free(arr[0]);
    free(arr);
}

/* Wrap a phase value into (-pi, pi] */
static double wrap_phase(double p) {
    while (p >  M_PI) p -= 2.0 * M_PI;
    while (p <= -M_PI) p += 2.0 * M_PI;
    return p;
}

/* =========================================================
 * SECTION 1 — WIGNER-VILLE DISTRIBUTION
 * ========================================================= */

WVDResult *create_wvd(int n_samples) {
    WVDResult *wvd = (WVDResult *)malloc(sizeof(WVDResult));
    if (!wvd) return NULL;
    wvd->n_samples = n_samples;
    wvd->n_freqs   = n_samples;
    wvd->data      = alloc_2d_d(n_samples, n_samples);
    if (!wvd->data) { free(wvd); return NULL; }
    return wvd;
}

void free_wvd(WVDResult *wvd) {
    if (!wvd) return;
    free_2d_d(wvd->data);
    free(wvd);
}

/*
 * compute_wvd_1d
 *
 * Discrete Pseudo Wigner-Ville Distribution (DPWVD) using a
 * rectangular lag window of half-length `half_win`.
 *
 * Algorithm:
 *   For each time index n:
 *     Compute the local auto-correlation kernel:
 *       R(n, tau) = x(n+tau) * conj(x(n-tau)),  |tau| <= half_win
 *     Zero-pad to n_samples, then FFT over tau.
 *     The real part of the FFT gives the WVD slice.
 *
 * The analytic signal is computed via the Hilbert transform
 * (one-sided FFT zero-padding) before computing the WVD proper.
 */
WVDResult *compute_wvd_1d(const double *x, int n, int half_win) {
    WVDResult    *wvd;
    fftw_complex *X, *xa;       /* FFT of x, analytic signal              */
    fftw_complex *kernel_buf;   /* local auto-correlation for one slice   */
    fftw_complex *fft_out;
    fftw_plan     p_fwd, p_ker;
    int           i, tau, n_pos, n_neg;
    double        re_pos, im_pos, re_neg, im_neg;

    if (!x || n <= 0) return NULL;

    if (half_win <= 0) half_win = n / 4;
    if (half_win < 1)  half_win = 1;

    wvd = create_wvd(n);
    if (!wvd) return NULL;

    /* --- Step 1: compute analytic signal via one-sided FFT ---------- */
    X   = fftw_alloc_complex(n);
    xa  = fftw_alloc_complex(n);
    if (!X || !xa) { free_wvd(wvd); fftw_free(X); fftw_free(xa); return NULL; }

    /* Load real signal */
    for (i = 0; i < n; i++) { X[i][0] = x[i]; X[i][1] = 0.0; }

    /* Forward FFT */
    p_fwd = fftw_plan_dft_1d(n, X, xa, FFTW_FORWARD, FFTW_ESTIMATE);
    fftw_execute(p_fwd);
    fftw_destroy_plan(p_fwd);

    /* Zero negative frequencies (indices n/2+1 … n-1) */
    for (i = n / 2 + 1; i < n; i++) { xa[i][0] = 0.0; xa[i][1] = 0.0; }
    /* Double positive frequencies to preserve energy */
    for (i = 1; i < n / 2; i++) { xa[i][0] *= 2.0; xa[i][1] *= 2.0; }

    /* Inverse FFT → analytic signal in X (reuse buffer) */
    p_fwd = fftw_plan_dft_1d(n, xa, X, FFTW_BACKWARD, FFTW_ESTIMATE);
    fftw_execute(p_fwd);
    fftw_destroy_plan(p_fwd);

    /* Normalise */
    for (i = 0; i < n; i++) { X[i][0] /= n; X[i][1] /= n; }

    fftw_free(xa);

    /* --- Step 2: compute WVD slices --------------------------------- */
    kernel_buf = fftw_alloc_complex(n);
    fft_out    = fftw_alloc_complex(n);
    if (!kernel_buf || !fft_out) {
        free_wvd(wvd);
        fftw_free(X);
        fftw_free(kernel_buf);
        fftw_free(fft_out);
        return NULL;
    }

    p_ker = fftw_plan_dft_1d(n, kernel_buf, fft_out,
                              FFTW_FORWARD, FFTW_ESTIMATE);

    for (i = 0; i < n; i++) {
        /* Zero kernel buffer */
        memset(kernel_buf, 0, n * sizeof(fftw_complex));

        for (tau = -half_win; tau <= half_win; tau++) {
            n_pos = i + tau;
            n_neg = i - tau;

            /* Mirror boundary extension */
            if (n_pos < 0) n_pos = -n_pos;
            if (n_neg < 0) n_neg = -n_neg;
            if (n_pos >= n) n_pos = 2 * n - 2 - n_pos;
            if (n_neg >= n) n_neg = 2 * n - 2 - n_neg;

            n_pos = (int)clampd(n_pos, 0, n - 1);
            n_neg = (int)clampd(n_neg, 0, n - 1);

            /* R(tau) = x_a(n+tau) * conj(x_a(n-tau)) */
            re_pos = X[n_pos][0]; im_pos = X[n_pos][1];
            re_neg = X[n_neg][0]; im_neg = X[n_neg][1];

            /* Index mapping: positive tau → index tau, negative → n+tau */
            int idx = (tau >= 0) ? tau : (n + tau);
            kernel_buf[idx][0] = re_pos * re_neg + im_pos * im_neg;
            kernel_buf[idx][1] = im_pos * re_neg - re_pos * im_neg;
        }

        fftw_execute(p_ker);

        /* Store real part of FFT (WVD is real for analytic signals) */
        for (int f = 0; f < n; f++)
            wvd->data[f][i] = fft_out[f][0];
    }

    fftw_destroy_plan(p_ker);
    fftw_free(kernel_buf);
    fftw_free(fft_out);
    fftw_free(X);

    return wvd;
}

void wvd_instantaneous_frequency(const WVDResult *wvd, double *if_out) {
    int    n, nf, i, f, peak_f;
    double peak_val;

    n  = wvd->n_samples;
    nf = wvd->n_freqs;

    for (i = 0; i < n; i++) {
        peak_f   = 0;
        peak_val = wvd->data[0][i];
        /* Search only first half (positive frequencies) */
        for (f = 1; f < nf / 2; f++) {
            if (wvd->data[f][i] > peak_val) {
                peak_val = wvd->data[f][i];
                peak_f   = f;
            }
        }
        /* Normalise to [0, 0.5] cycles/sample */
        if_out[i] = (double)peak_f / (double)nf;
    }
}

/* =========================================================
 * SECTION 2 — PHASE EXTRACTION
 * ========================================================= */

PhaseMap *create_phase_map(int w, int h) {
    PhaseMap *pm = (PhaseMap *)malloc(sizeof(PhaseMap));
    if (!pm) return NULL;
    pm->width   = w;
    pm->height  = h;
    pm->quality = NULL;
    pm->data    = alloc_2d_d(h, w);
    if (!pm->data) { free(pm); return NULL; }
    return pm;
}

void free_phase_map(PhaseMap *pm) {
    if (!pm) return;
    free_2d_d(pm->data);
    if (pm->quality) free_2d_d(pm->quality);
    free(pm);
}

/*
 * extract_phase_wvd
 *
 * For each row of the interferogram:
 *   1. Extract intensity signal
 *   2. Compute DPWVD
 *   3. Find instantaneous frequency ridge
 *   4. Integrate IF → wrapped phase
 *
 * The DC fringe frequency is removed by subtracting the mean IF.
 */
PhaseMap *extract_phase_wvd(BMPImage *interferogram, int half_win) {
    int       w, h, i, j;
    PhaseMap *pm;
    double   *row_signal, *if_row;
    double    mean_if, phase_acc;
    WVDResult *wvd;

    if (!interferogram) return NULL;

    w  = interferogram->info_header.width_px;
    h  = abs(interferogram->info_header.height_px);
    pm = create_phase_map(w, h);
    if (!pm) return NULL;

    row_signal = (double *)malloc(w * sizeof(double));
    if_row     = (double *)malloc(w * sizeof(double));
    if (!row_signal || !if_row) {
        free(row_signal); free(if_row);
        free_phase_map(pm);
        return NULL;
    }

    for (i = 0; i < h; i++) {
        /* Extract normalised row signal */
        if (interferogram->info_header.bits_per_pixel == 24) {
            for (j = 0; j < w; j++) {
                row_signal[j] = (0.299 * interferogram->pixels[i][j*3+2]
                               + 0.587 * interferogram->pixels[i][j*3+1]
                               + 0.114 * interferogram->pixels[i][j*3+0])
                               / 255.0;
            }
        } else {
            for (j = 0; j < w; j++)
                row_signal[j] = interferogram->pixels[i][j] / 255.0;
        }

        /* Compute WVD and extract IF ridge */
        wvd = compute_wvd_1d(row_signal, w, half_win);
        if (!wvd) {
            /* Fallback: zero phase */
            for (j = 0; j < w; j++) pm->data[i][j] = 0.0;
            continue;
        }
        wvd_instantaneous_frequency(wvd, if_row);
        free_wvd(wvd);

        /* Remove mean carrier frequency */
        mean_if = 0.0;
        for (j = 0; j < w; j++) mean_if += if_row[j];
        mean_if /= w;
        for (j = 0; j < w; j++) if_row[j] -= mean_if;

        /* Integrate IF → phase; wrap into (-pi, pi] */
        phase_acc = 0.0;
        for (j = 0; j < w; j++) {
            phase_acc += 2.0 * M_PI * if_row[j];
            pm->data[i][j] = wrap_phase(phase_acc);
        }
    }

    free(row_signal);
    free(if_row);
    return pm;
}

/*
 * compute_phase_quality
 *
 * Quality metric: 1 / (1 + local phase derivative variance).
 * A smooth, slowly-varying phase → quality near 1.
 * Noisy, rapidly-varying fringe → quality near 0.
 */
void compute_phase_quality(PhaseMap *pm) {
    int    w, h, i, j, ni, nj, ci, cj;
    double sum, dx, val;
    const int radius = 2;

    if (!pm) return;

    w = pm->width;
    h = pm->height;

    if (!pm->quality) {
        pm->quality = alloc_2d_d(h, w);
        if (!pm->quality) return;
    }

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            sum = 0.0;
            int count = 0;
            for (ci = -radius; ci <= radius; ci++) {
                for (cj = -radius; cj <= radius; cj++) {
                    if (ci == 0 && cj == 0) continue;
                    ni = (int)clampd(i + ci, 0, h - 1);
                    nj = (int)clampd(j + cj, 0, w - 1);
                    dx  = wrap_phase(pm->data[ni][nj] - pm->data[i][j]);
                    sum += dx * dx;
                    count++;
                }
            }
            val = (count > 0) ? (sum / count) : 0.0;
            /* Map variance to [0,1]: high variance → low quality */
            pm->quality[i][j] = 1.0 / (1.0 + val);
        }
    }
}

/* =========================================================
 * SECTION 3 — PHASE UNWRAPPING
 * ========================================================= */

/*
 * unwrap_phase_simple  — Itoh row-then-column
 */
void unwrap_phase_simple(PhaseMap *pm) {
    int    w, h, i, j;
    double diff;

    if (!pm) return;
    w = pm->width;
    h = pm->height;

    /* Unwrap along rows */
    for (i = 0; i < h; i++) {
        for (j = 1; j < w; j++) {
            diff = pm->data[i][j] - pm->data[i][j - 1];
            diff = wrap_phase(diff);
            pm->data[i][j] = pm->data[i][j - 1] + diff;
        }
    }

    /* Unwrap along columns */
    for (j = 0; j < w; j++) {
        for (i = 1; i < h; i++) {
            diff = pm->data[i][j] - pm->data[i - 1][j];
            diff = wrap_phase(diff);
            pm->data[i][j] = pm->data[i - 1][j] + diff;
        }
    }
}

/*
 * unwrap_phase_quality — quality-guided flood-fill (Ghiglia & Pritt)
 *
 * Priority queue implemented as a simple binary-heap of (quality, idx)
 * pairs.  Pixels are processed highest-quality-first so that the
 * unwrapping path follows the most reliable phase gradient.
 */

typedef struct { double q; int idx; } QNode;

static void heap_push(QNode *heap, int *size, QNode node) {
    int i = (*size)++;
    heap[i] = node;
    /* Sift up */
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (heap[parent].q >= heap[i].q) break;
        QNode tmp = heap[parent]; heap[parent] = heap[i]; heap[i] = tmp;
        i = parent;
    }
}

static QNode heap_pop(QNode *heap, int *size) {
    QNode top = heap[0];
    heap[0] = heap[--(*size)];
    int i = 0;
    for (;;) {
        int l = 2*i+1, r = 2*i+2, best = i;
        if (l < *size && heap[l].q > heap[best].q) best = l;
        if (r < *size && heap[r].q > heap[best].q) best = r;
        if (best == i) break;
        QNode tmp = heap[best]; heap[best] = heap[i]; heap[i] = tmp;
        i = best;
    }
    return top;
}

void unwrap_phase_quality(PhaseMap *pm) {
    int     w, h, N, i, j, n_heap;
    int    *visited;
    QNode  *heap;
    double  diff;

    if (!pm || !pm->quality) {
        unwrap_phase_simple(pm);
        return;
    }

    w = pm->width;
    h = pm->height;
    N = w * h;

    visited = (int *)calloc(N, sizeof(int));
    heap    = (QNode *)malloc(N * sizeof(QNode));
    if (!visited || !heap) {
        free(visited); free(heap);
        unwrap_phase_simple(pm);
        return;
    }

    n_heap = 0;

    /* Start seed at highest-quality pixel */
    {
        double best_q = -1.0;
        int    best_i = 0, best_j = 0;
        for (i = 0; i < h; i++) {
            for (j = 0; j < w; j++) {
                if (pm->quality[i][j] > best_q) {
                    best_q = pm->quality[i][j];
                    best_i = i; best_j = j;
                }
            }
        }
        visited[best_i * w + best_j] = 1;
        /* Push 4-connected neighbours of seed */
        const int di[4] = {-1,1,0,0};
        const int dj[4] = {0,0,-1,1};
        for (int d = 0; d < 4; d++) {
            int ni = best_i + di[d];
            int nj = best_j + dj[d];
            if (ni < 0 || ni >= h || nj < 0 || nj >= w) continue;
            int nidx = ni * w + nj;
            if (visited[nidx]) continue;
            QNode node;
            node.q   = pm->quality[ni][nj];
            node.idx = nidx;
            heap_push(heap, &n_heap, node);
        }
    }

    /* Flood fill */
    const int di[4] = {-1,1,0,0};
    const int dj[4] = {0,0,-1,1};

    while (n_heap > 0) {
        QNode cur = heap_pop(heap, &n_heap);
        int idx = cur.idx;
        if (visited[idx]) continue;
        visited[idx] = 1;

        i = idx / w;
        j = idx % w;

        /* Find best visited neighbour */
        double best_phase = pm->data[i][j];
        int    found = 0;
        for (int d = 0; d < 4; d++) {
            int ni = i + di[d], nj = j + dj[d];
            if (ni < 0 || ni >= h || nj < 0 || nj >= w) continue;
            if (!visited[ni * w + nj]) continue;
            diff = wrap_phase(pm->data[i][j] - pm->data[ni][nj]);
            best_phase = pm->data[ni][nj] + diff;
            found = 1;
            break;
        }
        if (found) pm->data[i][j] = best_phase;

        /* Enqueue unvisited neighbours */
        for (int d = 0; d < 4; d++) {
            int ni = i + di[d], nj = j + dj[d];
            if (ni < 0 || ni >= h || nj < 0 || nj >= w) continue;
            int nidx = ni * w + nj;
            if (visited[nidx]) continue;
            QNode node;
            node.q   = pm->quality[ni][nj];
            node.idx = nidx;
            heap_push(heap, &n_heap, node);
        }
    }

    free(visited);
    free(heap);
}

/* =========================================================
 * SECTION 4 — ZERNIKE POLYNOMIALS
 * ========================================================= */

/*
 * OSA/ANSI single-index Zernike polynomial Z_j(rho, theta).
 */

static double zernike_radial(int n, int m, double rho) {
    int    s, nm = abs(m), k = (n - nm) / 2;
    double sum = 0.0, rho_pow;

    if ((n - nm) % 2 != 0) return 0.0;

    for (s = 0; s <= k; s++) {
        rho_pow = pow(rho, (double)(n - 2 * s));
        double lgnum = lgamma(n - s + 1);
        double lgden = lgamma(s + 1)
                     + lgamma((n + nm) / 2 - s + 1)
                     + lgamma((n - nm) / 2 - s + 1);
        double coeff = ((s % 2 == 0) ? 1.0 : -1.0) * exp(lgnum - lgden);
        sum += coeff * rho_pow;
    }
    return sum;
}

static void osa_index_to_nm(int j, int *n_out, int *m_out) {
    int n = (int)ceil((-3.0 + sqrt(9.0 + 8.0 * j)) / 2.0);
    int m = 2 * j - n * (n + 2);
    *n_out = n;
    *m_out = m;
}

static double eval_zernike(int j, double rho, double theta) {
    int    n, m;
    double R, Z;

    osa_index_to_nm(j, &n, &m);
    R = zernike_radial(n, m, rho);

    if (m == 0) {
        Z = sqrt((double)(n + 1)) * R;
    } else if (m > 0) {
        Z = sqrt(2.0 * (n + 1)) * R * cos((double)m * theta);
    } else {
        Z = sqrt(2.0 * (n + 1)) * R * sin((double)(-m) * theta);
    }
    return Z;
}

ZernikeBasis *create_zernike_basis(int w, int h, int n_terms) {
    ZernikeBasis *zb;
    double        cx, cy, rho, theta, x_n, y_n, max_r;
    int           i, j, k, idx, n_pupil;

    zb = (ZernikeBasis *)malloc(sizeof(ZernikeBasis));
    if (!zb) return NULL;

    zb->n_terms = n_terms;
    zb->width   = w;
    zb->height  = h;
    zb->basis   = (double **)malloc(n_terms * sizeof(double *));
    zb->pupil_mask = (int *)calloc(w * h, sizeof(int));

    if (!zb->basis || !zb->pupil_mask) {
        free(zb->basis); free(zb->pupil_mask); free(zb);
        return NULL;
    }

    for (k = 0; k < n_terms; k++) {
        zb->basis[k] = (double *)calloc(w * h, sizeof(double));
        if (!zb->basis[k]) {
            for (int kk = 0; kk < k; kk++) free(zb->basis[kk]);
            free(zb->basis); free(zb->pupil_mask); free(zb);
            return NULL;
        }
    }

    cx    = (w - 1) / 2.0;
    cy    = (h - 1) / 2.0;
    max_r = (cx < cy) ? cx : cy;

    n_pupil = 0;
    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            x_n = (j - cx) / max_r;
            y_n = (i - cy) / max_r;
            rho = sqrt(x_n * x_n + y_n * y_n);

            idx = i * w + j;
            if (rho > 1.0) {
                zb->pupil_mask[idx] = 0;
                continue;
            }
            zb->pupil_mask[idx] = 1;
            n_pupil++;

            theta = atan2(y_n, x_n);
            for (k = 0; k < n_terms; k++)
                zb->basis[k][idx] = eval_zernike(k, rho, theta);
        }
    }
    zb->n_pupil = n_pupil;
    return zb;
}

void free_zernike_basis(ZernikeBasis *zb) {
    int k;
    if (!zb) return;
    for (k = 0; k < zb->n_terms; k++) free(zb->basis[k]);
    free(zb->basis);
    free(zb->pupil_mask);
    free(zb);
}

static void cholesky_solve(double *A, double *b, int n) {
    int i, j, k;
    double sum;

    for (i = 0; i < n; i++) {
        for (j = 0; j <= i; j++) {
            sum = A[i * n + j];
            for (k = 0; k < j; k++) sum -= A[i * n + k] * A[j * n + k];
            if (i == j) {
                A[i * n + i] = (sum > 0.0) ? sqrt(sum) : sqrt(fabs(sum) + 1e-12);
            } else {
                A[i * n + j] = sum / A[j * n + j];
            }
        }
    }

    for (i = 0; i < n; i++) {
        sum = b[i];
        for (k = 0; k < i; k++) sum -= A[i * n + k] * b[k];
        b[i] = sum / A[i * n + i];
    }

    for (i = n - 1; i >= 0; i--) {
        sum = b[i];
        for (k = i + 1; k < n; k++) sum -= A[k * n + i] * b[k];
        b[i] = sum / A[i * n + i];
    }
}

void fit_zernike(const PhaseMap *phase, const ZernikeBasis *zb,
                 double *coeffs_out) {
    int     n = zb->n_terms, N = zb->width * zb->height;
    double *ZtZ, *Ztphi;
    int     i, j, k;
    double  phi_val;

    ZtZ   = (double *)calloc(n * n, sizeof(double));
    Ztphi = (double *)calloc(n,     sizeof(double));
    if (!ZtZ || !Ztphi) { free(ZtZ); free(Ztphi); return; }

    for (i = 0; i < N; i++) {
        if (!zb->pupil_mask[i]) continue;
        int row = i / zb->width;
        int col = i % zb->width;
        phi_val = phase->data[row][col];

        for (j = 0; j < n; j++) {
            Ztphi[j] += zb->basis[j][i] * phi_val;
            for (k = j; k < n; k++) {
                ZtZ[j * n + k] += zb->basis[j][i] * zb->basis[k][i];
            }
        }
    }

    for (j = 0; j < n; j++)
        for (k = j + 1; k < n; k++)
            ZtZ[k * n + j] = ZtZ[j * n + k];

    for (j = 0; j < n; j++) ZtZ[j * n + j] += 1e-10;

    memcpy(coeffs_out, Ztphi, n * sizeof(double));
    cholesky_solve(ZtZ, coeffs_out, n);

    free(ZtZ);
    free(Ztphi);
}

/* =========================================================
 * SECTION 5 — ZERNIKE → SEIDEL CONVERSION
 * ========================================================= */

void zernike_to_seidel(const double *zc, SeidelCoeffs *sc) {
    double c4, c5, c6, c7, c8, c11;
    double sum_sq, sigma;
    int    k;

    if (!zc || !sc) return;

    c4  = (ZERNIKE_TERMS >  4) ? zc[4]  : 0.0;
    c5  = (ZERNIKE_TERMS >  5) ? zc[5]  : 0.0;
    c6  = (ZERNIKE_TERMS >  6) ? zc[6]  : 0.0;
    c7  = (ZERNIKE_TERMS >  7) ? zc[7]  : 0.0;
    c8  = (ZERNIKE_TERMS >  8) ? zc[8]  : 0.0;
    c11 = (ZERNIKE_TERMS > 11) ? zc[11] : 0.0;

    sc->W[0] = c11 / (6.0 * sqrt(5.0));
    sc->W[1] = sqrt(c7*c7 + c8*c8) / (2.0 * sqrt(8.0) / 3.0);
    sc->W[2] = sqrt(c5*c5 + c6*c6) / (2.0 * sqrt(6.0));
    sc->W[3] = c4 / (2.0 * sqrt(3.0)) - sc->W[2] / 2.0;
    sc->W[4] = 0.0;

    sum_sq = 0.0;
    for (k = 1; k < ZERNIKE_TERMS; k++) sum_sq += zc[k] * zc[k];
    sc->rms_wavefront = sqrt(sum_sq);

    sigma      = 2.0 * M_PI * sc->rms_wavefront;
    sc->strehl = exp(-(sigma * sigma));
}

void print_seidel(const SeidelCoeffs *sc) {
    if (!sc) return;
    printf("\n--- Seidel Aberration Coefficients ---\n");
    printf("  W040  Spherical aberration : %+.6f waves\n", sc->W[0]);
    printf("  W131  Coma                 : %+.6f waves\n", sc->W[1]);
    printf("  W222  Astigmatism          : %+.6f waves\n", sc->W[2]);
    printf("  W220  Field curvature      : %+.6f waves\n", sc->W[3]);
    printf("  W311  Distortion           : %+.6f waves\n", sc->W[4]);
    printf("  RMS wavefront error        : %.6f waves\n",  sc->rms_wavefront);
    printf("  Strehl ratio (Marechal)    : %.4f\n",        sc->strehl);
    printf("--------------------------------------\n\n");
}

/* =========================================================
 * SECTION 6 — FULL ISP PIPELINE
 * ========================================================= */

void free_aberration_result(AberrationPipelineResult *r) {
    if (!r) return;
    if (r->wrapped_phase)   free_phase_map(r->wrapped_phase);
    if (r->unwrapped_phase) free_phase_map(r->unwrapped_phase);
    if (r->zernike_coeffs)  free(r->zernike_coeffs);
    if (r->denoised)        freeBMPImage(r->denoised);
    if (r->phase_image)     freeBMPImage(r->phase_image);
    free(r);
}

BMPImage *phase_map_to_bmp(const PhaseMap *pm) {
    int      w, h, i, j;
    BMPImage *out;
    double   mn, mx, range, val;

    if (!pm) return NULL;
    w = pm->width; h = pm->height;

    mn = pm->data[0][0]; mx = mn;
    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++) {
            if (pm->data[i][j] < mn) mn = pm->data[i][j];
            if (pm->data[i][j] > mx) mx = pm->data[i][j];
        }

    range = mx - mn;
    out   = createEmptyBMP(w, h, 8);
    if (!out) return NULL;

    for (i = 0; i < h; i++)
        for (j = 0; j < w; j++) {
            val = (range > 1e-12)
                ? (pm->data[i][j] - mn) / range * 255.0
                : 0.0;
            out->pixels[i][j] = (unsigned char)clampd(val, 0.0, 255.0);
        }
    return out;
}

/*
 * run_aberration_pipeline
 *
 * Corrected stage order:
 *
 *   Stage 1a — Wiener deconvolution (on raw interferogram)
 *              Noise is estimated from the original blurred image so
 *              that the adaptive K-spectrum reflects true acquisition
 *              noise, not an already-smoothed version of it.
 *
 *   Stage 1b — Wavelet denoise (on deconvolved image)
 *              The Wiener filter amplifies noise near OTF nulls even
 *              with adaptive regularisation.  Wavelet soft-thresholding
 *              cleans up this residual noise.  The threshold is
 *              auto-estimated from the deconvolved image (thresh=0),
 *              which is correct because noise statistics change after
 *              deconvolution.
 *
 *   Stage 2  — WVD phase extraction
 *   Stage 3  — Phase quality + quality-guided unwrapping
 *   Stage 4  — Zernike fit (order 6, 28 terms)
 *   Stage 5  — Zernike → Seidel conversion
 */
AberrationPipelineResult *run_aberration_pipeline(BMPImage *interferogram,
                                                   Kernel   *blur_kernel,
                                                   int       use_quality_unwrap) {
    AberrationPipelineResult *result;
    BMPImage    *denoised;
    PhaseMap    *wrapped, *unwrapped;
    ZernikeBasis *zb;
    double      *zc;
    int          w, h;

    if (!interferogram) return NULL;

    result = (AberrationPipelineResult *)calloc(1, sizeof(*result));
    if (!result) return NULL;

    w = interferogram->info_header.width_px;
    h = abs(interferogram->info_header.height_px);

    /* ----------------------------------------------------------------
     * Stage 1a: Wiener deconvolution — performed first, on the raw
     * interferogram.  If no blur kernel is supplied the stage is
     * skipped and the raw image is passed directly to the wavelet step.
     * ---------------------------------------------------------------- */
    {
        BMPImage *current = interferogram; /* default: bypass Wiener */

        if (blur_kernel) {
            Filter   *wf;
            BMPImage *w_out;

            /*
             * Noise estimate comes from the original blurred image.
             * This gives an honest noise level for building K(u,v).
             */
            wf    = create_wiener_filter_auto(FILTER_DOMAIN_DFT,
                                              w, h, blur_kernel,
                                              interferogram);
            w_out = apply_frequency_filter_to_bmp(interferogram, wf);
            destroy_wiener_filter(wf);

            if (w_out)
                current = w_out; /* deconvolved image, owned locally */
        }

        /* --------------------------------------------------------------
         * Stage 1b: Wavelet denoise — on the deconvolved (or original)
         * image.  thresh = 0 triggers the automatic VisuShrink estimator
         * which reads the HH1 subband of `current`, giving the right
         * noise level for whatever the Wiener stage left behind.
         * -------------------------------------------------------------- */
        denoised = apply_wavelet_denoise(current, 3, 0.0,
                                         WAVELET_THRESH_SOFT);
        if (!denoised)
            denoised = current; /* fallback: keep deconvolved image as-is */

        /*
         * Free the intermediate Wiener output now that the wavelet step
         * has produced its own allocation (or fell back to current).
         */
        if (current != interferogram && current != denoised)
            freeBMPImage(current);

        result->denoised = denoised;
    }

    /* ---- Stage 2: WVD phase extraction ------------------------- */
    wrapped               = extract_phase_wvd(denoised, 0);
    result->wrapped_phase = wrapped;

    /* ---- Stage 3: phase unwrapping ----------------------------- */
    unwrapped = create_phase_map(w, h);
    if (unwrapped && wrapped) {
        int i;
        for (i = 0; i < h; i++)
            memcpy(unwrapped->data[i], wrapped->data[i],
                   w * sizeof(double));

        if (use_quality_unwrap) {
            compute_phase_quality(unwrapped);
            unwrap_phase_quality(unwrapped);
        } else {
            unwrap_phase_simple(unwrapped);
        }
    }
    result->unwrapped_phase = unwrapped;

    /* ---- Stage 4: Zernike fit ---------------------------------- */
    zc = (double *)calloc(ZERNIKE_TERMS, sizeof(double));
    result->zernike_coeffs = zc;

    if (zc && unwrapped) {
        zb = create_zernike_basis(w, h, ZERNIKE_TERMS);
        if (zb) {
            fit_zernike(unwrapped, zb, zc);
            free_zernike_basis(zb);
        }
    }

    /* ---- Stage 5: Seidel conversion ---------------------------- */
    if (zc) {
        zernike_to_seidel(zc, &result->seidel);
        print_seidel(&result->seidel);
    }

    /* ---- Stage 6: export phase BMP ----------------------------- */
    if (unwrapped)
        result->phase_image = phase_map_to_bmp(unwrapped);

    return result;
}