#ifndef I_SUPERBLOCK_SUBSIDY_CONTAINER_H
#define I_SUPERBLOCK_SUBSIDY_CONTAINER_H
class I_SuperblockHeightValidator;
class I_BlockSubsidyProvider;
class I_SuperblockSubsidyContainer
{
public:
    virtual ~I_SuperblockSubsidyContainer(){};
    virtual const I_SuperblockHeightValidator& superblockHeightValidator() const = 0;
    virtual const I_BlockSubsidyProvider& blockSubsidiesProvider() const = 0;
};
#endif// I_SUPERBLOCK_SUBSIDY_CONTAINER_H