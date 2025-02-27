#include "Utils.hpp"

using namespace std;

void fcloseOrDie(FILE* file)
{
    if (!fclose(file))
    {
        return;
    };
    /* error */
    perror("fclose error");
    exit(1);
}

FILE* fopenOrDie(const char* filename, const char* instruction)
{
    FILE* const inFile = fopen(filename, instruction);
    if (inFile)
        return inFile;
    /* error */
    perror(filename);
    exit(1);
}

size_t freadOrDie(void* buffer, size_t sizeToRead, FILE* file)
{
    size_t const readSize = fread(buffer, 1, sizeToRead, file);
    if (readSize == sizeToRead)
        return readSize; /* good */
    if (feof(file))
        return readSize; /* good, reached end of file */
    /* error */
    perror("fread");
    exit(1);
}

size_t count_lines(const std::string& fpath)
{
    std::ifstream in(fpath);
    size_t count = 0;
    std::string line;
    while (getline(in, line))
    {
        count++;
    }
    return count;
}

std::string timestamp()
{
    auto t1 = std::chrono::system_clock::now();
    std::time_t tc = std::chrono::system_clock::to_time_t(t1);
    std::string str(std::ctime(&tc));
    str.pop_back(); // str[str.size() - 1] = '.';
    str = std::string("[") + str + std::string("] ");
    return str;
}

