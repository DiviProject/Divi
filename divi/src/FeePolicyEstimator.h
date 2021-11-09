#ifndef FEE_POLICY_ESTIMATOR_H
#define FEE_POLICY_ESTIMATOR_H
#include <boost/circular_buffer.hpp>
#include <FeeRate.h>

class CAutoFile;
class CTxMemPoolEntry;
/**
 * Keep track of fee/priority for transactions confirmed within N blocks
 */
class CBlockAverage
{
private:
    boost::circular_buffer<CFeeRate> feeSamples;
    boost::circular_buffer<double> prioritySamples;

public:
    CBlockAverage();

    void RecordFee(const CFeeRate& feeRate);
    void RecordPriority(double priority);

    size_t FeeSamples() const;
    size_t GetFeeSamples(std::vector<CFeeRate>& insertInto) const;
    size_t PrioritySamples() const;
    size_t GetPrioritySamples(std::vector<double>& insertInto) const;

    /**
     * Used as belt-and-suspenders check when reading to detect
     * file corruption
     */
    static bool AreSane(const CFeeRate fee, const CFeeRate& minRelayFee);
    static bool AreSane(const std::vector<CFeeRate>& vecFee, const CFeeRate& minRelayFee);
    static bool AreSane(const double priority);
    static bool AreSane(const std::vector<double> vecPriority);

    void Write(CAutoFile& fileout) const;
    void Read(CAutoFile& filein, const CFeeRate& minRelayFee);
};

class FeePolicyEstimator
{
private:
    /**
     * Records observed averages transactions that confirmed within one block, two blocks,
     * three blocks etc.
     */
    std::vector<CBlockAverage> history;
    std::vector<CFeeRate> sortedFeeSamples;
    std::vector<double> sortedPrioritySamples;

    int nBestSeenHeight;

    /**
     * nBlocksAgo is 0 based, i.e. transactions that confirmed in the highest seen block are
     * nBlocksAgo == 0, transactions in the block before that are nBlocksAgo == 1 etc.
     */
    void seenTxConfirm(const CFeeRate& feeRate, const CFeeRate& minRelayFee, double coinAgeOfInputs, int nBlocksAgo);

public:
    FeePolicyEstimator(int nEntries);

    void seenBlock(const std::vector<const CTxMemPoolEntry*>& entries, int nBlockHeight, const CFeeRate minRelayFee);
    CFeeRate estimateFee(int nBlocksToConfirm);
    double estimatePriority(int nBlocksToConfirm);
    void Write(CAutoFile& fileout) const;
    void Read(CAutoFile& filein, const CFeeRate& minRelayFee);
};
#endif // FEE_POLICY_ESTIMATOR_H