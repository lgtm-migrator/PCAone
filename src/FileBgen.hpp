#ifndef __FileBgen__
#define __FileBgen__

#include "Data.hpp"
#include "bgen/bgen.h"

// const double GENOTYPE_THRESHOLD = 0.9;
// const double BGEN_MISSING_VALUE = -9;
// const double BGEN2GENO[4] = {0, 0.5, 1, BGEN_MISSING_VALUE};

class FileBgen : public Data
{
public:
    // using Data::Data;
    FileBgen(const Param& params_) : Data(params_)
        {
            bg = new bgen::Bgen(params.bgen, "", true);
            nsamples = bg->header.nsamples;
            nsnps = bg->header.nvariants;
            std::cout << timestamp() << "the layout of bgen file is " << bg->header.layout << ". N samples is " << nsamples << ". M snps is " << nsnps << std::endl;
        }

    ~FileBgen() { delete bg; }

    virtual void read_all_and_centering();
    // for blockwise
    virtual void read_snp_block_initial(uint64 start_idx, uint64 stop_idx, bool standardize = false);
    virtual void read_snp_block_update(uint64 start_idx, uint64 stop_idx, const MatrixXd& U, const VectorXd& svals, const MatrixXd& VT, bool standardize = false) {}
    virtual void check_file_offset_first_var() { bg->set_offset_first_var(); }

private:
    bgen::Bgen* bg;
    bgen::Variant var;
    float* dosages;
    float* probs1d;
    bool frequency_was_estimated = false;

};



#endif
