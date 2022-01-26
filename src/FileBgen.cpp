#include "FileBgen.hpp"

using namespace std;

void FileBgen::read_all_and_centering()
{
    uint i, j, gc;
    double gs;
    cout << timestamp() << "begin to parse the bgen file.\n";
    if (!params.pcangsd)
    {
        F = MyVector::Zero(nsnps);
        G = MyMatrix(nsamples, nsnps);
        if (params.runem) C.resize(nsnps * nsamples);
        for (j = 0; j < nsnps; j++) {
            try {
                var = bg->next_var();
                dosages = var.minor_allele_dosage();
                gc = 0; gs = 0.0;
                // calculate allele frequency
                #pragma omp parallel for reduction(+:gc) reduction(+:gs)
                for (i = 0; i < nsamples; i++) {
                    if (std::isnan(dosages[i])) {
                        if (params.runem) C[j * nsamples + i] = 1;
                    } else {
                        if (params.runem) C[j * nsamples + i] = 0;
                        G(i, j) = dosages[i] / 2.0; // map to [0, 1];
                        gs += G(i, j);
                        gc += 1;
                    }
                }
                if (gc==0) throw std::runtime_error("Error: the allele frequency should not be 0. should do filtering first.");
                F(j) = (double) gs / gc;
                // do centering and initialing
                #pragma omp parallel for
                for (i = 0; i < nsamples; i++) {
                    if (std::isnan(dosages[i])) {
                        G(i, j) = 0;
                    } else {
                        G(i, j) -= F(j);
                    }
                }
            } catch (const std::out_of_range & e) {
                break;
            }
        }
    } else {
        // read all GP data into P;
        for (j = 0; j < nsnps; j++) {
            try {
                var = bg->next_var();
                probs1d = var.probs_1d();
                #pragma omp parallel for
                for (i = 0; i < nsamples; i++) {
                    P(j, i * 3 + 0) = probs1d[i * 3 + 0];
                    P(j, i * 3 + 1) = probs1d[i * 3 + 1];
                    P(j, i * 3 + 2) = probs1d[i * 3 + 2];
                }
            } catch (const std::out_of_range & e) {
                break;
            }
        }
        assert( j == nsnps );
        cout << timestamp() << "begin to estimate allele frequencies using GP" << endl;
        MyVector Ft(nsnps);
        F = MyVector::Constant(nsnps, 0.25);
        // run EM to estimate allele frequencies
        double diff;
        for (uint it = 0; it < params.maxiter; it++)
        {
            #pragma omp parallel for
            for (uint j = 0; j < nsnps; j++) {
                Ft(j) = F(j);
                double p0, p1, p2, pt = 0.0;
                for (uint i = 0; i < nsamples; i++) {
                    p0 = P(j, 3 * i + 0) * (1.0 - F(j)) * (1.0 - F(j));
                    p1 = P(j, 3 * i + 1) * 2 * F(j) * (1.0 - F(j));
                    p2 = P(j, 3 * i + 2) * F(j) * F(j);
                    pt += (p1 + 2 * p2) / (2 * (p0 + p1 + p2));
                }
                F(j) = pt / (double) nsamples;
            }
            // calculate differences between iterations
            diff = sqrt((F - Ft).array().square().sum() / nsnps);
            // Check for convergence
            if (diff < params.tolmaf) {
                cout << "EM (MAF) converged at iteration: " << it+1 << endl;
                break;
            } else if (it == (params.maxiter-1)) {
                cerr << "EM (MAF) did not converge.\n";
            }
        }
        // initial E which is G
        G = MyMatrix(nsamples, nsnps);
        #pragma omp parallel for
        for (j = 0; j < nsnps; j++) {
            double p0, p1, p2;
            for (i = 0; i < nsamples; i++) {
                p0 = P(j, 3 * i + 0) * (1.0 - F(j)) * (1.0 - F(j));
                p1 = P(j, 3 * i + 1) * 2 * F(j) * (1.0 - F(j));
                p2 = P(j, 3 * i + 2) * F(j) * F(j);
                G(i, j) = (p1 + 2 * p2)/(p0 + p1 + p2) - 2.0 * F(j);
            }
        }
    }
}

void FileBgen::read_snp_block_initial(uint64 start_idx, uint64 stop_idx, bool standardize)
{
    uint actual_block_size = stop_idx - start_idx + 1;
    uint i, j, snp_idx;
    if (G.cols() < params.blocksize || (actual_block_size < params.blocksize))
    {
        G = MyMatrix::Zero(nsamples, actual_block_size);
    }
    if (frequency_was_estimated)
    {
        for (i = 0; i < actual_block_size; ++i)
        {
            snp_idx = start_idx + i;
            var = bg->next_var();
            dosages = var.minor_allele_dosage();
            #pragma omp parallel for
            for (j = 0; j < nsamples; j++) {
                if (std::isnan(dosages[j])) {
                    G(j, i) = 0;
                } else {
                    G(j, i) = dosages[j]/2.0 - F(snp_idx);
                }
                if (standardize && sqrt(F(snp_idx) * (1 - F(snp_idx))) > VAR_TOL) G(j, i) /= sqrt(F(snp_idx) * (1 - F(snp_idx)));
            }
        }
    } else {
        uint gc;
        double gs;
        for (i = 0; i < actual_block_size; ++i)
        {
            snp_idx = start_idx + i;
            var = bg->next_var();
            dosages = var.minor_allele_dosage();
            gc = 0; gs = 0.0;
            #pragma omp parallel for reduction(+:gc) reduction(+:gs)
            for (j = 0; j < nsamples; j++) {
                if (std::isnan(dosages[j])) {
                    G(j, i) = 0;
                } else {
                    G(j, i) = dosages[j]/2.0;
                    gs += G(j, i);
                    gc += 1;
                }
            }
            if (gc==0) throw std::runtime_error("Error: the allele frequency should not be 0. should do filtering first.");
            F(snp_idx) = (double) gs / gc;
            // do centering
            #pragma omp parallel for
            for (j = 0; j < nsamples; j++) {
                if (std::isnan(dosages[j])) {
                    G(j, i) = 0;
                } else {
                    G(j, i) -= F(snp_idx);
                }
                if (standardize && sqrt(F(snp_idx) * (1 - F(snp_idx))) > VAR_TOL) G(j, i) /= sqrt(F(snp_idx) * (1 - F(snp_idx)));
            }
        }
        if (stop_idx + 1 == nsnps) frequency_was_estimated = true;
    }
}