// structured permutation with cached buffer
void permute_plink(std::string& fin, const std::string& fout, uint gb, uint nbands)
{
    cout << timestamp() << "begin to permute plink data.\n";
    uint64 nsnps = count_lines(fin + ".bim");
    uint64 nsamples = count_lines(fin + ".fam");
    uint64 bed_bytes_per_snp = (nsamples + 3) >> 2;

    // calculate the readin number of snps of certain big buffer like 2GB.
    // must be a multiple of nbands.
    uint64 twoGB = (uint64)1073741824 * gb;
    uint64 twoGB_snps = (uint64)floor((double)twoGB / bed_bytes_per_snp);
    if (twoGB_snps > nsnps)
        twoGB_snps = nsnps;
    uint64 bufsize = (uint64)floor((double)twoGB_snps / nbands);
    twoGB_snps = bufsize * nbands; // initially twoGB_snps is a multiple of nbands
    assert(nsnps >= twoGB_snps);
    uint64 nblocks = (uint64)ceil((double)nsnps / twoGB_snps);
    uint modr2 = nsnps % twoGB_snps;
    uint64 bed_bytes_per_block = bed_bytes_per_snp * twoGB_snps;
    vector<uchar> inbed; // keep the input buffer
    inbed.resize(bed_bytes_per_block);
    vector<uchar> outbed; // keep the output buffer
    uint64 out_bytes_per_block = bed_bytes_per_snp * bufsize;
    outbed.resize(out_bytes_per_block);

    // get index of first snp of each band
    vector<uint64> bandidx;
    bandidx.resize(nbands);
    uint modr = nsnps % nbands;
    uint64 bandsize = (uint64)ceil((double)nsnps / nbands);
    if (modr == 0)
    {
        for (uint i = 0; i < nbands; ++i)
        {
            bandidx[i] = i * bandsize;
        }
    }
    else
    {
        for (uint i = 0; i < nbands; ++i)
        {
            if (i < modr)
            {
                bandidx[i] = i * bandsize;
            }
            else
            {
                bandidx[i] = modr * bandsize + (bandsize - 1) * (i - modr);
            }
        }
    }

    ios_base::sync_with_stdio(false);
    std::ifstream in(fin + ".bed", std::ios::binary);
    std::ofstream out(fout + ".bed", std::ios::binary);
    if (!in.is_open())
    {
        throw std::invalid_argument(colerror + "Cannot open bed file.\n");
    }
    uchar header[3];
    in.read(reinterpret_cast<char*>(&header[0]), 3);
    if ((header[0] != 0x6c) || (header[1] != 0x1b) || (header[2] != 0x01))
    {
        throw std::invalid_argument(colerror + "Incorrect magic number in bed file.\n");
    }
    out.write(reinterpret_cast<char*>(&header[0]), 3);
    std::ifstream in_bim(fin + ".bim", std::ios::in);
    std::ofstream out_bim(fout + ".bim", std::ios::out);
    vector<std::string> bims(std::istream_iterator<Line>{in_bim}, std::istream_iterator<Line>{});
    vector<std::string> bims2;
    bims2.resize(nsnps);
    uint64 b, i, j, twoGB_snps2, idx, bufidx = bufsize;
    for (i = 0; i < nblocks; i++)
    {
        if (i == nblocks - 1 && modr2 != 0)
        {
            twoGB_snps2 = nsnps - (nblocks - 1) * twoGB_snps;
            bed_bytes_per_block = bed_bytes_per_snp * twoGB_snps2;
            inbed.resize(bed_bytes_per_block);
            // in last block, twoGB_snps is not neccessary a multiple of nbands and smaller than the previous
            bufsize = (uint64)ceil((double)twoGB_snps2 / nbands);
            modr2 = twoGB_snps2 % nbands;
            out_bytes_per_block = bed_bytes_per_snp * bufsize;
            outbed.resize(out_bytes_per_block);
        }
        in.read(reinterpret_cast<char*>(&inbed[0]), bed_bytes_per_block);
        for (b = 0; b < nbands; b++)
        {
            idx = 3 + (i * bufidx + bandidx[b]) * bed_bytes_per_snp;
            for (j = 0; j < bufsize - 1; j++)
            {
                std::copy(inbed.begin() + (j * nbands + b) * bed_bytes_per_snp, inbed.begin() + (j * nbands + b + 1) * bed_bytes_per_snp,
                          outbed.begin() + j * bed_bytes_per_snp);
                // cout << i * twoGB_snps + j * nbands + b << endl;
                bims2[i * bufidx + bandidx[b] + j] = bims[i * twoGB_snps + j * nbands + b];
            }
            if (i != nblocks - 1 || (i == nblocks - 1 && b < modr2) || modr2 == 0)
            {
                std::copy(inbed.begin() + (j * nbands + b) * bed_bytes_per_snp, inbed.begin() + (j * nbands + b + 1) * bed_bytes_per_snp,
                          outbed.begin() + j * bed_bytes_per_snp);
                bims2[i * bufidx + bandidx[b] + j] = bims[i * twoGB_snps + j * nbands + b];
            }
            else
            {
                out_bytes_per_block = bed_bytes_per_snp * (bufsize - 1);
            }
            out.seekp(idx, std::ios_base::beg);
            out.write(reinterpret_cast<char*>(&outbed[0]), out_bytes_per_block);
        }
    }
    for (auto b : bims2)
    {
        out_bim << b << "\n";
    }
    in.close();
    out.close();
    out_bim.close();

    std::ifstream in_fam(fin + ".fam");
    std::ofstream out_fam(fout + ".fam");
    out_fam << in_fam.rdbuf();
    fin = fout;
}


// Sign correction to ensure deterministic output from SVD.
// see https://www.kite.com/python/docs/sklearn.utils.extmath.svd_flip
void flip_UV(MyMatrix& U, MyMatrix& V, bool ubase)
{
    if (ubase)
    {
        Eigen::Index x, i;
        for (i = 0; i < U.cols(); ++i)
        {
            U.col(i).cwiseAbs().maxCoeff(&x);
            if (U(x, i) < 0)
            {
                U.col(i) *= -1;
                if (V.cols() == U.cols())
                {
                    V.col(i) *= -1;
                }
                else if (V.rows() == U.cols())
                {
                    V.row(i) *= -1;
                }
                else
                {
                    throw std::runtime_error("the dimention of U and V have different k ranks.\n");
                }
            }
        }
    }
    else
    {
        Eigen::Index x, i;
        for (i = 0; i < V.cols(); ++i)
        {
            if (V.cols() == U.cols())
            {
                V.col(i).cwiseAbs().maxCoeff(&x);
                if (V(x, i) < 0)
                {
                    U.col(i) *= -1;
                    V.col(i) *= -1;
                }
            }
            else if (V.rows() == U.cols())
            {
                V.row(i).cwiseAbs().maxCoeff(&x);
                if (V(i, x) < 0)
                {
                    U.col(i) *= -1;
                    V.row(i) *= -1;
                }
            }
            else
            {
                throw std::runtime_error("the dimention of U and V have different k ranks.\n");
            }
        }
    }
}

void flip_Omg(MyMatrix& Omg2, MyMatrix& Omg)
{
    for (Eigen::Index i = 0; i < Omg.cols(); ++i)
    {
        // if signs of half of values are flipped then correct signs.
        if ((Omg2.col(i) - Omg.col(i)).array().abs().sum() > 2 * (Omg2.col(i) + Omg.col(i)).array().abs().sum())
        {
            Omg.col(i) *= -1;
        }
    }
    Omg2 = Omg;
}

void flip_Y(const MyMatrix& X, MyMatrix& Y)
{
    for (Eigen::Index i = 0; i < X.cols(); ++i)
    {
        // if signs of half of values are flipped then correct signs.
        if ((X.col(i) - Y.col(i)).array().abs().sum() > 2 * (X.col(i) + Y.col(i)).array().abs().sum())
        {
            Y.col(i) *= -1;
        }
    }
}

double rmse(const MyMatrix& X, const MyMatrix& Y)
{
    MyMatrix Z = Y;
    flip_Y(X, Z);
    return sqrt((X - Z).array().square().sum() / (X.cols() * X.rows()));
}

// Y is the truth matrix, X is the test matrix
Eigen::VectorXd minSSE(const MyMatrix& X, const MyMatrix& Y)
{
    Eigen::Index w1, w2;
    Eigen::VectorXd res(X.cols());
    for (Eigen::Index i = 0; i < X.cols(); ++i)
    {
        // test against the original matrix to find the index with mincoeff
        ((-Y).colwise() + X.col(i)).array().square().colwise().sum().minCoeff(&w1);
        // test against the flipped matrix with the opposite sign
        (Y.colwise() + X.col(i)).array().square().colwise().sum().minCoeff(&w2);
        // get the minSSE value for X.col(i) against -Y.col(w1)
        auto val1 = (-Y.col(w1) + X.col(i)).array().square().sum();
        // get the minSSE value for X.col(i) against Y.col(w2)
        auto val2 = (Y.col(w2) + X.col(i)).array().square().sum();
        if (w1 != w2 && val1 > val2)
            res[i] = val2;
        else
            res[i] = val1;
    }
    return res;
}

double mev(const MyMatrix& X, const MyMatrix& Y)
{
    double res = 0;
    for (Eigen::Index i = 0; i < X.cols(); ++i)
    {
        res += (X.transpose() * Y.col(i)).norm();
    }
    return res / X.cols();
}

void mev_rmse_byk(const MyMatrix& X, const MyMatrix& Y, MyVector& Vm, MyVector& Vr)
{
    for (Eigen::Index i = 0; i < X.cols(); ++i)
    {
        Vm(i) = 1 - mev(X.leftCols(i + 1), Y.leftCols(i + 1));
        Vr(i) = rmse(X.leftCols(i + 1), Y.leftCols(i + 1));
    }
}

double get_median(std::vector<double> v)
{
    size_t n = v.size();
    if (n == 0)
    {
        return 0;
    }
    else
    {
        std::sort(v.begin(), v.end());
        if (n % 2 == 0)
        {
            return (v[n / 2 - 1] + v[n / 2]) / 2.0;
        }
        else
        {
            return v[n / 2];
        }
    }
}
